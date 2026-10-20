#include "selection.hpp"

#include <array>
#include <cctype>
#include <optional>
#include <set>
#include <vector>

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

namespace cs2::protect {

namespace {

/// The annotation the source-level selection uses.
constexpr std::string_view k_protect_annotation = "cs2.protect";

/// The string of a constant. The annotation string is a global whose
/// initializer holds the characters.
[[nodiscard]] auto constant_string(const llvm::Constant* constant) noexcept -> std::string_view {
  if (const auto* global = llvm::dyn_cast<llvm::GlobalVariable>(constant)) {
    constant = global->getInitializer();
  }
  const auto* data = llvm::dyn_cast_or_null<llvm::ConstantDataSequential>(constant);
  if (data == nullptr || !data->isCString()) {
    return {};
  }
  return data->getAsCString();
}

/// Reads the annotated functions out of `llvm.global.annotations`.
///
/// The annotation keeps the annotated function alive and blocks inlining, so
/// the entry is keyed by the *mangled* symbol name.
[[nodiscard]] auto annotated_functions(const llvm::Module& module) -> std::set<std::string> {
  std::set<std::string> result;
  const llvm::GlobalVariable* annotations = module.getGlobalVariable("llvm.global.annotations");
  if (annotations == nullptr) {
    return result;
  }
  for (const llvm::Use& operand : annotations->operands()) {
    const auto* array = llvm::dyn_cast<llvm::ConstantArray>(operand.get());
    if (array == nullptr) {
      continue;
    }
    for (const llvm::Use& element : array->operands()) {
      const auto* entry = llvm::dyn_cast<llvm::ConstantStruct>(element.get());
      if (entry == nullptr || entry->getNumOperands() < 2) {
        continue;
      }
      const auto* text = llvm::dyn_cast<llvm::Constant>(entry->getOperand(1));
      if (text == nullptr) {
        continue;
      }
      std::string_view annotation = constant_string(text);
      // LLVM marks an annotation string with a leading '\01'.
      if (!annotation.empty() && annotation.front() == '\01') {
        annotation.remove_prefix(1);
      }
      if (annotation != k_protect_annotation) {
        continue;
      }
      const llvm::Value* target = entry->getOperand(0)->stripPointerCasts();
      const auto* function = llvm::dyn_cast<llvm::Function>(target);
      if (function == nullptr) {
        continue;
      }
      result.insert(function->getName().str());
    }
  }
  return result;
}

/// Whether the pattern matches the symbol, either by its mangled name or by
/// its demangled qualified name.
[[nodiscard]] auto matches(std::string_view pattern, std::string_view symbol,
                           std::string_view qualified) noexcept -> bool {
  return glob_match(pattern, symbol) || (!qualified.empty() && glob_match(pattern, qualified));
}

/// The calling conventions the MSVC demangler prints between the return type
/// and the name.
constexpr std::array<std::string_view, 9> k_calling_conventions = {
    "__cdecl",   "__fastcall", "__thiscall",  "__vectorcall",     "__stdcall",
    "__clrcall", "__usercall", "__swiftcall", "__swiftasynccall",
};

/// The access labels the MSVC demangler prints in front of a member function.
constexpr std::array<std::string_view, 3> k_access_labels = {
    "public: ", "private: ", "protected: "};

/// Whether the text ends with the `operator` token.
///
/// The token has to be a whole word: `my_operator` is a name, `operator<` is
/// an operator.
[[nodiscard]] auto ends_with_operator(std::string_view text) noexcept -> bool {
  constexpr std::string_view k_operator = "operator";
  if (!text.ends_with(k_operator)) {
    return false;
  }
  if (text.size() == k_operator.size()) {
    return true;
  }
  const char before = text[text.size() - k_operator.size() - 1];
  return std::isalnum(static_cast<unsigned char>(before)) == 0 && before != '_';
}

/// The start of the last path component of a qualified name.
///
/// The components are separated by "::", but a "::" inside a template
/// argument list ("std::map<int, int>") or inside parentheses
/// ("operator()") does not separate them.
[[nodiscard]] auto last_component_start(std::string_view name) noexcept -> std::size_t {
  int depth = 0;
  for (std::size_t i = name.size(); i-- > 0;) {
    switch (name[i]) {
      case '>':
      case ')':
        ++depth;
        break;
      case '<':
      case '(':
        --depth;
        break;
      case ':':
        if (depth == 0 && i > 0 && name[i - 1] == ':') {
          return i + 1;
        }
        break;
      default:
        break;
    }
  }
  return 0;
}

/// The name without its template argument list.
///
/// "cs2::aim::solve<int>" is "cs2::aim::solve": a policy entry names the
/// function, not its instantiation. Only the last component is cut, and an
/// operator name is never cut ("operator<" is not a template).
[[nodiscard]] auto without_template_arguments(std::string_view name) noexcept -> std::string_view {
  const std::size_t start = last_component_start(name);
  const std::string_view component = name.substr(start);
  for (std::size_t i = 0; i < component.size(); ++i) {
    if (component[i] != '<') {
      continue;
    }
    if (ends_with_operator(component.substr(0, i))) {
      continue;
    }
    return name.substr(0, start + i);
  }
  return name;
}

/// Whether the space at `position` is inside a parenthesised group.
///
/// The Itanium demangler writes an anonymous namespace as
/// "(anonymous namespace)", and that space belongs to the name.
[[nodiscard]] auto inside_parentheses(std::string_view head, std::size_t position) noexcept
    -> bool {
  int depth = 0;
  for (std::size_t i = position; i-- > 0;) {
    if (head[i] == ')') {
      ++depth;
    } else if (head[i] == '(') {
      if (depth == 0) {
        return true;  // this '(' encloses the position
      }
      --depth;
    }
  }
  return false;
}

}  // namespace

[[nodiscard]] auto to_string(SkipReason reason) noexcept -> const char* {
  switch (reason) {
    case SkipReason::denied:
      return "denied";
    case SkipReason::optnone:
      return "optnone";
    case SkipReason::always_inline:
      return "always_inline";
    case SkipReason::declaration:
      return "declaration";
    case SkipReason::intrinsic:
      return "intrinsic";
    case SkipReason::not_annotated:
    default:
      return "not_annotated";
  }
}

[[nodiscard]] auto glob_match(std::string_view pattern, std::string_view text) noexcept -> bool {
  std::size_t pattern_pos = 0;
  std::size_t text_pos = 0;
  std::size_t star = std::string_view::npos;
  std::size_t star_text = 0;
  while (text_pos < text.size()) {
    if (pattern_pos < pattern.size() &&
        (pattern[pattern_pos] == '?' || pattern[pattern_pos] == text[text_pos])) {
      ++pattern_pos;
      ++text_pos;
      continue;
    }
    if (pattern_pos < pattern.size() && pattern[pattern_pos] == '*') {
      star = pattern_pos++;
      star_text = text_pos;
      continue;
    }
    if (star != std::string_view::npos) {
      pattern_pos = star + 1;
      text_pos = ++star_text;
      continue;
    }
    return false;
  }
  while (pattern_pos < pattern.size() && pattern[pattern_pos] == '*') {
    ++pattern_pos;
  }
  return pattern_pos == pattern.size();
}

[[nodiscard]] auto qualified_name(std::string_view symbol) -> std::string {
  const std::string demangled = llvm::demangle(symbol);
  if (demangled.empty() || demangled == symbol) {
    return {};
  }

  // The parameter list is the last parenthesised group: scan back from the
  // final ')' to the '(' that opens it.
  const auto close = demangled.rfind(')');
  if (close == std::string::npos) {
    return {};
  }
  int depth = 0;
  std::size_t open = std::string::npos;
  for (std::size_t i = close + 1; i-- > 0;) {
    if (demangled[i] == ')') {
      ++depth;
    } else if (demangled[i] == '(') {
      --depth;
      if (depth == 0) {
        open = i;
        break;
      }
    }
  }
  if (open == std::string::npos) {
    return {};
  }

  // The head is "<return type> <qualified name>" (the MSVC demangler) or
  // "<qualified name>" (the Itanium one, which prints no return type for an
  // ordinary function).
  std::string_view head(demangled.data(), open);
  while (!head.empty() && (head.back() == ' ' || head.back() == '\t')) {
    head.remove_suffix(1);
  }
  // A function pointer type ("int (__cdecl *)(void)") carries its parameter
  // list in the middle: there is no qualified name to report. A name that
  // holds parentheses ("operator()") is not one.
  if (head.empty() || head.find("(*") != std::string_view::npos) {
    return {};
  }
  for (const std::string_view label : k_access_labels) {
    if (head.starts_with(label)) {
      head.remove_prefix(label.size());
      break;
    }
  }

  std::string_view name = head;
  std::size_t after_convention = std::string_view::npos;
  for (const std::string_view convention : k_calling_conventions) {
    const auto position = head.find(convention);
    if (position == std::string_view::npos) {
      continue;
    }
    const std::size_t after = position + convention.size();
    if (after < head.size() && head[after] != ' ') {
      continue;  // not a whole word ("__cdecl_helper")
    }
    after_convention = after;
    break;
  }
  if (after_convention != std::string_view::npos) {
    // The MSVC demangler prints the calling convention between the return
    // type and the name: the name is what follows it.
    name = head.substr(after_convention);
  } else {
    // The Itanium demangler prints no return type for an ordinary function,
    // so the head is the name. A function template carries its return type
    // ("int cs2::aim::solve<3>"), which the space separates -- unless the
    // space belongs to the name itself (an operator name, an anonymous
    // namespace).
    const auto space = head.find(' ');
    if (space != std::string_view::npos && !inside_parentheses(head, space) &&
        !ends_with_operator(head.substr(0, space)) &&
        !head.substr(space + 1).starts_with("operator")) {
      name = head.substr(head.find_last_of(' ') + 1);
    }
  }

  while (!name.empty() && (name.front() == ' ' || name.front() == '*' || name.front() == '&')) {
    name.remove_prefix(1);
  }
  return std::string(without_template_arguments(name));
}

[[nodiscard]] auto derive_seed(std::string_view module_id, std::string_view symbol) noexcept
    -> std::uint64_t {
  // FNV-1a: the seed only has to be stable and well spread, not secret.
  std::uint64_t hash = 14695981039346656037ULL;
  const auto mix = [&hash](std::string_view text) noexcept {
    for (const char c : text) {
      hash ^= static_cast<std::uint8_t>(c);
      hash *= 1099511628211ULL;
    }
  };
  mix(module_id);
  hash ^= 0x1FU;  // a separator, so that "ab"+"c" and "a"+"bc" differ
  hash *= 1099511628211ULL;
  mix(symbol);
  return hash;
}

[[nodiscard]] auto module_applies(std::string_view scope, std::string_view module_id) noexcept
    -> bool {
  if (scope.empty()) {
    return true;
  }
  const auto slash = module_id.find_last_of("/\\");
  const std::string_view file_name =
      slash == std::string_view::npos ? module_id : module_id.substr(slash + 1);
  if (file_name == scope) {
    return true;
  }
  // A path suffix, e.g. "aim/src/solver.cpp".
  if (module_id.size() > scope.size() &&
      module_id.compare(module_id.size() - scope.size(), scope.size(), scope) == 0) {
    const char before = module_id[module_id.size() - scope.size() - 1];
    return before == '/' || before == '\\';
  }
  return false;
}

/// The blocker that a transform cannot work around, if there is one.
[[nodiscard]] auto blocker_of(const llvm::Function& function) -> std::optional<SkipReason> {
  if (function.isIntrinsic()) {
    return SkipReason::intrinsic;
  }
  if (function.isDeclaration()) {
    return SkipReason::declaration;
  }
  if (function.hasFnAttribute(llvm::Attribute::AlwaysInline)) {
    return SkipReason::always_inline;
  }
  if (function.hasFnAttribute(llvm::Attribute::OptimizeNone)) {
    return SkipReason::optnone;
  }
  return std::nullopt;
}

/// Records that a policy entry named a function that cannot be protected.
///
/// The entry did **not** match: it protects nothing, and the build has to
/// say so. An entry that protected a function in this module keeps its
/// "matched" state -- it did protect something.
void block_entry(PolicyMatch& match, SkipReason reason) {
  if (match.matched) {
    return;
  }
  match.blocked = true;
  match.blocked_reason = reason;
}

/// Records that a policy entry named a function that the pass protects.
void match_entry(PolicyMatch& match) {
  match.matched = true;
  match.blocked = false;
  match.blocked_reason = std::nullopt;
}

auto select_functions(const llvm::Module& module, const Config& config) -> SelectionResult {
  SelectionResult result;
  const std::string& module_id = module.getSourceFileName();
  const std::set<std::string> annotated = annotated_functions(module);

  if (config.policy) {
    for (const PolicyEntry& entry : config.policy->protect) {
      PolicyMatch match;
      match.pattern = entry.pattern;
      match.module = entry.module;
      match.applicable = module_applies(entry.module, module_id);
      result.policy_matches.push_back(match);
    }
  }

  // Demangling is not free, and only a policy or the verbose report needs
  // the qualified name.
  const bool want_qualified = config.policy.has_value() || config.verbose;

  // The policy entries that name the function of the current iteration. The
  // vector is reused, so that a big translation unit does not allocate once
  // per function.
  std::vector<std::size_t> named;

  for (const llvm::Function& function : module) {
    const std::string symbol = function.getName().str();
    const std::string qualified = want_qualified ? qualified_name(symbol) : std::string();

    // The policy entries that name this function.
    named.clear();
    if (config.policy) {
      for (std::size_t i = 0; i < config.policy->protect.size(); ++i) {
        const PolicyEntry& entry = config.policy->protect[i];
        if (!module_applies(entry.module, module_id)) {
          continue;
        }
        if (!matches(entry.pattern, symbol, qualified)) {
          continue;
        }
        named.push_back(i);
      }
    }

    // The deny list wins over every selection source.
    if (config.policy) {
      bool denied = false;
      for (const std::string& pattern : config.policy->deny) {
        if (matches(pattern, symbol, qualified)) {
          denied = true;
          break;
        }
      }
      if (denied) {
        result.skipped.push_back(Skipped{symbol, qualified, SkipReason::denied});
        for (const std::size_t index : named) {
          block_entry(result.policy_matches[index], SkipReason::denied);
        }
        continue;
      }
    }

    SelectionSource source = SelectionSource::policy;
    ProtectionLevel level = ProtectionLevel::call;
    bool selected = false;
    if (annotated.find(symbol) != annotated.end()) {
      selected = true;
      source = SelectionSource::annotation;
    } else if (!named.empty() && config.policy.has_value()) {
      selected = true;
      source = SelectionSource::policy;
      level = config.policy.value().protect[named.front()].level;
    }
    if (!selected) {
      result.skipped.push_back(Skipped{symbol, qualified, SkipReason::not_annotated});
      continue;
    }

    // The function is selected; now the blockers that a transform cannot work
    // around. A policy entry that names such a function protects nothing:
    // the entry is *blocked*, not matched, and the build says so.
    const auto blocker = blocker_of(function);
    if (blocker.has_value()) {
      result.skipped.push_back(Skipped{symbol, qualified, blocker.value()});
      for (const std::size_t index : named) {
        block_entry(result.policy_matches[index], blocker.value());
      }
      continue;
    }

    Selection entry;
    entry.symbol = symbol;
    entry.qualified = qualified;
    entry.source = source;
    entry.level = level;
    entry.may_inline = !function.hasFnAttribute(llvm::Attribute::NoInline);
    if (config.seed) {
      entry.seed = *config.seed;
    } else if (config.policy && config.policy->seed) {
      entry.seed = *config.policy->seed;
    } else {
      entry.seed = derive_seed(module_id, symbol);
    }
    result.selected.push_back(std::move(entry));
    for (const std::size_t index : named) {
      match_entry(result.policy_matches[index]);
    }
  }

  return result;
}

}  // namespace cs2::protect
