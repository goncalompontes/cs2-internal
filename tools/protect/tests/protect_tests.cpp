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
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include "config.hpp"
#include "report.hpp"
#include "selection.hpp"
#include "strings.hpp"
#include "transform.hpp"

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

/// The string transform --------------------------------------------------------

/// A module with one literal per refusal reason.
///
/// Every literal has its own user, so that the verdict of the gate is
/// unambiguous: the gate reports the first reason that holds, and a literal
/// with two reasons would depend on the order of the use list.
[[nodiscard]] auto build_string_module(llvm::LLVMContext& context)
    -> std::unique_ptr<llvm::Module> {
  static constexpr std::string_view k_ir = R"(
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"

@.str.plain = private unnamed_addr constant [8 x i8] c"healthy\00"
@.str.short = private unnamed_addr constant [4 x i8] c"abc\00"
@.str.empty = private unnamed_addr constant [1 x i8] c"\00"
@.str.shape = private unnamed_addr constant [2 x i32] [i32 7, i32 9]
@.str.escapes = private unnamed_addr constant [7 x i8] c"escape\00"
@.str.optnone = private unnamed_addr constant [9 x i8] c"optnones\00"
@.str.virtual = private unnamed_addr constant [8 x i8] c"virtual\00"
@.str.eh = private unnamed_addr constant [5 x i8] c"ehxy\00"
@.str.compare = private unnamed_addr constant [9 x i8] c"compared\00"
@.str.twoblock = private unnamed_addr constant [9 x i8] c"twoblock\00"

@global.init = private constant { ptr, i64 } { ptr @.str.escapes, i64 7 }

declare void @may_throw()
declare i32 @__gxx_personality_v0(...)

define void @uses_plain(ptr %out) {
entry:
  %c = load i8, ptr @.str.plain
  store i8 %c, ptr %out
  ret void
}

define void @uses_short(ptr %out) {
entry:
  %c = load i8, ptr @.str.short
  store i8 %c, ptr %out
  ret void
}

define void @uses_empty(ptr %out) {
entry:
  %c = load i8, ptr @.str.empty
  store i8 %c, ptr %out
  ret void
}

define void @uses_shape(ptr %out) {
entry:
  %c = load i32, ptr @.str.shape
  store i32 %c, ptr %out
  ret void
}

define void @uses_optnone(ptr %out) noinline optnone {
entry:
  %c = load i8, ptr @.str.optnone
  store i8 %c, ptr %out
  ret void
}

define void @uses_virtual(ptr %out) {
entry:
  %c = load i8, ptr @.str.virtual
  store i8 %c, ptr %out
  ret void
}

define void @uses_eh(ptr %out) personality ptr @__gxx_personality_v0 {
entry:
  %c = load i8, ptr @.str.eh
  store i8 %c, ptr %out
  invoke void @may_throw() to label %cont unwind label %lpad

cont:
  ret void

lpad:
  %l = landingpad { ptr, i32 } cleanup
  ret void
}

define i1 @compares_addresses() {
entry:
  %c = icmp eq ptr @.str.compare, null
  ret i1 %c
}

define void @uses_two_blocks(ptr %out, i1 %cond) {
entry:
  %c = load i8, ptr @.str.twoblock
  store i8 %c, ptr %out
  br i1 %cond, label %then, label %else

then:
  %d = load i8, ptr @.str.twoblock
  store i8 %d, ptr %out
  ret void

else:
  ret void
}
)";
  llvm::SMDiagnostic error;
  auto module = llvm::parseAssemblyString(k_ir, error, context);
  if (module == nullptr) {
    std::string message;
    llvm::raw_string_ostream stream(message);
    error.print("protect_tests", stream);
    std::fprintf(stderr, "FAIL: the string module does not parse: %s\n", stream.str().c_str());
    ++g_failures;
    return nullptr;
  }
  module->setModuleIdentifier("strings.obj");
  module->setSourceFileName("strings.obj");
  return module;
}

/// The policy's selection of the strings.
void test_string_policy() {
  cs2::protect::Policy policy;
  policy.strings = {"healthy", "cs2_*"};
  check(cs2::protect::string_selected(policy, "healthy"),
        "an exact glob selects the literal");
  check(cs2::protect::string_selected(policy, "cs2_esp"),
        "a wildcard glob selects the literal");
  check(!cs2::protect::string_selected(policy, "other"),
        "a literal that matches nothing is not selected");

  policy.strings_deny = {"cs2_*"};
  check(!cs2::protect::string_selected(policy, "cs2_esp"),
        "the deny glob wins over the select glob");
  check(cs2::protect::string_selected(policy, "healthy"),
        "the deny glob does not touch the other literals");

  // The policy reader: the new section, and its default.
  auto parsed = cs2::protect::parse_policy_text(
      R"({"strings": ["box", "cs2_*"], "strings_deny": ["CS2_*"], "strings_min_length": 3})");
  check(parsed.has_value(), "a policy with a strings section parses");
  if (parsed.has_value()) {
    check(parsed->strings.size() == 2, "the policy has two strings");
    check(parsed->strings_deny.size() == 1, "the policy has one denied string");
    check(parsed->strings_min_length == 3, "the minimum length is read");
  }

  auto defaults = cs2::protect::parse_policy_text(R"({"protect": []})");
  check(defaults.has_value(), "a policy without a strings section parses");
  if (defaults.has_value()) {
    check(defaults->strings_min_length == 4, "the default minimum length is four");
    check(defaults->strings.empty(), "no string is selected by default");
  }

  // A malformed section is an error, not a warning.
  auto malformed = cs2::protect::parse_policy_text(R"({"strings": "box"})");
  check(!malformed.has_value(), "a strings section that is not an array is rejected");
  auto negative = cs2::protect::parse_policy_text(R"({"strings_min_length": -1})");
  check(!negative.has_value(), "a negative minimum length is rejected");
}

/// The gate: one fixture per refusal reason.
void test_string_gate() {
  llvm::LLVMContext context;
  const auto module = build_string_module(context);
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.strings_min_length = 4;
  cs2::protect::Config config;
  config.policy = policy;

  // The function that is selected at the `virtualized` level: the VM's blob
  // format and the string blob format must not meet.
  cs2::protect::RecordedSelection selection;
  selection.functions.push_back(
      {"uses_virtual", "", cs2::protect::ProtectionLevel::virtualized});

  const auto verdict = [&](const char* name) -> std::optional<cs2::protect::StringSkipReason> {
    llvm::GlobalVariable* global = module->getGlobalVariable(name, /*AllowLocal=*/true);
    if (global == nullptr) {
      std::fprintf(stderr, "FAIL: the fixture has no global '%s'\n", name);
      ++g_failures;
      return std::nullopt;
    }
    return cs2::protect::string_gate(*global, config, selection);
  };

  check(!verdict(".str.plain").has_value(), "a plain literal passes the gate");

  const auto check_reason = [&](const char* name, cs2::protect::StringSkipReason expected,
                                 const char* what) {
    const auto actual = verdict(name);
    if (actual.has_value() && *actual == expected) {
      return;
    }
    std::fprintf(stderr, "FAIL: %s (expected '%s', got '%s')\n", what,
                 cs2::protect::to_string(expected),
                 actual.has_value() ? cs2::protect::to_string(*actual) : "none");
    ++g_failures;
  };

  check_reason(".str.escapes", cs2::protect::StringSkipReason::escapes_to_initializer,
               "a literal in a global initializer is refused");
  check_reason(".str.shape", cs2::protect::StringSkipReason::unsupported_shape,
               "a literal that is not a sequence of bytes is refused");
  check_reason(".str.empty", cs2::protect::StringSkipReason::empty,
               "an empty literal is refused");
  check_reason(".str.short", cs2::protect::StringSkipReason::too_short,
               "a literal that is shorter than the minimum is refused");
  check_reason(".str.optnone", cs2::protect::StringSkipReason::optnone_function,
               "a literal used by an optnone function is refused");
  check_reason(".str.virtual", cs2::protect::StringSkipReason::virtualized_function,
               "a literal used by a virtualized function is refused");
  check_reason(".str.eh", cs2::protect::StringSkipReason::eh_use,
               "a literal used by a function with an EH pad is refused");
  check_reason(".str.compare", cs2::protect::StringSkipReason::unsupported_use,
               "a literal whose address is compared is refused");
}

/// The transform: the literal leaves the image, the blob and the decode are
/// there, and the coverage does not carry the content.
void test_string_transform() {
  llvm::LLVMContext context;
  const auto module = build_string_module(context);
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.strings = {"healthy"};
  policy.strings_min_length = 4;
  cs2::protect::Config config;
  config.policy = policy;
  config.policy_digest = "0123456789ABCDEF";
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "cs2-protect-tests-strings";
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  config.coverage_dir = directory;

  const cs2::protect::StringResult result =
      cs2::protect::transform_strings(*module, config, {});
  check(result.protected_count == 1, "one literal is protected");
  check(result.refused_count == 0, "no literal is refused");
  check(result.decodes == 1, "one decode is emitted");
  check(result.plaintext_bytes == 7, "seven bytes of plaintext left the image");

  // The plaintext is gone: the literal does not exist any more.
  check(module->getGlobalVariable(".str.plain", /*AllowLocal=*/true) == nullptr,
        "the plaintext literal is deleted");
  // The blob and the decode are there.
  check(module->getGlobalVariable("cs2.protect.strblob.0", /*AllowLocal=*/true) != nullptr,
        "the blob is added");
  check(module->getFunction("cs2.protect.strdecode") != nullptr, "the decode helper is added");
  // The blob is not the plaintext.
  llvm::GlobalVariable* blob = module->getGlobalVariable("cs2.protect.strblob.0", true);
  if (blob != nullptr) {
    const auto* data = llvm::dyn_cast<llvm::ConstantDataSequential>(blob->getInitializer());
    check(data != nullptr, "the blob is a sequence of bytes");
    if (data != nullptr) {
      bool differs = false;
      for (unsigned index = 0; index < data->getNumElements(); ++index) {
        const auto* value = llvm::cast<llvm::ConstantInt>(data->getElementAsConstant(index));
        if (value->getValue().getZExtValue() != static_cast<std::uint64_t>("healthy\0"[index])) {
          differs = true;
        }
      }
      check(differs, "the blob does not hold the plaintext");
    }
  }

  // The using function has the buffer and the decode call.
  llvm::Function* user = module->getFunction("uses_plain");
  check(user != nullptr, "the using function is there");
  if (user != nullptr) {
    std::size_t allocas = 0;
    std::size_t calls = 0;
    for (llvm::BasicBlock& block : *user) {
      for (llvm::Instruction& instruction : block) {
        if (llvm::isa<llvm::AllocaInst>(&instruction)) {
          ++allocas;
        }
        if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
          if (call->getCalledFunction() != nullptr &&
              call->getCalledFunction()->getName() == "cs2.protect.strdecode") {
            ++calls;
          }
        }
      }
    }
    check(allocas == 1, "the using function has one buffer");
    check(calls == 1, "the using function has one decode call");
  }

  // The coverage carries the length and the digest, never the content.
  const std::vector<cs2::protect::CoverageString> covered =
      cs2::protect::coverage_strings(result);
  check(covered.size() == 1, "the coverage has one literal");
  if (!covered.empty()) {
    check(covered[0].length == 7, "the coverage records the length");
    check(covered[0].digest.size() == 64, "the coverage records a digest");
    check(covered[0].digest != "healthy", "the coverage does not carry the content");
  }
  check(!cs2::protect::write_coverage(config, *module, {}, {}, {}, covered).has_value(),
        "the coverage is written");
  for (const auto& path : std::filesystem::directory_iterator(directory)) {
    for (const std::string& line : read_lines(path)) {
      check(line.find("healthy") == std::string::npos,
            "the coverage file does not carry the plaintext");
    }
  }

  std::filesystem::remove_all(directory, error);
}

/// The transform: the decode runs before **every** use of the literal.
///
/// A literal that is used in two blocks of one function -- one of them the
/// entry block -- is the case that breaks a naive placement: a decode that
/// is emitted after a use of the entry block leaves that use reading an
/// uninitialised stack slot, which is a *wrong string* and not a crash.
void test_string_decode_dominance() {
  llvm::LLVMContext context;
  const auto module = build_string_module(context);
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.strings = {"twoblock"};
  policy.strings_min_length = 4;
  cs2::protect::Config config;
  config.policy = policy;

  const cs2::protect::StringResult result = cs2::protect::transform_strings(*module, config, {});
  check(result.protected_count == 1, "the literal of the two-block function is protected");
  check(result.decodes == 1, "one decode is emitted");

  llvm::Function* function = module->getFunction("uses_two_blocks");
  check(function != nullptr, "the two-block function is there");
  if (function == nullptr) {
    return;
  }

  // The buffer, the decode and the uses of the buffer.
  llvm::Value* buffer = nullptr;
  llvm::Instruction* decode = nullptr;
  for (llvm::BasicBlock& block : *function) {
    for (llvm::Instruction& instruction : block) {
      if (instruction.getName() == "cs2.protect.buffer") {
        buffer = &instruction;
      }
      if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
        if (call->getCalledFunction() != nullptr &&
            call->getCalledFunction()->getName() == "cs2.protect.strdecode") {
          decode = &instruction;
        }
      }
    }
  }
  check(buffer != nullptr, "the function has a buffer");
  check(decode != nullptr, "the function has a decode");
  if (buffer == nullptr || decode == nullptr) {
    return;
  }

  std::vector<llvm::Instruction*> uses;
  for (llvm::User* user : buffer->users()) {
    auto* instruction = llvm::dyn_cast<llvm::Instruction>(user);
    if (instruction == nullptr || instruction == decode) {
      continue;
    }
    uses.push_back(instruction);
  }
  check(uses.size() == 2, "the function has two uses of the buffer");

  // The decode dominates every use, and it is before the uses of its own
  // block.
  llvm::DominatorTree tree(*function);
  for (const llvm::Instruction* use : uses) {
    if (use->getParent() == decode->getParent()) {
      check(decode->comesBefore(use), "the decode is before the use of its own block");
      continue;
    }
    check(tree.dominates(decode->getParent(), use->getParent()),
          "the decode dominates the use of the other block");
  }
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
  test_string_policy();
  test_string_gate();
  test_string_transform();
  test_string_decode_dominance();
  if (g_failures != 0) {
    std::printf("protect: %d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("protect: ok");
  return 0;
}
