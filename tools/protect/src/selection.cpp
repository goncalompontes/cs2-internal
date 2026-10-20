#include "selection.hpp"

#include <set>

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

}  // namespace

[[nodiscard]] auto to_string(SkipReason reason) noexcept -> std::string_view {
  switch (reason) {
    case SkipReason::not_annotated:
      return "not_annotated";
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
  }
  return "not_annotated";
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
  if (demangled.empty()) {
    return {};
  }
  const auto paren = demangled.find('(');
  if (paren == std::string::npos) {
    return {};
  }
  // The demangled form is "<return type> <qualified name>(<parameters>)"; the
  // qualified name is the last token before the parameter list.
  std::string_view head(demangled.data(), paren);
  const auto space = head.find_last_of(" \t");
  if (space == std::string_view::npos) {
    return {};
  }
  std::string_view name = head.substr(space + 1);
  while (!name.empty() && (name.front() == '*' || name.front() == '&')) {
    name.remove_prefix(1);
  }
  const auto angle = name.find('<');
  if (angle != std::string_view::npos) {
    name = name.substr(0, angle);
  }
  return std::string(name);
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

auto select_functions(const llvm::Module& module, const Config& config) -> SelectionResult {
  SelectionResult result;
  const std::string module_id = module.getSourceFileName();
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

  for (const llvm::Function& function : module) {
    const std::string symbol = function.getName().str();
    const std::string qualified = want_qualified ? qualified_name(symbol) : std::string();

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
        continue;
      }
    }

    SelectionSource source = SelectionSource::policy;
    ProtectionLevel level = ProtectionLevel::call;
    bool selected = false;
    if (annotated.find(symbol) != annotated.end()) {
      selected = true;
      source = SelectionSource::annotation;
    } else if (config.policy) {
      for (std::size_t i = 0; i < config.policy->protect.size(); ++i) {
        const PolicyEntry& entry = config.policy->protect[i];
        if (!module_applies(entry.module, module_id)) {
          continue;
        }
        if (!matches(entry.pattern, symbol, qualified)) {
          continue;
        }
        selected = true;
        source = SelectionSource::policy;
        level = entry.level;
        result.policy_matches[i].matched = true;
        break;
      }
    }
    if (!selected) {
      result.skipped.push_back(Skipped{symbol, qualified, SkipReason::not_annotated});
      continue;
    }

    // The function is selected; now the blockers that a transform cannot work
    // around.
    if (function.isIntrinsic()) {
      result.skipped.push_back(Skipped{symbol, qualified, SkipReason::intrinsic});
      continue;
    }
    if (function.isDeclaration()) {
      result.skipped.push_back(Skipped{symbol, qualified, SkipReason::declaration});
      continue;
    }
    if (function.hasFnAttribute(llvm::Attribute::AlwaysInline)) {
      result.skipped.push_back(Skipped{symbol, qualified, SkipReason::always_inline});
      continue;
    }
    if (function.hasFnAttribute(llvm::Attribute::OptimizeNone)) {
      result.skipped.push_back(Skipped{symbol, qualified, SkipReason::optnone});
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
  }

  return result;
}

}  // namespace cs2::protect
