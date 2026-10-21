// The plugin's own tests.
//
// The plugin is a build-time tool, so its tests live with it and run under
// CTest (see the CMakeLists.txt). They cover the parts that are easy to get
// wrong and hard to see:
//
//   * the policy reader -- a malformed policy file must be a *reported*
//     error, never a crash of the compiler, and the plugin and the checker
//     must read the same file the same way;
//   * the qualified name -- the demangled form is what a policy entry is
//     matched against, so a wrong one silently matches nothing;
//   * the selection -- an entry that named a function that cannot be
//     protected protects nothing, and the build has to say so;
//   * the coverage file -- two modules must not write the same file, and a
//     blocked entry must be recorded as blocked;
//   * the transform -- the calls of a selected function become indirect
//     dispatches, the calls that cannot be rewritten are left alone, the
//     level that is not implemented yet is refused, and a selected function
//     that did not survive is reported.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include "config.hpp"
#include "report.hpp"
#include "selection.hpp"
#include "transform.hpp"
#include "vm.hpp"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}

void check_equal(std::string_view actual, std::string_view expected, const char* what) {
  if (actual == expected) {
    return;
  }
  std::fprintf(stderr, "FAIL: %s (expected '%s', got '%s')\n", what, std::string(expected).c_str(),
               std::string(actual).c_str());
  ++g_failures;
}

/// The policy reader ---------------------------------------------------------

void test_policy_reader() {
  // A policy that parses.
  auto parsed = cs2::protect::parse_policy_text(R"({"protect": ["cs2::aim::solve"]})");
  check(parsed.has_value(), "a simple policy parses");
  if (parsed.has_value()) {
    check(parsed->protect.size() == 1, "the policy has one entry");
    check_equal(parsed->protect[0].pattern, "cs2::aim::solve", "the entry keeps its pattern");
  }

  // A duplicate key is an error on both sides (the plugin and the checker).
  auto duplicate = cs2::protect::parse_policy_text(R"({"protect": ["a"], "protect": ["b"]})");
  check(!duplicate.has_value(), "a duplicate key is rejected");
  if (!duplicate.has_value()) {
    check_equal(duplicate.error().message, "the policy file has a duplicate key 'protect'",
                "the duplicate key is named");
  }

  // A deeply nested file is an error, not a stack overflow. The SDK's parser
  // recurses, so the depth is checked before the text reaches it.
  std::string deep;
  for (int i = 0; i < 5000; ++i) {
    deep.push_back('[');
  }
  auto nested = cs2::protect::parse_policy_text(deep);
  check(!nested.has_value(), "a deeply nested policy is rejected");
  if (!nested.has_value()) {
    check(nested.error().message.find("nested too deeply") != std::string::npos,
          "the depth error says so");
  }

  // A UTF-8 byte order mark: some Windows editors write one.
  auto bom = cs2::protect::parse_policy_text(
      "\xEF\xBB\xBF"
      R"({"protect": ["a"]})");
  check(bom.has_value(), "a policy with a byte order mark parses");

  // An unknown key is an error, not a warning.
  auto unknown = cs2::protect::parse_policy_text(R"({"protekct": ["a"]})");
  check(!unknown.has_value(), "an unknown key is rejected");

  // The number grammar is the SDK's: "1.2.3" is not a number.
  auto number = cs2::protect::parse_policy_text(R"({"seed": 1.2.3})");
  check(!number.has_value(), "a malformed number is rejected");

  // A seed is 64 bits: a longer one keeps its low bits, and says so.
  auto seed = cs2::protect::parse_policy_text(R"({"seed": "0123456789ABCDEF0"})");
  check(seed.has_value(), "an over-long seed is truncated, not rejected");
  if (seed.has_value() && seed->seed.has_value()) {
    check_equal(std::to_string(*seed->seed), "1311768467463790320",
                "the low 64 bits of the seed are kept");
  }
}

/// The qualified name --------------------------------------------------------

void test_qualified_name() {
  // The MSVC mangling (the payload is built with clang-cl).
  check_equal(cs2::protect::qualified_name("?solve@aim@cs2@@YA?AUSolveResult@12@H@Z"),
              "cs2::aim::solve", "an MSVC name");
  check_equal(cs2::protect::qualified_name("??BWidget@aim@cs2@@QEBA_NXZ"),
              "cs2::aim::Widget::operator bool", "an MSVC conversion operator");
  check_equal(cs2::protect::qualified_name("??RWidget@aim@cs2@@QEAAHH@Z"),
              "cs2::aim::Widget::operator()", "an MSVC call operator");
  check_equal(cs2::protect::qualified_name("?anon_target@anonymous@aim@cs2@@YAHH@Z"),
              "cs2::aim::anonymous::anon_target", "an MSVC anonymous namespace");

  // The Itanium mangling (a cross build with clang).
  check_equal(cs2::protect::qualified_name("_ZN3cs23aim5solveEi"), "cs2::aim::solve",
              "an Itanium name");
  check_equal(cs2::protect::qualified_name("_ZN3cs23aim12_GLOBAL__N_111anon_targetEi"),
              "cs2::aim::(anonymous namespace)::anon_target", "an Itanium anonymous namespace");
  check_equal(cs2::protect::qualified_name("_ZN3cs23aim5solveILi3EEEii"), "cs2::aim::solve",
              "an Itanium template");
  check_equal(cs2::protect::qualified_name("_ZNK3cs23aim6WidgetcvbEv"),
              "cs2::aim::Widget::operator bool", "an Itanium conversion operator");

  // A C symbol has no qualified name.
  check(cs2::protect::qualified_name("plain_c_symbol").empty(), "a C symbol has no qualified name");
}

/// The selection -------------------------------------------------------------

/// A module with one function that cannot be protected (alwaysinline) and one
/// that can.
[[nodiscard]] auto build_module(llvm::LLVMContext& context, std::string_view identifier)
    -> std::unique_ptr<llvm::Module> {
  static constexpr std::string_view k_ir = R"(
define i32 @inlined_target(i32 %x) alwaysinline {
entry:
  ret i32 %x
}

define i32 @plain_target(i32 %x) {
entry:
  ret i32 %x
}
)";
  llvm::SMDiagnostic error;
  auto module = llvm::parseAssemblyString(k_ir, error, context);
  if (module == nullptr) {
    std::string message;
    llvm::raw_string_ostream stream(message);
    error.print("protect_tests", stream);
    std::fprintf(stderr, "FAIL: the test module does not parse: %s\n", stream.str().c_str());
    ++g_failures;
    return nullptr;
  }
  module->setModuleIdentifier(identifier);
  return module;
}

void test_selection() {
  llvm::LLVMContext context;
  const auto module = build_module(context, "selection.obj");
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.protect.push_back({"inlined_target", "", cs2::protect::ProtectionLevel::call});
  policy.protect.push_back({"plain_target", "", cs2::protect::ProtectionLevel::call});
  cs2::protect::Config config;
  config.policy = policy;

  const auto result = cs2::protect::select_functions(*module, config);
  check(result.policy_matches.size() == 2, "every entry has a state");
  check(result.selected.size() == 1, "only the protectable function is selected");

  // The entry that named an alwaysinline function protects nothing: it is
  // blocked, not matched.
  check(!result.policy_matches[0].matched, "a blocked entry did not match");
  check(result.policy_matches[0].blocked, "a blocked entry is blocked");
  check(result.policy_matches[0].blocked_reason.has_value(), "a blocked entry says why");
  if (result.policy_matches[0].blocked_reason.has_value()) {
    check_equal(cs2::protect::to_string(*result.policy_matches[0].blocked_reason), "always_inline",
                "the blocker is named");
  }

  // The entry that named a protectable function matched.
  check(result.policy_matches[1].matched, "a protectable entry matched");
  check(!result.policy_matches[1].blocked, "a matched entry is not blocked");
}

/// The coverage file ---------------------------------------------------------

/// The lines of a file.
[[nodiscard]] auto read_lines(const std::filesystem::path& path) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(line);
  }
  return lines;
}

void test_coverage() {
  llvm::LLVMContext context;
  const auto first = build_module(context, "first.obj");
  const auto second = build_module(context, "second.obj");
  if (first == nullptr || second == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.protect.push_back({"inlined_target", "", cs2::protect::ProtectionLevel::call});
  cs2::protect::Config config;
  config.policy = policy;
  config.policy_digest = "0123456789ABCDEF";
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "cs2-protect-tests-coverage";
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  config.coverage_dir = directory;

  const auto result = cs2::protect::select_functions(*first, config);
  check(!cs2::protect::write_coverage(config, *first, result).has_value(),
        "the coverage is written");
  check(!cs2::protect::write_coverage(config, *second, result).has_value(),
        "the coverage of the second target is written");

  // Two modules with different identifiers write two files.
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    files.push_back(entry.path());
  }
  check(files.size() == 2, "the two modules wrote two files");

  // The blocked entry is recorded as blocked, with the reason.
  bool found = false;
  for (const auto& path : files) {
    for (const std::string& line : read_lines(path)) {
      if (line.find("entry blocked always_inline inlined_target") != std::string::npos) {
        found = true;
      }
    }
  }
  check(found, "the coverage records the blocked entry");

  std::filesystem::remove_all(directory, error);
}

/// The transform -------------------------------------------------------------

/// The mixer of the transform, in the test.
///
/// The test decodes the descriptor with it, so a wrong index is caught even
/// when the shape of the rewritten IR is right.
constexpr std::uint64_t k_mix_multiplier = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t k_mix_xor = 0xBF58476D1CE4E5B9ULL;
constexpr unsigned k_mix_rotate = 31;

[[nodiscard]] auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value *= k_mix_multiplier;
  value ^= k_mix_xor;
  return (value << k_mix_rotate) | (value >> (64U - k_mix_rotate));
}

/// A module with a caller that calls a target, and the four calls that the
/// pass must not rewrite.
[[nodiscard]] auto build_transform_module(llvm::LLVMContext& context)
    -> std::unique_ptr<llvm::Module> {
  static constexpr std::string_view k_ir = R"(
define i32 @target(i32 %x) {
entry:
  %r = add i32 %x, 1
  ret i32 %r
}

define i32 @variadic_target(i32 %x, ...) {
entry:
  ret i32 %x
}

define i32 @musttail_target(i32 %x) {
entry:
  ret i32 %x
}

define void @sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x) {
entry:
  store i64 0, ptr %out
  ret void
}

define i32 @caller(i32 %x) {
entry:
  %out = alloca { i64, i64, i64 }, align 8
  %a = call i32 @target(i32 %x)
  %b = call i32 (i32, ...) @variadic_target(i32 %x, i32 %x)
  call void @sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x)
  %d = add i32 %a, %b
  br i1 true, label %tail, label %exit

tail:
  %r = musttail call i32 @musttail_target(i32 %x)
  ret i32 %r

exit:
  ret i32 %d
}
)";
  llvm::SMDiagnostic error;
  auto module = llvm::parseAssemblyString(k_ir, error, context);
  if (module == nullptr) {
    std::string message;
    llvm::raw_string_ostream stream(message);
    error.print("protect_tests", stream);
    std::fprintf(stderr, "FAIL: the transform module does not parse: %s\n", stream.str().c_str());
    ++g_failures;
    return nullptr;
  }
  module->setModuleIdentifier("transform.obj");
  return module;
}

/// The calls of a function, in order.
[[nodiscard]] auto calls_of(llvm::Function& function) -> std::vector<llvm::CallBase*> {
  std::vector<llvm::CallBase*> calls;
  for (llvm::BasicBlock& block : function) {
    for (llvm::Instruction& instruction : block) {
      if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
        calls.push_back(call);
      }
    }
  }
  return calls;
}

/// The value of a constant global.
[[nodiscard]] auto constant_value(llvm::Module& module, llvm::StringRef name) -> std::uint64_t {
  llvm::GlobalVariable* global = module.getGlobalVariable(name, /*AllowLocal=*/true);
  if (global == nullptr) {
    return 0;
  }
  const auto* value = llvm::dyn_cast<llvm::ConstantInt>(global->getInitializer());
  if (value == nullptr) {
    return 0;
  }
  return value->getZExtValue();
}

/// The elements of a constant array.
///
/// An array of integers is a `ConstantDataArray` and an array of pointers is
/// a `ConstantArray`: the two are not related in the IR.
[[nodiscard]] auto elements_of(llvm::Constant* constant) -> std::vector<llvm::Constant*> {
  std::vector<llvm::Constant*> elements;
  if (auto* data = llvm::dyn_cast<llvm::ConstantDataSequential>(constant)) {
    for (unsigned index = 0; index < data->getNumElements(); ++index) {
      elements.push_back(data->getElementAsConstant(index));
    }
    return elements;
  }
  if (auto* array = llvm::dyn_cast<llvm::ConstantArray>(constant)) {
    for (const llvm::Use& operand : array->operands()) {
      elements.push_back(llvm::cast<llvm::Constant>(operand.get()));
    }
  }
  return elements;
}

void test_transform() {
  llvm::LLVMContext context;
  const auto module = build_transform_module(context);
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  check(selection.selected.size() == 1, "the caller is selected");

  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  check(recorded.functions.size() == 1, "the selection is recorded");
  check(recorded.entries.size() == 1, "the policy entry is recorded");

  const auto result = cs2::protect::transform_module(*module, config, recorded);
  check(result.functions.size() == 1, "the transform reports one function");
  check(result.functions[0].rewritten == 1, "exactly one call is rewritten");
  check(result.functions[0].skipped.size() == 3, "exactly three calls are skipped");
  check(result.table_size == 1, "the table holds one callee");
  check(result.descriptor_size == 1, "the descriptor holds one slot");
  check(result.vanished.empty(), "no function vanished");

  // The call to the target is indirect now, and the other three are the
  // calls they were.
  llvm::Function* caller = module->getFunction("caller");
  check(caller != nullptr, "the caller is still there");
  if (caller == nullptr) {
    return;
  }
  const auto calls = calls_of(*caller);
  check(calls.size() == 4, "the caller still makes four calls");
  std::size_t indirect = 0;
  for (const llvm::CallBase* call : calls) {
    if (call->getCalledFunction() == nullptr) {
      ++indirect;
    }
  }
  check(indirect == 1, "exactly one call is indirect");
  check(calls.size() > 1 && calls[1]->getCalledFunction() != nullptr &&
            calls[1]->getCalledFunction()->getName() == "variadic_target",
        "the variadic call was rewritten");
  check(calls.size() > 2 && calls[2]->getCalledFunction() != nullptr &&
            calls[2]->getCalledFunction()->getName() == "sret_target",
        "the sret call was rewritten");
  check(calls.size() > 3 && calls[3]->isMustTailCall(), "the musttail call was rewritten");

  // The table holds the target.
  llvm::GlobalVariable* table = module->getGlobalVariable("cs2.protect.table", /*AllowLocal=*/true);
  check(table != nullptr, "the table global exists");
  if (table != nullptr) {
    const auto elements = elements_of(table->getInitializer());
    check(elements.size() == 1, "the table holds one entry");
    check(elements.size() == 1 && elements[0] == module->getFunction("target"),
          "the table does not hold the target");
  }

  // The descriptor holds an encrypted index, and the decode really yields
  // the index of the target.
  llvm::GlobalVariable* descriptor =
      module->getGlobalVariable("cs2.protect.desc", /*AllowLocal=*/true);
  check(descriptor != nullptr, "the descriptor global exists");
  std::uint64_t cipher = 0;
  if (descriptor != nullptr) {
    const auto elements = elements_of(descriptor->getInitializer());
    check(elements.size() == 1, "the descriptor holds one slot");
    if (elements.size() == 1) {
      const auto* value = llvm::dyn_cast<llvm::ConstantInt>(elements[0]);
      check(value != nullptr, "the descriptor holds an integer");
      if (value != nullptr) {
        cipher = value->getZExtValue();
      }
    }
  }
  check(cipher != 0, "the descriptor holds the plain index: it is not encrypted");

  const std::uint64_t key = mix64(constant_value(*module, "cs2.protect.key0") ^
                                  constant_value(*module, "cs2.protect.key1") ^
                                  constant_value(*module, "cs2.protect.key2"));
  check((cipher ^ key) == 0, "the decode does not yield the index of the target");
}

void test_level_gate() {
  llvm::LLVMContext context;
  const auto module = build_transform_module(context);
  if (module == nullptr) {
    return;
  }

  // A policy that asks for the level of T20: the function is selected, but
  // this task does not transform it, and the entry protects nothing.
  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::virtualized});
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  check(selection.selected.size() == 1, "the caller is selected");
  check(selection.selected[0].level == cs2::protect::ProtectionLevel::virtualized,
        "the level of the function is the level of the entry");
  check(!selection.policy_matches[0].matched, "the entry did not match");
  check(selection.policy_matches[0].unimplemented, "the entry is not implemented");

  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  const auto result = cs2::protect::transform_module(*module, config, recorded);
  check(result.unimplemented.size() == 1, "the function is not transformed");
  check(result.table_size == 0, "no table is created");
  check(result.descriptor_size == 0, "no descriptor is created");
  check(module->getGlobalVariable("cs2.protect.table", /*AllowLocal=*/true) == nullptr,
        "the module has no table");

  // The coverage records the entry as unimplemented, so that the
  // whole-build check fails for it.
  const auto entries = cs2::protect::coverage_entries(recorded, "transform.obj", result.vanished);
  check(entries.size() == 1, "the coverage has one entry");
  check(entries[0].state == cs2::protect::EntryState::unimplemented,
        "the coverage does not record the unimplemented level");
}

void test_vanished() {
  llvm::LLVMContext context;
  const auto module = build_transform_module(context);
  if (module == nullptr) {
    return;
  }

  // A selection that names a function that is not in the module: the
  // optimiser deleted it, and the protection would be silently lost.
  cs2::protect::SelectionResult selection;
  cs2::protect::Selection selected;
  selected.symbol = "gone";
  selected.qualified = "cs2::aim::gone";
  selected.level = cs2::protect::ProtectionLevel::call;
  selection.selected.push_back(selected);
  cs2::protect::PolicyMatch match;
  match.pattern = "cs2::aim::gone";
  match.matched = true;
  selection.policy_matches.push_back(match);

  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  check(recorded.functions.size() == 1, "the selection is recorded");

  cs2::protect::Config config;
  const auto result = cs2::protect::transform_module(*module, config, recorded);
  check(result.vanished.size() == 1, "the vanished function is reported");
  check(result.functions.empty(), "nothing was transformed");

  // The coverage records the entry as vanished, so that the whole-build
  // check fails for it.
  const auto entries = cs2::protect::coverage_entries(recorded, "transform.obj", result.vanished);
  check(entries.size() == 1, "the coverage has one entry");
  check(entries[0].state == cs2::protect::EntryState::vanished,
        "the coverage does not record the vanished function");
}

void test_level_gate_two_entries() {
  llvm::LLVMContext context;
  const auto module = build_transform_module(context);
  if (module == nullptr) {
    return;
  }

  // Two entries name the same function: one asks for the call level, the
  // other for the level of T20. The level that applies to the function is
  // the strongest one, so the call entry did not get its level either: it
  // protects nothing. The level that was *applied* is the level of the
  // function (the virtualized one), not the level of the entry -- the
  // diagnostic has to say which one, or it says that the call level (the
  // one this task implements) is not implemented.
  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::virtualized});
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  check(selection.selected.size() == 1, "the caller is selected");
  check(selection.selected[0].level == cs2::protect::ProtectionLevel::virtualized,
        "the level of the function is the strongest level of the entries");
  check(selection.policy_matches.size() == 2, "the policy has two entries");
  for (const cs2::protect::PolicyMatch& match : selection.policy_matches) {
    check(!match.matched, "an entry that did not get its level does not match");
    check(match.unimplemented, "the entry is not implemented");
    check(match.applied_level == cs2::protect::ProtectionLevel::virtualized,
          "the applied level is the level of the function, not the level of the entry");
  }
}

/// The virtualizer's analysis -------------------------------------------------

/// The analysis of a function, with a fixed seed.
///
/// The seed is fixed so that the test does not depend on the build: the
/// analysis is deterministic, and the test asserts on the shape of the
/// bytecode, not on its bytes.
[[nodiscard]] auto analyze(llvm::Function& function) -> cs2::protect::VmFunction {
  cs2::protect::VmOptions options;
  options.seed = 0x5EED5EED5EED5EEDULL;
  return cs2::protect::analyze_function(function, options);
}

/// The opcodes of a lowered function, in order.
[[nodiscard]] auto opcodes_of(const cs2::protect::VmFunction& function)
    -> std::vector<cs2::protect::VmOpcode> {
  std::vector<cs2::protect::VmOpcode> opcodes;
  opcodes.reserve(function.code.size());
  for (const cs2::protect::VmInstruction& instruction : function.code) {
    opcodes.push_back(instruction.opcode);
  }
  return opcodes;
}

/// The number of the instructions of one opcode.
[[nodiscard]] auto count_of(const std::vector<cs2::protect::VmOpcode>& opcodes,
                             cs2::protect::VmOpcode opcode) -> std::size_t {
  std::size_t count = 0;
  for (const cs2::protect::VmOpcode candidate : opcodes) {
    if (candidate == opcode) {
      ++count;
    }
  }
  return count;
}

/// A function that the virtualizer must refuse, and the reason it must give.
void check_refused(llvm::Function& function, cs2::protect::VmRefusal expected,
                   const char* what) {
  const auto result = analyze(function);
  if (result.virtualizable) {
    std::fprintf(stderr, "FAIL: %s (the function was accepted)\n", what);
    ++g_failures;
    return;
  }
  if (result.failure.reason != expected) {
    std::fprintf(stderr, "FAIL: %s (expected '%s', got '%s')\n", what,
                 cs2::protect::to_string(expected),
                 cs2::protect::to_string(result.failure.reason));
    ++g_failures;
  }
}

/// A function that the virtualizer must accept.
void check_accepted(llvm::Function& function, const char* what) {
  const auto result = analyze(function);
  if (!result.virtualizable) {
    std::fprintf(stderr, "FAIL: %s (refused: %s: %s)\n", what,
                 cs2::protect::to_string(result.failure.reason),
                 result.failure.detail.c_str());
    ++g_failures;
  }
}

/// The virtualizer's front end: a function that can be lowered.
void test_vm_virtualizable() {
  llvm::LLVMContext context;
  llvm::Module module("vm.obj", context);
  auto* function = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(context),
                               {static_cast<llvm::Type*>(llvm::Type::getInt32Ty(context))}, false),
      llvm::GlobalValue::ExternalLinkage, "add_one", &module);
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
  llvm::IRBuilder<> builder(entry);
  llvm::Value* sum = builder.CreateAdd(function->getArg(0), builder.getInt32(1));
  builder.CreateRet(sum);

  const auto result = analyze(*function);
  check(result.virtualizable, "the function is virtualizable");
  check(result.failure.detail.empty(), "a virtualizable function has no refusal");
  // The slots: the argument, the result of the addition and the constant.
  check(result.slots == 3, "the function uses three slots");
  // The instructions: the constant, the addition and the return.
  check(result.instructions == 3, "the function has three instructions");
  check(result.calls == 0, "the function makes no call");
  check(result.bytecode_bytes == cs2::protect::k_header_bytes + 3 * cs2::protect::k_instruction_bytes,
        "the container holds the header and the instruction stream");

  // The header is the contract: it names the layout of the frame.
  check(result.header.magic == cs2::protect::k_vm_magic, "the header carries the magic");
  check(result.header.version == cs2::protect::k_vm_version, "the header carries the version");
  check(result.header.slot_count == result.slots, "the header names the slots");
  check(result.header.arg_slots == cs2::protect::k_frame_argument_slots,
        "the header names the argument region");
  check(result.header.call_slots == cs2::protect::k_frame_call_slots,
        "the header names the call scratch");
  check(result.header.instruction_count == result.instructions, "the header names the instructions");
  check(result.frame_bytes == cs2::protect::k_frame_bytes, "the frame is the fixed size");

  // The container is encrypted: the magic is not in the clear.
  check(result.blob.size() == result.bytecode_bytes, "the blob is the container");
  check(result.blob[0] != static_cast<std::uint8_t>(cs2::protect::k_vm_magic & 0xFFU) ||
            result.blob[1] != static_cast<std::uint8_t>((cs2::protect::k_vm_magic >> 8U) & 0xFFU) ||
            result.blob[2] != static_cast<std::uint8_t>((cs2::protect::k_vm_magic >> 16U) & 0xFFU) ||
            result.blob[3] != static_cast<std::uint8_t>((cs2::protect::k_vm_magic >> 24U) & 0xFFU),
        "the container is not encrypted: the magic is in the clear");

  // The opcodes are permuted: the canonical number of an opcode is not the
  // byte in the container.
  const auto [perm_a, perm_b] = cs2::protect::opcode_permutation(0x5EED5EED5EED5EEDULL);
  check(perm_a % 2 == 1, "the multiplier of the permutation is odd");
  check(cs2::protect::canonical_opcode(cs2::protect::permuted_opcode(
                                             cs2::protect::VmOpcode::Add, perm_a, perm_b),
                                         perm_a, perm_b) == cs2::protect::VmOpcode::Add,
        "the permutation is invertible");
}

/// The virtualizer's front end: the parallel copy of a swapping back-edge.
void test_vm_parallel_copy() {
  llvm::LLVMContext context;
  llvm::Module module("swap.obj", context);
  auto* function = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(context),
                               {static_cast<llvm::Type*>(llvm::Type::getInt32Ty(context))}, false),
      llvm::GlobalValue::ExternalLinkage, "swap", &module);
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
  llvm::BasicBlock* loop = llvm::BasicBlock::Create(context, "loop", function);

  llvm::IRBuilder<> builder(entry);
  builder.CreateBr(loop);

  builder.SetInsertPoint(loop);
  auto* first = builder.CreatePHI(llvm::Type::getInt32Ty(context), 2, "first");
  auto* second = builder.CreatePHI(llvm::Type::getInt32Ty(context), 2, "second");
  first->addIncoming(builder.getInt32(0), entry);
  second->addIncoming(builder.getInt32(1), entry);
  llvm::Value* sum = builder.CreateAdd(first, second);
  llvm::Value* difference = builder.CreateSub(sum, second);
  // The back edge swaps the two values: a sequential copy would break it.
  first->addIncoming(second, loop);
  second->addIncoming(first, loop);
  builder.CreateBr(loop);

  const auto result = analyze(*function);
  check(result.virtualizable, "the function with the swapping back-edge is virtualizable");
  const auto opcodes = opcodes_of(result);
  // Two edges reach the loop, and each of them copies two values: four
  // pushes and four pops.
  check(count_of(opcodes, cs2::protect::VmOpcode::Arg) == 4,
        "the parallel copy pushes both sources on both edges");
  check(count_of(opcodes, cs2::protect::VmOpcode::Pop) == 4,
        "the parallel copy pops both destinations on both edges");
  // The copy of the back edge is the last one: the first of its pushes is
  // the last push that is not preceded by another push.
  std::size_t last_push = 0;
  std::size_t first_push = 0;
  bool found = false;
  for (std::size_t index = 0; index < opcodes.size(); ++index) {
    if (opcodes[index] != cs2::protect::VmOpcode::Arg) {
      continue;
    }
    if (index > 0 && opcodes[index - 1] == cs2::protect::VmOpcode::Arg) {
      continue;
    }
    if (!found) {
      first_push = index;
      found = true;
    }
    last_push = index;
  }
  check(found, "the copy pushes the sources");
  if (!found || last_push + 3 >= opcodes.size() || first_push + 3 >= opcodes.size()) {
    return;
  }
  // The pushes come first, and the pops follow, in the reverse order.
  check(opcodes[last_push] == cs2::protect::VmOpcode::Arg &&
            opcodes[last_push + 1] == cs2::protect::VmOpcode::Arg &&
            opcodes[last_push + 2] == cs2::protect::VmOpcode::Pop &&
            opcodes[last_push + 3] == cs2::protect::VmOpcode::Pop,
        "the copy pushes every source and then pops every destination");

  // The first copy is the edge from the entry: its destinations are the
  // slots of the two phis, and the pops are in the reverse order (the last
  // pop fills the first phi).
  const auto first_phi = result.code[first_push + 3].dst;
  const auto second_phi = result.code[first_push + 2].dst;
  check(first_phi != second_phi, "the two phis have two slots");

  // The copy of the back edge swaps the two values: the first phi takes
  // the value of the second one, and the destinations are popped in the
  // reverse order, so the second phi is filled first.
  check(result.code[last_push].src1 == second_phi,
        "the first push takes the value of the second phi");
  check(result.code[last_push + 1].src1 == first_phi,
        "the second push takes the value of the first phi");
  check(result.code[last_push + 2].dst == second_phi, "the first pop fills the second phi");
  check(result.code[last_push + 3].dst == first_phi, "the second pop fills the first phi");
}

/// The virtualizer's front end: the flags of the arithmetic.
void test_vm_flags() {
  llvm::LLVMContext context;
  llvm::Module module("flags.obj", context);
  auto* function = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(context),
                               {static_cast<llvm::Type*>(llvm::Type::getInt32Ty(context))}, false),
      llvm::GlobalValue::ExternalLinkage, "flags", &module);
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
  llvm::IRBuilder<> builder(entry);
  llvm::Value* wrapped = builder.CreateNSWAdd(function->getArg(0), builder.getInt32(1));
  llvm::Value* bounded = builder.CreateNUWMul(wrapped, builder.getInt32(3));
  builder.CreateRet(bounded);

  const auto result = analyze(*function);
  check(result.virtualizable, "the function with the flags is virtualizable");
  bool saw_nsw = false;
  bool saw_nuw = false;
  for (const cs2::protect::VmInstruction& instruction : result.code) {
    if (instruction.opcode != cs2::protect::VmOpcode::Add &&
        instruction.opcode != cs2::protect::VmOpcode::Mul) {
      continue;
    }
    if ((instruction.flags & cs2::protect::k_flag_nsw) != 0) {
      saw_nsw = true;
    }
    if ((instruction.flags & cs2::protect::k_flag_nuw) != 0) {
      saw_nuw = true;
    }
  }
  check(saw_nsw, "the nsw flag is carried into the bytecode");
  check(saw_nuw, "the nuw flag is carried into the bytecode");
}

/// The virtualizer's front end: an `undef` that is read twice.
void test_vm_freeze() {
  llvm::LLVMContext context;
  llvm::Module module("undef.obj", context);
  auto* function = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(context),
                               {static_cast<llvm::Type*>(llvm::Type::getInt32Ty(context))}, false),
      llvm::GlobalValue::ExternalLinkage, "undef", &module);
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
  llvm::IRBuilder<> builder(entry);
  auto* undef = llvm::UndefValue::get(llvm::Type::getInt32Ty(context));
  llvm::Value* first = builder.CreateAdd(undef, function->getArg(0));
  llvm::Value* second = builder.CreateAdd(undef, function->getArg(0));
  builder.CreateRet(builder.CreateAdd(first, second));

  const auto result = analyze(*function);
  check(result.virtualizable, "the function with the undef is virtualizable");
  // The undef is frozen: it is materialised once, and the two reads use
  // the same slot.
  std::vector<std::uint16_t> sources;
  for (const cs2::protect::VmInstruction& instruction : result.code) {
    if (instruction.opcode != cs2::protect::VmOpcode::Add) {
      continue;
    }
    sources.push_back(instruction.src1);
  }
  check(sources.size() == 3, "the function has three additions");
  check(sources.size() >= 2 && sources[0] == sources[1],
        "the two reads of the undef use the same slot: it is frozen");
  check(count_of(opcodes_of(result), cs2::protect::VmOpcode::Const) == 1,
        "the undef is materialised once");
}

/// The virtualizer's front end: the refusals.
void test_vm_refusals() {
  llvm::LLVMContext context;
  llvm::Module module("refusals.obj", context);
  llvm::Type* const i32 = llvm::Type::getInt32Ty(context);
  llvm::Type* const void_type = llvm::Type::getVoidTy(context);

  // A variadic function.
  {
    auto* function = llvm::Function::Create(
        llvm::FunctionType::get(i32, {i32}, true), llvm::GlobalValue::ExternalLinkage,
        "variadic_function", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    builder.CreateRet(function->getArg(0));
    check_refused(*function, cs2::protect::VmRefusal::variadic_function,
                  "a variadic function is refused");
  }

  // A call to a variadic function.
  {
    auto* callee = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, true),
                                          llvm::GlobalValue::ExternalLinkage, "printf", &module);
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "caller",
                                             &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    builder.CreateRet(builder.CreateCall(callee, {function->getArg(0)}));
    check_refused(*function, cs2::protect::VmRefusal::variadic_callee,
                  "a call to a variadic function is refused");
  }

  // An indirect call.
  {
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "indirect",
                                             &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* pointer = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(context));
    builder.CreateRet(builder.CreateCall(function->getFunctionType(), pointer,
                                               llvm::ArrayRef<llvm::Value*>{function->getArg(0)}));
    check_refused(*function, cs2::protect::VmRefusal::indirect_call,
                  "an indirect call is refused");
  }

  // A float.
  {
    auto* function = llvm::Function::Create(
        llvm::FunctionType::get(i32, {llvm::Type::getFloatTy(context)}, false),
        llvm::GlobalValue::ExternalLinkage, "float", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    builder.CreateRet(builder.getInt32(0));
    check_refused(*function, cs2::protect::VmRefusal::unsupported_type,
                  "a float is refused");
  }

  // A `memcpy` whose length is not a constant.
  {
    auto* function = llvm::Function::Create(llvm::FunctionType::get(void_type, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "dynamic",
                                             &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* buffer = builder.CreateAlloca(llvm::Type::getInt8Ty(context), nullptr, "buffer");
    builder.CreateMemCpy(buffer, llvm::MaybeAlign(), buffer, llvm::MaybeAlign(),
                         function->getArg(0));
    builder.CreateRetVoid();
    check_refused(*function, cs2::protect::VmRefusal::dynamic_memcpy,
                  "a memcpy with a dynamic length is refused");
  }

  // The address of the function is taken.
  {
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "taken",
                                             &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* slot = builder.CreateAlloca(llvm::PointerType::getUnqual(context), nullptr, "slot");
    builder.CreateStore(function, slot);
    builder.CreateRet(function->getArg(0));
    check_refused(*function, cs2::protect::VmRefusal::address_taken,
                  "a function whose address is taken is refused");
  }

  // More slots than the budget.
  {
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "wide",
                                             &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    llvm::Value* value = function->getArg(0);
    for (int index = 0; index < 300; ++index) {
      value = builder.CreateAdd(value, builder.getInt32(1));
    }
    builder.CreateRet(value);
    check_refused(*function, cs2::protect::VmRefusal::too_many_slots,
                  "a function with too many slots is refused");
  }

  // An EH edge.
  {
    auto* personality = llvm::Function::Create(
        llvm::FunctionType::get(i32, {llvm::PointerType::getUnqual(context)}, false),
        llvm::GlobalValue::ExternalLinkage, "__gxx_personality_v0", &module);
    auto* throwing = llvm::Function::Create(
        llvm::FunctionType::get(i32, {i32}, false), llvm::GlobalValue::ExternalLinkage, "throwing",
        &module);
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "eh", &module);
    function->setPersonalityFn(personality);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::BasicBlock* normal = llvm::BasicBlock::Create(context, "normal", function);
    llvm::BasicBlock* unwind = llvm::BasicBlock::Create(context, "unwind", function);
    llvm::IRBuilder<> builder(entry);
    llvm::Value* result =
        builder.CreateInvoke(throwing, normal, unwind, function->getArg(0));
    builder.SetInsertPoint(normal);
    builder.CreateRet(result);
    builder.SetInsertPoint(unwind);
    builder.CreateLandingPad(llvm::StructType::get(context, llvm::ArrayRef<llvm::Type*>{i32}), 0);
    builder.CreateResume(builder.getInt32(0));
    check_refused(*function, cs2::protect::VmRefusal::eh_pad, "an EH edge is refused");
  }

  // A volatile access.
  {
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "volatile",
                                             &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* slot = builder.CreateAlloca(i32, nullptr, "slot");
    builder.CreateStore(function->getArg(0), slot);
    auto* loaded = builder.CreateLoad(i32, slot);
    loaded->setVolatile(true);
    builder.CreateRet(loaded);
    check_refused(*function, cs2::protect::VmRefusal::volatile_access,
                  "a volatile access is refused");
  }

  // A recursion.
  {
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "recursive",
                                             &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    builder.CreateRet(builder.CreateCall(function, {function->getArg(0)}));
    check_refused(*function, cs2::protect::VmRefusal::recursion, "a recursion is refused");
  }

  // A callee that is itself selected at the virtualized level.
  {
    auto* callee = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                          llvm::GlobalValue::ExternalLinkage, "virtualized",
                                          &module);
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "caller",
                                             &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    builder.CreateRet(builder.CreateCall(callee, {function->getArg(0)}));
    cs2::protect::VmOptions options;
    options.seed = 1;
    const std::vector<std::string> virtualized{"virtualized"};
    options.virtualized_symbols = virtualized;
    const auto result = cs2::protect::analyze_function(*function, options);
    check(!result.virtualizable, "a call to a virtualized function is refused");
    check(result.failure.reason == cs2::protect::VmRefusal::virtualized_callee,
          "the refusal of a call to a virtualized function names the reason");
  }

  // An instruction the lowering does not model.
  {
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                             llvm::GlobalValue::ExternalLinkage, "unsupported",
                                             &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    // A `ctpop`: the ISA has no opcode for it.
    builder.CreateRet(builder.CreateCall(
        llvm::Intrinsic::getOrInsertDeclaration(&module, llvm::Intrinsic::ctpop, i32,
                                                          llvm::ArrayRef<llvm::Type*>{i32}),
        {function->getArg(0)}));
    check_refused(*function, cs2::protect::VmRefusal::unsupported_instruction,
                  "an instruction the lowering does not model is refused");
  }
}

/// The virtualizer's front end: a call.
void test_vm_call() {
  llvm::LLVMContext context;
  llvm::Module module("call.obj", context);
  llvm::Type* const i32 = llvm::Type::getInt32Ty(context);
  auto* callee = llvm::Function::Create(llvm::FunctionType::get(i32, {i32, i32}, false),
                                        llvm::GlobalValue::ExternalLinkage, "add_two", &module);
  llvm::BasicBlock* callee_entry = llvm::BasicBlock::Create(context, "entry", callee);
  {
    llvm::IRBuilder<> builder(callee_entry);
    builder.CreateRet(builder.CreateAdd(callee->getArg(0), callee->getArg(1)));
  }
  auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                           llvm::GlobalValue::ExternalLinkage, "caller", &module);
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
  llvm::IRBuilder<> builder(entry);
  builder.CreateRet(builder.CreateCall(callee, {function->getArg(0), builder.getInt32(2)}));

  const auto result = analyze(*function);
  check(result.virtualizable, "the function with the call is virtualizable");
  check(result.calls == 1, "the function has one call site");
  check(result.call_sites.size() == 1, "the call site is recorded");
  if (result.call_sites.size() == 1) {
    const auto& site = result.call_sites.front();
    check_equal(site.callee, "add_two", "the call site names the callee");
    check(site.argument_types.size() == 2, "the call site names the two arguments");
    check_equal(site.return_type, "i32", "the call site names the type of the return value");
    check(site.pool_index < result.pool.size(), "the call site has an entry in the pool");
  }
  const auto opcodes = opcodes_of(result);
  check(count_of(opcodes, cs2::protect::VmOpcode::Arg) == 2, "the arguments are pushed");
  check(count_of(opcodes, cs2::protect::VmOpcode::Call) == 1, "the call is one instruction");
}

}  // namespace

auto main() -> int {
  test_policy_reader();
  test_qualified_name();
  test_selection();
  test_coverage();
  test_transform();
  test_level_gate();
  test_level_gate_two_entries();
  test_vanished();
  test_vm_virtualizable();
  test_vm_parallel_copy();
  test_vm_flags();
  test_vm_freeze();
  test_vm_refusals();
  test_vm_call();
  if (g_failures != 0) {
    std::printf("protect: %d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("protect: ok");
  return 0;
}
