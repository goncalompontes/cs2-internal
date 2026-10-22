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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include "cipher.hpp"
#include "config.hpp"
#include "layout.hpp"
#include "report.hpp"
#include "selection.hpp"
#include "strings.hpp"
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

/// The mandatory seed ---------------------------------------------------------

/// Sets an environment variable, for the tests.
///
/// The plugin reads its configuration from the environment, and the test has
/// to set it: `setenv` is the POSIX spelling and `_putenv_s` the Windows
/// one.
void set_env(const char* name, const char* value) {
#if defined(_WIN32)
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

/// The seed is mandatory: the plugin refuses to run without one.
///
/// The fallback is not a default, it is a **constant of the payload**: two
/// builds without a seed are byte for byte the same file. The failure has to
/// be loud, and the message has to name both sources.
void test_mandatory_seed() {
  set_env("CS2_PROTECT_SEED", "");
  set_env("CS2_PROTECT_CONFIG", "");
  auto without = cs2::protect::load_config();
  check(!without.has_value(), "the configuration without a seed is refused");
  if (!without.has_value()) {
    const std::string& message = without.error().message;
    check(message.find("CS2_PROTECT_SEED") != std::string::npos,
          "the message names the environment variable");
    check(message.find("\"seed\"") != std::string::npos, "the message names the policy's seed");
  }

  // A policy that carries a seed: the configuration is accepted.
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "cs2-protect-tests-seed";
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  std::filesystem::create_directories(directory);
  const std::filesystem::path policy = directory / "policy.json";
  {
    std::ofstream out(policy);
    out << R"({"protect": [], "seed": "0x0123456789ABCDEF"})";
  }
  set_env("CS2_PROTECT_CONFIG", policy.string().c_str());
  auto with_policy = cs2::protect::load_config();
  check(with_policy.has_value(), "the policy's seed is enough");
  if (with_policy.has_value()) {
    const auto seed = cs2::protect::build_seed(*with_policy);
    check(seed.has_value() && *seed == 0x0123456789ABCDEFULL,
          "the policy's seed is the seed of the build");
  }

  // The environment wins over the policy: the environment is what the
  // release process sets.
  set_env("CS2_PROTECT_SEED", "0xFEDCBA9876543210");
  auto with_both = cs2::protect::load_config();
  check(with_both.has_value(), "the configuration with two seeds is accepted");
  if (with_both.has_value()) {
    const auto seed = cs2::protect::build_seed(*with_both);
    check(seed.has_value() && *seed == 0xFEDCBA9876543210ULL,
          "the environment wins over the policy");
  }

  // The environment is cleared again: the other tests read the
  // configuration by hand.
  set_env("CS2_PROTECT_SEED", "");
  set_env("CS2_PROTECT_CONFIG", "");
  std::filesystem::remove_all(directory, error);
}

/// The mixer ------------------------------------------------------------------

/// The mixer is per build.
///
/// The multiplier, the xor constant and the rotation come from the module's
/// seed: two builds with two seeds hold two different mixers, and the
/// protection leaves no fixed byte in the image.
void test_per_build_mixer() {
  const cs2::protect::Mixer first = cs2::protect::mixer_for(0x1111111111111111ULL);
  const cs2::protect::Mixer second = cs2::protect::mixer_for(0x2222222222222222ULL);
  check(first.multiplier != second.multiplier, "two seeds give two different multipliers");
  check(first.xor_constant != second.xor_constant, "two seeds give two different xor constants");
  check(first.rotate != second.rotate, "two seeds give two different rotations");
  check(cs2::protect::mixer_for(0x5EED5EED5EED5EEDULL).multiplier ==
            cs2::protect::mixer_for(0x5EED5EED5EED5EEDULL).multiplier,
        "the same seed gives the same mixer");

  // The properties the mixer has to have for every seed: the rotation is in
  // [1, 63] (a rotation of 0 would make the `lshr` by 64 of the decode
  // undefined behaviour), the multiplier is odd and the xor constant is not
  // zero.
  for (std::uint64_t seed = 0; seed < 1000; ++seed) {
    const cs2::protect::Mixer mixer = cs2::protect::mixer_for(seed);
    check(mixer.rotate >= 1 && mixer.rotate <= 63, "the rotation is in [1, 63]");
    check(mixer.multiplier % 2 == 1, "the multiplier is odd");
    check(mixer.xor_constant != 0, "the xor constant is not zero");
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

/// A module built from a piece of IR.
[[nodiscard]] auto build_module_from(llvm::LLVMContext& context, std::string_view ir,
                                     std::string_view identifier) -> std::unique_ptr<llvm::Module> {
  llvm::SMDiagnostic error;
  auto module = llvm::parseAssemblyString(ir, error, context);
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
  return build_module_from(context, k_ir, identifier);
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
/// The mixer is **per build**: it comes from the module's seed. The test
/// derives it the way the plugin does, so that a mixer that drifts is caught
/// even when the shape of the rewritten IR is right.
[[nodiscard]] auto mixer_of(const cs2::protect::Config& config, const llvm::Module& module)
    -> cs2::protect::Mixer {
  return cs2::protect::mixer_for(cs2::protect::module_seed(config, module.getSourceFileName()));
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

  // The mixer of the build: the globals hold its values, and the rotation
  // is the one the decode emits.
  const cs2::protect::Mixer mixer = mixer_of(config, *module);
  check(mixer.rotate >= 1 && mixer.rotate <= 63, "the rotation is in [1, 63]");
  check(constant_value(*module, "cs2.protect.mixmul") == mixer.multiplier,
        "the global holds the multiplier of the build");
  check(constant_value(*module, "cs2.protect.mixxor") == mixer.xor_constant,
        "the global holds the xor constant of the build");

  // The decode rotates by the rotation of the build: the `shl` and the
  // `lshr` of the caller carry it.
  llvm::Function* caller_function = module->getFunction("caller");
  check(caller_function != nullptr, "the caller is still there");
  if (caller_function != nullptr) {
    bool saw_shift = false;
    bool saw_opposite = false;
    for (llvm::BasicBlock& block : *caller_function) {
      for (llvm::Instruction& instruction : block) {
        const auto* shift = llvm::dyn_cast<llvm::BinaryOperator>(&instruction);
        if (shift == nullptr) {
          continue;
        }
        const auto* amount = llvm::dyn_cast<llvm::ConstantInt>(shift->getOperand(1));
        if (amount == nullptr) {
          continue;
        }
        const auto value = amount->getZExtValue();
        if (shift->getOpcode() == llvm::Instruction::Shl && value == mixer.rotate) {
          saw_shift = true;
        }
        if (shift->getOpcode() == llvm::Instruction::LShr && value == 64U - mixer.rotate) {
          saw_opposite = true;
        }
      }
    }
    check(saw_shift, "the decode shifts by the rotation of the build");
    check(saw_opposite, "the decode shifts by 64 minus the rotation of the build");
  }

  const std::uint64_t key = mixer.mix64(constant_value(*module, "cs2.protect.key0") ^
                                        constant_value(*module, "cs2.protect.key1") ^
                                        constant_value(*module, "cs2.protect.key2"));
  check((cipher ^ key) == 0, "the decode does not yield the index of the target");
}

/// The transform refuses a callee with an `sret` parameter on a target the
/// narrowing was not measured on.
///
/// The rule is **target bound**: the evidence is the measurement of one
/// backend on one target, and a rule that is not measured is not used. The
/// refusal names the attribute.
void test_transform_sret_other_target() {
  llvm::LLVMContext context;
  const auto module = build_transform_module(context);
  if (module == nullptr) {
    return;
  }
  // The module of the test has no target triple at all: the rule is closed
  // there, and it is closed on every target but the one that was measured.
  module->setTargetTriple(llvm::Triple("x86_64-unknown-linux-gnu"));

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  const auto result = cs2::protect::transform_module(*module, config, recorded);

  check(result.functions.size() == 1, "the transform reports one function");
  check(result.functions[0].rewritten == 1, "only the call to the target is rewritten");
  check(result.functions[0].skipped.size() == 3, "three calls are skipped");
  bool named = false;
  for (const cs2::protect::CallSkipReason reason : result.functions[0].skipped) {
    if (reason == cs2::protect::CallSkipReason::sret_parameter) {
      named = true;
    }
  }
  check(named, "the refusal names the sret parameter");
  check_equal(cs2::protect::to_string(cs2::protect::CallSkipReason::sret_parameter),
              "sret_parameter", "the reason of the refusal names the attribute");
}

/// The transform routes a callee that takes an `sret` parameter.
///
/// The call goes through a **thunk**: the thunk carries the `sret` attribute
/// of the call and the hidden pointer, which it takes out of the cell of the
/// scratch that the `sret` attribute names -- the first one here, the callee
/// being a free function.
void test_transform_sret() {
  llvm::LLVMContext context;
  const auto module = build_transform_module(context);
  if (module == nullptr) {
    return;
  }
  // The target the narrowing was measured on. The triple of a build carries
  // the version of the toolchain, and the rule has to be open there too.
  module->setTargetTriple(llvm::Triple("x86_64-pc-windows-msvc"));
  check(cs2::protect::sret_routable(*module), "the sret parameter is routable on this target");
  module->setTargetTriple(llvm::Triple("x86_64-pc-windows-msvc19.51.36252"));
  check(cs2::protect::sret_routable(*module),
        "the sret parameter is routable on the triple of a build");
  module->setTargetTriple(llvm::Triple("x86_64-pc-windows-msvc"));

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  const auto result = cs2::protect::transform_module(*module, config, recorded);

  check(result.functions.size() == 1, "the transform reports one function");
  check(result.functions[0].rewritten == 2, "the target and the sret callee are rewritten");
  check(result.functions[0].skipped.size() == 2, "the variadic and the musttail calls are skipped");
  check(result.table_size == 2, "the table holds the two callees");
  check(result.descriptor_size == 2, "the descriptor holds two slots");

  // The thunk: the uniform signature, the hidden pointer out of the first
  // cell of the scratch, and the call with the `sret` attribute.
  llvm::Function* thunk = nullptr;
  for (llvm::Function& function : *module) {
    if (function.getName().starts_with("cs2.protect.thunk.")) {
      thunk = &function;
    }
  }
  check(thunk != nullptr, "the thunk of the sret call is emitted");
  if (thunk == nullptr) {
    return;
  }
  check(thunk->getReturnType()->isIntegerTy(64), "the thunk returns a 64-bit value");
  check(thunk->arg_size() == 1, "the thunk takes one argument: the scratch");

  const auto thunk_calls = calls_of(*thunk);
  check(thunk_calls.size() == 1, "the thunk makes one call");
  if (thunk_calls.size() == 1) {
    const llvm::CallBase* call = thunk_calls[0];
    check(call->getCalledFunction() == nullptr, "the call of the thunk is indirect");
    check(call->getParamAttr(0, llvm::Attribute::StructRet).isValid(),
          "the call of the thunk carries the sret attribute");
    // The hidden pointer: the cell of the scratch that the `sret` attribute
    // names -- the first one here -- unchanged.
    const auto* load = llvm::dyn_cast<llvm::LoadInst>(call->getArgOperand(0));
    check(load != nullptr, "the hidden pointer is loaded out of the scratch");
    if (load != nullptr) {
      check(load->getPointerOperand() == thunk->getArg(0),
            "the hidden pointer is the cell of the scratch that the sret attribute names");
    }
  }

  // The callee returns nothing: the thunk returns 0.
  bool returns_zero = false;
  for (const llvm::BasicBlock& block : *thunk) {
    const auto* ret = llvm::dyn_cast<llvm::ReturnInst>(block.getTerminator());
    if (ret == nullptr || ret->getReturnValue() == nullptr) {
      continue;
    }
    const auto* value = llvm::dyn_cast<llvm::ConstantInt>(ret->getReturnValue());
    if (value != nullptr && value->isZero()) {
      returns_zero = true;
    }
  }
  check(returns_zero, "the thunk returns zero");

  // The caller: the sret call goes through the thunk, and the scratch is in
  // the caller's frame.
  llvm::Function* caller = module->getFunction("caller");
  check(caller != nullptr, "the caller is still there");
  if (caller == nullptr) {
    return;
  }
  const auto calls = calls_of(*caller);
  check(calls.size() == 4, "the caller still makes four calls");
  check(calls.size() > 2 && calls[2]->getCalledFunction() == thunk,
        "the sret call goes through the thunk");
  bool scratch_in_caller = false;
  for (llvm::BasicBlock& block : *caller) {
    for (llvm::Instruction& instruction : block) {
      const auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(&instruction);
      if (alloca != nullptr && alloca->getAllocatedType()->isStructTy()) {
        scratch_in_caller = true;
      }
    }
  }
  check(scratch_in_caller, "the scratch is in the caller's frame");

  // The table holds the callee of the sret call: the thunk reaches it
  // through the caller's own table.
  llvm::GlobalVariable* table = module->getGlobalVariable("cs2.protect.table",
                                                          /*AllowLocal=*/true);
  check(table != nullptr, "the table global exists");
  if (table != nullptr) {
    const auto elements = elements_of(table->getInitializer());
    check(elements.size() == 2, "the table holds two entries");
    bool holds_sret = false;
    for (const llvm::Constant* element : elements) {
      if (element == module->getFunction("sret_target")) {
        holds_sret = true;
      }
    }
    check(holds_sret, "the table does not hold the sret callee");
  }
}

/// The transform routes a callee whose hidden pointer is **not** the first
/// parameter.
///
/// A member function takes the `this` pointer first, and the hidden pointer
/// after it: the attribute is what says which parameter it is.
void test_transform_sret_member() {
  llvm::LLVMContext context;
  llvm::SMDiagnostic error;
  auto module = llvm::parseAssemblyString(
      "define void @member_target(ptr %this, ptr sret({ i64, i64, i64 }) %out) {\n"
      "entry:\n"
      "  store { i64, i64, i64 } zeroinitializer, ptr %out\n"
      "  ret void\n"
      "}\n"
      "define void @caller(ptr %out) {\n"
      "entry:\n"
      "  call void @member_target(ptr null, ptr sret({ i64, i64, i64 }) %out)\n"
      "  ret void\n"
      "}\n",
      error, context);
  if (module == nullptr) {
    std::fprintf(stderr, "FAIL: the member module does not parse\n");
    ++g_failures;
    return;
  }
  module->setModuleIdentifier("member.obj");
  module->setTargetTriple(llvm::Triple("x86_64-pc-windows-msvc"));

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  const auto result = cs2::protect::transform_module(*module, config, recorded);

  check(result.functions.size() == 1, "the transform reports one function");
  check(result.functions[0].rewritten == 1, "the call is rewritten");
  check(result.functions[0].skipped.empty(), "no call is skipped");

  // The thunk: the hidden pointer is the **second** parameter, and the
  // scratch holds it in the second cell.
  llvm::Function* thunk = nullptr;
  for (llvm::Function& function : *module) {
    if (function.getName().starts_with("cs2.protect.thunk.")) {
      thunk = &function;
    }
  }
  check(thunk != nullptr, "the thunk of the sret call is emitted");
  if (thunk == nullptr) {
    return;
  }
  const auto calls = calls_of(*thunk);
  check(calls.size() == 1, "the thunk makes one call");
  if (calls.size() != 1) {
    return;
  }
  const llvm::CallBase* call = calls[0];
  check(call->getCalledFunction() == nullptr, "the call of the thunk is indirect");
  check(call->getParamAttr(1, llvm::Attribute::StructRet).isValid(),
        "the call of the thunk carries the sret attribute on the second parameter");
  check(!call->getParamAttr(0, llvm::Attribute::StructRet).isValid(),
        "the call of the thunk carries the sret attribute on the first parameter");
  // The hidden pointer: the second cell of the scratch.
  const auto* load = llvm::dyn_cast<llvm::LoadInst>(call->getArgOperand(1));
  check(load != nullptr, "the hidden pointer is loaded out of the scratch");
  if (load != nullptr) {
    const auto* address = llvm::dyn_cast<llvm::GEPOperator>(load->getPointerOperand());
    check(address != nullptr, "the hidden pointer is not the first cell of the scratch");
    if (address != nullptr) {
      check(
          address->getNumIndices() == 2 &&
              address->getOperand(2) == llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 1),
          "the hidden pointer is not the second cell of the scratch");
    }
  }
}

/// A module with a struct-returning call **inside a catch handler**.
///
/// The handler is a funclet in the MSVC EH model: it is a separate
/// machine-level function, and every call in it carries a `"funclet"(token
/// ...)` operand bundle. The bundle is what tells LLVM's Windows-EH codegen
/// that the call lives in a funclet.
[[nodiscard]] auto build_functlet_module(llvm::LLVMContext& context)
    -> std::unique_ptr<llvm::Module> {
  static constexpr std::string_view k_ir = R"(
declare i32 @__gxx_personality_v0(...)
declare void @may_throw()
declare void @external_sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x)

define i32 @caller(i32 %x) personality ptr @__gxx_personality_v0 {
entry:
  %out = alloca { i64, i64, i64 }, align 8
  invoke void @may_throw() to label %cont unwind label %catch.dispatch

catch.dispatch:
  %cs = catchswitch within none [label %handler] unwind to caller

handler:
  %cp = catchpad within %cs [ptr null, i32 64, ptr null]
  call void @external_sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x) [ "funclet"(token %cp) ]
  catchret from %cp to label %cont

cont:
  ret i32 0
}
)";
  return build_module_from(context, k_ir, "funclet.obj");
}

/// The replacement call keeps the **operand bundles** of the call it
/// replaces.
///
/// A catch handler is a funclet, and the calls in it carry a `"funclet"`
/// bundle. Without the bundle the Windows-EH codegen mis-lowers the funclet:
/// it drops the body after the prologue, never emits the parent's
/// continuation, and the image traps. The **ordinary** path does not need
/// this -- it mutates the call it rewrites, so the call keeps its bundles --
/// but the thunk path builds a *new* call, and only the thunk path can lose
/// them.
void test_transform_sret_functlet() {
  llvm::LLVMContext context;
  const auto module = build_functlet_module(context);
  if (module == nullptr) {
    return;
  }
  // The target the narrowing was measured on: the rule is closed on every
  // other one, and the call would not be rewritten at all.
  module->setTargetTriple(llvm::Triple("x86_64-pc-windows-msvc"));

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  policy.cross_module.push_back("external_sret_target");
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  check(selection.selected.size() == 1, "the caller is selected");
  check(selection.cross_matches.size() == 1, "the cross_module entry named the declaration");
  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  const auto result = cs2::protect::transform_module(*module, config, recorded);

  check(result.functions.size() == 1, "the transform reports one function");
  check(result.functions[0].rewritten == 1, "the call in the funclet is rewritten");
  check(result.functions[0].skipped.size() == 1, "the invoke of the import is skipped");

  llvm::Function* caller = module->getFunction("caller");
  check(caller != nullptr, "the caller is still there");
  if (caller == nullptr) {
    return;
  }

  // The catchpad of the handler: it is the token of the bundle.
  llvm::Value* catchpad = nullptr;
  for (llvm::BasicBlock& block : *caller) {
    for (llvm::Instruction& instruction : block) {
      if (auto* pad = llvm::dyn_cast<llvm::CatchPadInst>(&instruction)) {
        catchpad = pad;
      }
    }
  }
  check(catchpad != nullptr, "the catchpad of the handler is still there");

  // The call in the handler goes through the thunk, and the replacement call
  // keeps the bundle of the call it replaces.
  bool saw_thunk = false;
  for (const llvm::CallBase* call : calls_of(*caller)) {
    const llvm::Function* callee = call->getCalledFunction();
    if (callee == nullptr || !callee->getName().starts_with("cs2.protect.thunk.")) {
      continue;
    }
    saw_thunk = true;
    const auto bundle = call->getOperandBundle("funclet");
    check(bundle.has_value(),
          "the replacement call lost the funclet bundle of the call it replaces: the backend "
          "drops the body of the funclet and the image traps");
    if (bundle.has_value()) {
      check(bundle->isFuncletOperandBundle(), "the bundle of the replacement call is the funclet");
      check(bundle->Inputs.size() == 1, "the funclet bundle carries one operand: the token");
      check(catchpad != nullptr && bundle->Inputs[0].get() == catchpad,
            "the token of the bundle is the catchpad of the handler");
    }
  }
  check(saw_thunk, "the call in the funclet does not go through the thunk");
}

/// The transform still refuses the parameters an indirect call cannot carry.
///
/// The refusal **names** the attribute: a refusal that says "byval" is
/// actionable, a refusal that says "indirect_parameter" is not.
void test_transform_parameters() {
  struct Case {
    const char* ir;
    cs2::protect::CallSkipReason reason;
    const char* name;
  };
  const std::vector<Case> cases = {
      {"declare void @callee(ptr byval({ i64, i64 }) %out)\n"
       "define void @caller(ptr %out) {\n"
       "entry:\n"
       "  call void @callee(ptr byval({ i64, i64 }) %out)\n"
       "  ret void\n"
       "}\n",
       cs2::protect::CallSkipReason::byval_parameter, "byval_parameter"},
      {"declare void @callee(ptr inalloca({ i64, i64 }) %out)\n"
       "define void @caller(ptr %out) {\n"
       "entry:\n"
       "  call void @callee(ptr inalloca({ i64, i64 }) %out)\n"
       "  ret void\n"
       "}\n",
       cs2::protect::CallSkipReason::inalloca_parameter, "inalloca_parameter"},
      {"declare void @callee(ptr preallocated({ i64, i64 }) %out)\n"
       "define void @caller(ptr %out) {\n"
       "entry:\n"
       "  call void @callee(ptr preallocated({ i64, i64 }) %out)\n"
       "  ret void\n"
       "}\n",
       cs2::protect::CallSkipReason::preallocated_parameter, "preallocated_parameter"},
  };

  for (const Case& item : cases) {
    llvm::LLVMContext context;
    llvm::SMDiagnostic error;
    auto module = llvm::parseAssemblyString(item.ir, error, context);
    if (module == nullptr) {
      std::fprintf(stderr, "FAIL: the module of the case does not parse\n");
      ++g_failures;
      continue;
    }
    module->setModuleIdentifier("parameters.obj");
    module->setTargetTriple(llvm::Triple("x86_64-pc-windows-msvc"));

    cs2::protect::Policy policy;
    policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
    cs2::protect::Config config;
    config.policy = policy;

    const auto selection = cs2::protect::select_functions(*module, config);
    cs2::protect::record_selection(*module, selection);
    const auto recorded = cs2::protect::read_selection(*module);
    const auto result = cs2::protect::transform_module(*module, config, recorded);

    check(result.functions.size() == 1, "the transform reports one function");
    check(result.functions[0].rewritten == 0, "the call is not rewritten");
    check(result.functions[0].skipped.size() == 1, "the call is skipped");
    if (result.functions[0].skipped.size() == 1) {
      check(result.functions[0].skipped[0] == item.reason,
            "the refusal does not name the attribute");
      check_equal(cs2::protect::to_string(item.reason), item.name,
                  "the reason of the refusal names the attribute");
    }
  }
}

/// The transform refuses a callee that takes an `sret` parameter **and**
/// returns a value: that is not a thing in the x64 ABI.
void test_transform_sret_result() {
  llvm::LLVMContext context;
  llvm::Module module("sret-result.obj", context);
  module.setTargetTriple(llvm::Triple("x86_64-pc-windows-msvc"));

  auto* result_type = llvm::StructType::get(
      context, {llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context)});
  auto* callee = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(context),
                              {llvm::PointerType::getUnqual(context)}, false),
      llvm::GlobalValue::ExternalLinkage, "callee", &module);
  callee->addParamAttr(0, llvm::Attribute::getWithStructRetType(context, result_type));
  auto* caller = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                              {llvm::PointerType::getUnqual(context)}, false),
      llvm::GlobalValue::ExternalLinkage, "caller", &module);
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", caller);
  llvm::IRBuilder<> builder(entry);
  llvm::CallInst* call = builder.CreateCall(callee, {caller->getArg(0)});
  call->addParamAttr(0, llvm::Attribute::getWithStructRetType(context, result_type));
  builder.CreateRet(llvm::ConstantInt::get(llvm::Type::getVoidTy(context), 0));

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(module, config);
  cs2::protect::record_selection(module, selection);
  const auto recorded = cs2::protect::read_selection(module);
  const auto result = cs2::protect::transform_module(module, config, recorded);

  check(result.functions.size() == 1, "the transform reports one function");
  check(result.functions[0].rewritten == 0, "the call is not rewritten");
  check(result.functions[0].skipped.size() == 1, "the call is skipped");
  if (result.functions[0].skipped.size() == 1) {
    check(result.functions[0].skipped[0] == cs2::protect::CallSkipReason::sret_result,
          "the refusal is not the one of a callee that returns a value");
  }
}

/// The layout -----------------------------------------------------------------

/// The permutation of the module's function list is seeded.
///
/// The layout is the lever that moves the code in the image: the object
/// writer emits the functions in the module's function list order, and the
/// linker keeps the order of an object's sections.
void test_layout_permutation() {
  const std::vector<std::size_t> first = cs2::protect::layout_permutation(16, 0x1234ULL);
  const std::vector<std::size_t> again = cs2::protect::layout_permutation(16, 0x1234ULL);
  const std::vector<std::size_t> other = cs2::protect::layout_permutation(16, 0x5678ULL);
  check(first == again, "the same seed gives the same permutation");
  check(first != other, "two seeds give two different permutations");

  // It is a permutation: every index appears exactly once, so no function is
  // lost and none is duplicated.
  for (const std::vector<std::size_t>* permutation : {&first, &other}) {
    std::vector<bool> seen(16, false);
    for (const std::size_t index : *permutation) {
      check(index < 16, "the index is inside the range");
      if (index < 16) {
        check(!seen[index], "the index appears once");
        seen[index] = true;
      }
    }
  }

  // The permutation of a module moves the functions and keeps every name.
  llvm::LLVMContext context;
  const auto module = build_transform_module(context);
  if (module == nullptr) {
    return;
  }
  std::vector<std::string> before;
  for (llvm::Function& function : *module) {
    before.push_back(function.getName().str());
  }
  const cs2::protect::LayoutResult result = cs2::protect::permute_layout(*module, 0xABCDULL);
  check(result.functions == 5, "the module has five definitions");
  check(result.moved > 0, "the permutation moved a function");

  std::vector<std::string> after;
  for (llvm::Function& function : *module) {
    after.push_back(function.getName().str());
  }
  check(after.size() == before.size(), "the module has the same number of functions");
  check(after != before, "the function list is in another order");

  std::vector<std::string> sorted_before = before;
  std::vector<std::string> sorted_after = after;
  std::ranges::sort(sorted_before);
  std::ranges::sort(sorted_after);
  check(sorted_after == sorted_before, "the permutation lost or duplicated a function");

  // The module is whole: a broken module is a miscompile.
  std::string message;
  llvm::raw_string_ostream stream(message);
  check(!llvm::verifyModule(*module, &stream), "the permuted module verifies");
  if (llvm::verifyModule(*module, &stream)) {
    std::fprintf(stderr, "FAIL: the permuted module does not verify: %s\n", stream.str().c_str());
  }
  check(module->getFunction("target") != nullptr, "the module still knows its functions");
  check(module->getFunction("caller") != nullptr, "the module still knows its caller");
}

/// The level gate -------------------------------------------------------------

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

/// The cross-module routing -------------------------------------------------

/// A module with a caller that calls a **declaration**.
///
/// The declaration has no body in this module: at the IR level it is the
/// same thing as an import, and the policy's `cross_module` list is what
/// tells the two apart.
[[nodiscard]] auto build_cross_module(llvm::LLVMContext& context) -> std::unique_ptr<llvm::Module> {
  static constexpr std::string_view k_ir = R"(
declare i32 @external_target(i32 %x)

define i32 @caller(i32 %x) {
entry:
  %a = call i32 @external_target(i32 %x)
  %b = add i32 %a, 1
  ret i32 %b
}
)";
  return build_module_from(context, k_ir, "cross.obj");
}

/// A module with a caller that calls a **variadic** declaration.
///
/// The indirect call cannot reproduce the variadic ABI, so the call stays
/// direct even though the policy names the declaration.
[[nodiscard]] auto build_variadic_cross_module(llvm::LLVMContext& context)
    -> std::unique_ptr<llvm::Module> {
  static constexpr std::string_view k_ir = R"(
declare i32 @variadic_target(i32 %x, ...)

define i32 @caller(i32 %x) {
entry:
  %a = call i32 (i32, ...) @variadic_target(i32 %x, i32 %x)
  %b = add i32 %a, 1
  ret i32 %b
}
)";
  return build_module_from(context, k_ir, "variadic-cross.obj");
}

/// A module with a caller that calls a **declaration** through an EH edge.
///
/// The call is an `invoke`: the call may throw, and the landing pad catches
/// it. The rewrite replaces the callee and leaves the EH edges alone.
[[nodiscard]] auto build_invoke_cross_module(llvm::LLVMContext& context)
    -> std::unique_ptr<llvm::Module> {
  static constexpr std::string_view k_ir = R"(
declare i32 @__gxx_personality_v0(...)
declare i32 @external_target(i32 %x)

define i32 @caller(i32 %x) personality ptr @__gxx_personality_v0 {
entry:
  %a = invoke i32 @external_target(i32 %x) to label %cont unwind label %lpad

cont:
  ret i32 %a

lpad:
  %e = landingpad { ptr, i32 }
          cleanup
  ret i32 0
}
)";
  return build_module_from(context, k_ir, "invoke-cross.obj");
}

/// The policy reader accepts a `cross_module` list, and rejects a malformed
/// one as a *reported* error.
void test_cross_module_policy() {
  auto parsed = cs2::protect::parse_policy_text(
      R"({"protect": ["caller"], "cross_module": ["cs2::aim::DamageField::*"]})");
  check(parsed.has_value(), "a policy with a cross_module list parses");
  if (parsed.has_value()) {
    check(parsed->cross_module.size() == 1, "the policy has one cross_module entry");
    if (!parsed->cross_module.empty()) {
      check_equal(parsed->cross_module[0], "cs2::aim::DamageField::*",
                  "the entry keeps its pattern");
    }
  }

  // A list that is not a list.
  auto not_a_list = cs2::protect::parse_policy_text(R"({"cross_module": "cs2::aim::solve"})");
  check(!not_a_list.has_value(), "a cross_module that is not an array is rejected");
  if (!not_a_list.has_value()) {
    check_equal(not_a_list.error().message, "'cross_module' must be an array",
                "the error names the shape");
  }

  // An entry that is not a string.
  auto not_a_string = cs2::protect::parse_policy_text(R"({"cross_module": [42]})");
  check(!not_a_string.has_value(), "a cross_module entry that is not a string is rejected");
  if (!not_a_string.has_value()) {
    check_equal(not_a_string.error().message, "a 'cross_module' entry must be a string",
                "the error names the entry");
  }
}

/// The transform routes a declaration the policy names.
void test_transform_cross_module() {
  llvm::LLVMContext context;
  const auto module = build_cross_module(context);
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  policy.cross_module.push_back("external_target");
  cs2::protect::Config config;
  config.policy = policy;

  // The selection phase matches the list against the declarations of the
  // module, and the match is handed over to the transform phase.
  const auto selection = cs2::protect::select_functions(*module, config);
  check(selection.selected.size() == 1, "the caller is selected");
  check(selection.cross_matches.size() == 1, "the cross_module entry named the declaration");
  if (selection.cross_matches.empty()) {
    return;
  }
  check_equal(selection.cross_matches[0].symbol, "external_target",
              "the match names the declaration");

  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  check(recorded.cross.size() == 1, "the cross_module match is recorded");
  check(!recorded.cross.empty() && recorded.cross[0].symbol == "external_target",
        "the recorded match names the declaration");

  const auto result = cs2::protect::transform_module(*module, config, recorded);
  check(result.functions.size() == 1, "the transform reports one function");
  check(result.functions[0].rewritten == 1, "the call to the declaration is rewritten");
  check(result.functions[0].cross == 1, "the rewrite is counted as a cross-TU one");
  check(result.functions[0].skipped.empty(), "no call is skipped");
  check(result.table_size == 1, "the table holds one callee");
  check(result.descriptor_size == 1, "the descriptor holds one slot");

  // The call is indirect now.
  llvm::Function* caller = module->getFunction("caller");
  check(caller != nullptr, "the caller is still there");
  if (caller == nullptr) {
    return;
  }
  const auto calls = calls_of(*caller);
  check(calls.size() == 1, "the caller still makes one call");
  check(calls.size() == 1 && calls[0]->getCalledFunction() == nullptr,
        "the call to the declaration is indirect");

  // The table holds the *declaration*: the entry is the symbol, and the
  // object writer turns it into a relocatable pointer to the external
  // symbol.
  llvm::GlobalVariable* table = module->getGlobalVariable("cs2.protect.table",
                                                          /*AllowLocal=*/true);
  check(table != nullptr, "the table global exists");
  if (table != nullptr) {
    const auto elements = elements_of(table->getInitializer());
    check(elements.size() == 1, "the table holds one entry");
    check(elements.size() == 1 && elements[0] == module->getFunction("external_target"),
          "the table does not hold the declaration");
    // The entry is the symbol itself, not a copy of its address: a copy
    // would bake the image base into the image.
    check(elements.size() == 1 && llvm::isa<llvm::Function>(elements[0]),
          "the table entry is the function, not an address");
  }

  // The descriptor decodes to the index of the entry.
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
  const cs2::protect::Mixer mixer = mixer_of(config, *module);
  const std::uint64_t key = mixer.mix64(constant_value(*module, "cs2.protect.key0") ^
                                        constant_value(*module, "cs2.protect.key1") ^
                                        constant_value(*module, "cs2.protect.key2"));
  check((cipher ^ key) == 0, "the decode does not yield the index of the declaration");

  // The state of the entry: it named a declaration and every call to it was
  // routed.
  check(result.cross.entries.size() == 1, "the state of the entry is recorded");
  if (!result.cross.entries.empty()) {
    check(result.cross.entries[0].state == cs2::protect::CrossState::matched,
          "the entry is matched");
    check_equal(result.cross.entries[0].symbol, "external_target",
                "the state names the declaration");
  }
  check(result.cross.direct.empty(), "no cross-TU callee stayed direct");
}

/// The transform refuses a declaration the policy does not name.
void test_transform_cross_module_not_named() {
  llvm::LLVMContext context;
  const auto module = build_cross_module(context);
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  check(selection.cross_matches.empty(), "no declaration is named");

  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  const auto result = cs2::protect::transform_module(*module, config, recorded);
  check(result.functions[0].rewritten == 0, "the call is not rewritten");
  check(result.functions[0].skipped.size() == 1, "the call is skipped");
  check(result.functions[0].skipped.size() == 1 &&
            result.functions[0].skipped[0] == cs2::protect::CallSkipReason::external_callee,
        "the reason is external_callee");
  check(result.table_size == 0, "the module keeps no table");

  // The callee stays direct, and the coverage names it: the payload check
  // compares it with the symbols the payload's own libraries define.
  check(result.cross.direct.size() == 1, "the direct callee is recorded");
  if (!result.cross.direct.empty()) {
    check_equal(result.cross.direct[0].symbol, "external_target",
                "the direct callee is the declaration");
    check_equal(result.cross.direct[0].reason, "external_callee",
                "the direct callee names its reason");
  }
}

/// A `cross_module` entry that named a declaration whose call could not be
/// routed is *blocked*, and the state says why.
void test_transform_cross_module_blocked() {
  llvm::LLVMContext context;
  const auto module = build_variadic_cross_module(context);
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  policy.cross_module.push_back("variadic_target");
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  check(selection.cross_matches.size() == 1, "the cross_module entry named the declaration");
  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  const auto result = cs2::protect::transform_module(*module, config, recorded);
  check(result.functions[0].rewritten == 0, "the call is not rewritten");
  check(result.cross.entries.size() == 1, "the state of the entry is recorded");
  if (!result.cross.entries.empty()) {
    check(result.cross.entries[0].state == cs2::protect::CrossState::blocked,
          "the entry is blocked");
    check_equal(result.cross.entries[0].reason, "variadic_callee", "the state names the reason");
  }
  check(result.cross.direct.size() == 1, "the direct callee is recorded");
}

/// A cross-TU callee that is called through an EH edge is routed too: the
/// landing pad and the unwind edge stay, and only the callee becomes a
/// value.
void test_transform_cross_module_invoke() {
  llvm::LLVMContext context;
  const auto module = build_invoke_cross_module(context);
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.protect.push_back({"caller", "", cs2::protect::ProtectionLevel::call});
  policy.cross_module.push_back("external_target");
  cs2::protect::Config config;
  config.policy = policy;

  const auto selection = cs2::protect::select_functions(*module, config);
  check(selection.cross_matches.size() == 1, "the cross_module entry named the declaration");
  cs2::protect::record_selection(*module, selection);
  const auto recorded = cs2::protect::read_selection(*module);
  const auto result = cs2::protect::transform_module(*module, config, recorded);
  check(result.functions[0].rewritten == 1, "the invoke is rewritten");
  check(result.functions[0].cross == 1, "the rewrite is counted as a cross-TU one");
  check(result.functions[0].skipped.empty(), "no call is skipped");

  llvm::Function* caller = module->getFunction("caller");
  check(caller != nullptr, "the caller is still there");
  if (caller == nullptr) {
    return;
  }
  const auto calls = calls_of(*caller);
  check(calls.size() == 1, "the caller still makes one call");
  check(calls.size() == 1 && calls[0]->getCalledFunction() == nullptr, "the invoke is indirect");
  check(calls.size() == 1 && llvm::isa<llvm::InvokeInst>(calls[0]),
        "the call is still an invoke: the EH edge is not modelled away");
  check(caller->getPersonalityFn() != nullptr, "the personality function is still there");
  std::size_t blocks = 0;
  for (const llvm::BasicBlock& block : *caller) {
    (void)block;
    ++blocks;
  }
  check(blocks == 3, "the landing pad is still there");
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
void check_refused(llvm::Function& function, cs2::protect::VmRefusal expected, const char* what) {
  const auto result = analyze(function);
  if (result.virtualizable) {
    std::fprintf(stderr, "FAIL: %s (the function was accepted)\n", what);
    ++g_failures;
    return;
  }
  if (result.failure.reason != expected) {
    std::fprintf(stderr, "FAIL: %s (expected '%s', got '%s')\n", what,
                 cs2::protect::to_string(expected), cs2::protect::to_string(result.failure.reason));
    ++g_failures;
  }
}

/// A function that the virtualizer must accept.
void check_accepted(llvm::Function& function, const char* what) {
  const auto result = analyze(function);
  if (!result.virtualizable) {
    std::fprintf(stderr, "FAIL: %s (refused: %s: %s)\n", what,
                 cs2::protect::to_string(result.failure.reason), result.failure.detail.c_str());
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
  check_accepted(*function, "the function with an addition is virtualizable");
  check(result.failure.detail.empty(), "a virtualizable function has no refusal");
  // The slots: the argument, the result of the addition and the constant.
  check(result.slots == 3, "the function uses three slots");
  // The instructions: the constant, the addition and the return.
  check(result.instructions == 3, "the function has three instructions");
  check(result.calls == 0, "the function makes no call");
  check(
      result.bytecode_bytes == cs2::protect::k_header_bytes + 3 * cs2::protect::k_instruction_bytes,
      "the container holds the header and the instruction stream");

  // The header is the contract: it names the layout of the frame.
  check(result.header.magic == cs2::protect::k_vm_magic, "the header carries the magic");
  check(result.header.version == cs2::protect::k_vm_version, "the header carries the version");
  check(result.header.slot_count == result.slots, "the header names the slots");
  check(result.header.arg_slots == cs2::protect::k_frame_argument_slots,
        "the header names the argument region");
  check(result.header.call_slots == cs2::protect::k_frame_call_slots,
        "the header names the call scratch");
  check(result.header.instruction_count == result.instructions,
        "the header names the instructions");
  check(result.frame_bytes == cs2::protect::k_frame_bytes, "the frame is the fixed size");

  // The container is encrypted: the magic is not in the clear.
  check(result.blob.size() == result.bytecode_bytes, "the blob is the container");
  check(
      result.blob[0] != static_cast<std::uint8_t>(cs2::protect::k_vm_magic & 0xFFU) ||
          result.blob[1] != static_cast<std::uint8_t>((cs2::protect::k_vm_magic >> 8U) & 0xFFU) ||
          result.blob[2] != static_cast<std::uint8_t>((cs2::protect::k_vm_magic >> 16U) & 0xFFU) ||
          result.blob[3] != static_cast<std::uint8_t>((cs2::protect::k_vm_magic >> 24U) & 0xFFU),
      "the container is not encrypted: the magic is in the clear");

  // The opcodes are permuted: the canonical number of an opcode is not the
  // byte in the container.
  //
  // The multiplier is odd **and** coprime with the number of the opcodes,
  // for every seed: the contract says so, and one seed is not a proof.
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    const auto [perm_a, perm_b] = cs2::protect::opcode_permutation(seed);
    check(perm_a % 2 == 1, "the multiplier of the permutation is odd");
    check(cs2::protect::canonical_opcode(
              cs2::protect::permuted_opcode(cs2::protect::VmOpcode::Add, perm_a, perm_b), perm_a,
              perm_b) == cs2::protect::VmOpcode::Add,
          "the permutation is invertible");
  }

  // The width of every instruction is one of 1, 2, 4 or 8: a width of 0
  // would make an interpreter that truncates its result to the width return
  // zero from every function.
  for (const cs2::protect::VmInstruction& instruction : result.code) {
    check(instruction.width == 1 || instruction.width == 2 || instruction.width == 4 ||
              instruction.width == 8,
          "the width of an instruction is one of 1, 2, 4 or 8");
  }
  // The `Ret` carries the width of the value it returns: the type of the
  // `ret` itself is `void`.
  check(result.code.back().opcode == cs2::protect::VmOpcode::Ret,
        "the last instruction of the function is the return");
  check(result.code.back().width == 4, "the return of an i32 function has the width 4");
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
    auto* function =
        llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, true),
                               llvm::GlobalValue::ExternalLinkage, "variadic_function", &module);
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
                                            llvm::GlobalValue::ExternalLinkage, "caller", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    builder.CreateRet(builder.CreateCall(callee, {function->getArg(0)}));
    check_refused(*function, cs2::protect::VmRefusal::variadic_callee,
                  "a call to a variadic function is refused");
  }

  // An indirect call.
  {
    auto* function =
        llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                               llvm::GlobalValue::ExternalLinkage, "indirect", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* pointer = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(context));
    builder.CreateRet(builder.CreateCall(function->getFunctionType(), pointer,
                                         llvm::ArrayRef<llvm::Value*>{function->getArg(0)}));
    check_refused(*function, cs2::protect::VmRefusal::indirect_call, "an indirect call is refused");
  }

  // A float.
  {
    auto* function = llvm::Function::Create(
        llvm::FunctionType::get(i32, {llvm::Type::getFloatTy(context)}, false),
        llvm::GlobalValue::ExternalLinkage, "float", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    builder.CreateRet(builder.getInt32(0));
    check_refused(*function, cs2::protect::VmRefusal::unsupported_type, "a float is refused");
  }

  // A `memcpy` whose length is not a constant.
  {
    auto* function = llvm::Function::Create(llvm::FunctionType::get(void_type, {i32}, false),
                                            llvm::GlobalValue::ExternalLinkage, "dynamic", &module);
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
                                            llvm::GlobalValue::ExternalLinkage, "taken", &module);
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
                                            llvm::GlobalValue::ExternalLinkage, "wide", &module);
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
    auto* throwing =
        llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                               llvm::GlobalValue::ExternalLinkage, "throwing", &module);
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                            llvm::GlobalValue::ExternalLinkage, "eh", &module);
    function->setPersonalityFn(personality);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::BasicBlock* normal = llvm::BasicBlock::Create(context, "normal", function);
    llvm::BasicBlock* unwind = llvm::BasicBlock::Create(context, "unwind", function);
    llvm::IRBuilder<> builder(entry);
    llvm::Value* result = builder.CreateInvoke(throwing, normal, unwind, function->getArg(0));
    builder.SetInsertPoint(normal);
    builder.CreateRet(result);
    builder.SetInsertPoint(unwind);
    builder.CreateLandingPad(llvm::StructType::get(context, llvm::ArrayRef<llvm::Type*>{i32}), 0);
    builder.CreateResume(builder.getInt32(0));
    check_refused(*function, cs2::protect::VmRefusal::eh_pad, "an EH edge is refused");
  }

  // A volatile access.
  {
    auto* function =
        llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                               llvm::GlobalValue::ExternalLinkage, "volatile", &module);
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
    auto* function =
        llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                               llvm::GlobalValue::ExternalLinkage, "recursive", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    builder.CreateRet(builder.CreateCall(function, {function->getArg(0)}));
    check_refused(*function, cs2::protect::VmRefusal::recursion, "a recursion is refused");
  }

  // A callee that is itself selected at the virtualized level.
  {
    auto* callee =
        llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                               llvm::GlobalValue::ExternalLinkage, "virtualized", &module);
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                            llvm::GlobalValue::ExternalLinkage, "caller", &module);
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
    auto* function =
        llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                               llvm::GlobalValue::ExternalLinkage, "unsupported", &module);
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

  // A local variable whose size is not constant: the frame is a constant.
  {
    auto* function =
        llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                               llvm::GlobalValue::ExternalLinkage, "dynamic_local", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    builder.CreateAlloca(i32, function->getArg(0), "slot");
    builder.CreateRet(function->getArg(0));
    check_refused(*function, cs2::protect::VmRefusal::local_variable,
                  "a local with a variable size is refused");
  }

  // Locals that do not fit in the frame: the value region of the frame
  // holds 256 cells, and the locals are the cells above the value slots.
  {
    auto* function =
        llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                               llvm::GlobalValue::ExternalLinkage, "big_local", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    // 64 cells of local, and 200 values: the sum does not fit.
    builder.CreateAlloca(llvm::ArrayType::get(llvm::Type::getInt64Ty(context), 64), nullptr,
                         "buffer");
    llvm::Value* value = function->getArg(0);
    for (int index = 0; index < 200; ++index) {
      value = builder.CreateAdd(value, builder.getInt32(1));
    }
    builder.CreateRet(value);
    check_refused(*function, cs2::protect::VmRefusal::local_variable,
                  "locals that do not fit in the frame are refused");
  }

  // A callee with an `sret` parameter: Windows x64 rejects it in an
  // indirect signature.
  {
    auto* callee = llvm::Function::Create(
        llvm::FunctionType::get(i32, {llvm::PointerType::getUnqual(context)}, false),
        llvm::GlobalValue::ExternalLinkage, "with_sret", &module);
    callee->addParamAttr(0, llvm::Attribute::StructRet);
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                            llvm::GlobalValue::ExternalLinkage, "caller", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* out = builder.CreateAlloca(i32, nullptr, "out");
    builder.CreateRet(builder.CreateCall(callee, {out}));
    check_refused(*function, cs2::protect::VmRefusal::indirect_parameter,
                  "a callee with an sret parameter is refused");
  }

  // A `musttail` call: the tail-call contract does not survive the rewrite.
  {
    auto* callee = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                          llvm::GlobalValue::ExternalLinkage, "tail", &module);
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                            llvm::GlobalValue::ExternalLinkage, "caller", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* call = builder.CreateCall(callee, {function->getArg(0)});
    call->setTailCallKind(llvm::CallInst::TCK_MustTail);
    builder.CreateRet(call);
    check_refused(*function, cs2::protect::VmRefusal::musttail, "a musttail call is refused");
  }

  // A call with more arguments than the budget.
  {
    std::vector<llvm::Type*> parameters(65, i32);
    auto* callee = llvm::Function::Create(llvm::FunctionType::get(i32, parameters, false),
                                          llvm::GlobalValue::ExternalLinkage, "many", &module);
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                            llvm::GlobalValue::ExternalLinkage, "caller", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    std::vector<llvm::Value*> arguments(65, function->getArg(0));
    builder.CreateRet(builder.CreateCall(callee, arguments));
    check_refused(*function, cs2::protect::VmRefusal::too_many_arguments,
                  "a call with too many arguments is refused");
  }

  // An atomic access: the ordering guarantees are not modelled.
  {
    auto* function = llvm::Function::Create(llvm::FunctionType::get(i32, {i32}, false),
                                            llvm::GlobalValue::ExternalLinkage, "atomic", &module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
    llvm::IRBuilder<> builder(entry);
    auto* slot = builder.CreateAlloca(i32, nullptr, "slot");
    builder.CreateStore(function->getArg(0), slot);
    auto* loaded = builder.CreateAlignedLoad(i32, slot, llvm::MaybeAlign());
    loaded->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
    builder.CreateRet(loaded);
    check_refused(*function, cs2::protect::VmRefusal::atomic_access, "an atomic access is refused");
  }
}

/// The virtualizer's front end: a function that did not survive.
///
/// The transform reports such a function as `vanished`, and the analysis
/// has to say the same: an operator who cannot tell "not selected" from
/// "selected and deleted by the optimiser" cannot act on the verdict.
void test_vm_vanished() {
  llvm::LLVMContext context;
  llvm::Module module("vanished.obj", context);
  auto* function =
      llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {}, false),
                             llvm::GlobalValue::ExternalLinkage, "survivor", &module);
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
  llvm::IRBuilder<> builder(entry);
  builder.CreateRet(builder.getInt32(0));

  cs2::protect::Config config;
  const std::vector<std::string> symbols{"survivor", "ghost"};
  const auto results = cs2::protect::analyze_module(module, config, symbols, {});
  check(results.size() == 2, "the analysis has a verdict for every selected function");
  if (results.size() != 2) {
    return;
  }
  check(results[0].virtualizable, "the function that is there is virtualizable");
  check(!results[1].virtualizable, "the function that vanished is not virtualizable");
  check(results[1].failure.reason == cs2::protect::VmRefusal::vanished,
        "the function that vanished is reported as vanished");
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

  // The widths: the call carries the width of the value it returns (the
  // callee returns an `i32`), and the pushes carry the width of a cell.
  for (const cs2::protect::VmInstruction& instruction : result.code) {
    if (instruction.opcode == cs2::protect::VmOpcode::Call) {
      check(instruction.width == 4, "the call that returns an i32 has the width 4");
    }
    if (instruction.opcode == cs2::protect::VmOpcode::Arg ||
        instruction.opcode == cs2::protect::VmOpcode::Pop) {
      check(instruction.width == 8, "a cell of the argument stack has the width 8");
    }
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
@.str.address = private unnamed_addr constant [8 x i8] c"address\00"
@.str.twoblock = private unnamed_addr constant [9 x i8] c"twoblock\00"
@.str.tworeasons = private unnamed_addr constant [10 x i8] c"tworeason\00"
@.str.aspace = internal addrspace(1) constant [8 x i8] c"aspace1\00"

@global.init = private constant { ptr, i64 } { ptr @.str.escapes, i64 7 }

declare void @may_throw()
declare i32 @__gxx_personality_v0(...)
declare i64 @use_address(i64)

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

define void @uses_two_reasons_optnone(ptr %out) noinline optnone {
entry:
  %c = load i8, ptr @.str.tworeasons
  store i8 %c, ptr %out
  ret void
}

define i1 @compares_two_reasons() {
entry:
  %c = icmp eq ptr @.str.tworeasons, null
  ret i1 %c
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

define i64 @takes_the_address() {
entry:
  %a = call i64 @use_address(i64 ptrtoint (ptr @.str.address to i64))
  ret i64 %a
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

define void @uses_aspace(ptr addrspace(1) %out) {
entry:
  %c = load i8, ptr addrspace(1) @.str.aspace
  store i8 %c, ptr addrspace(1) %out
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
  check(cs2::protect::string_selected(policy, "healthy"), "an exact glob selects the literal");
  check(cs2::protect::string_selected(policy, "cs2_esp"), "a wildcard glob selects the literal");
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
  selection.functions.push_back({"uses_virtual", "", cs2::protect::ProtectionLevel::virtualized});

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
  check_reason(".str.empty", cs2::protect::StringSkipReason::empty, "an empty literal is refused");
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
  check_reason(".str.address", cs2::protect::StringSkipReason::unsupported_use,
               "a literal whose address is taken as an integer is refused");
  check_reason(".str.aspace", cs2::protect::StringSkipReason::unsupported_address_space,
               "a literal in another address space is refused");
  // A literal with **two** reasons has one verdict: the first *rule* of the
  // table that holds. The use list of the literal is the module's, and the
  // last use comes first, so the use the old gate answered with (the
  // unrewritable one) is the first of the list.
  check_reason(".str.tworeasons", cs2::protect::StringSkipReason::optnone_function,
               "a literal with two reasons is refused with the first rule that holds");
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

  const cs2::protect::StringResult result = cs2::protect::transform_strings(*module, config, {});
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
  const std::vector<cs2::protect::CoverageString> covered = cs2::protect::coverage_strings(result);
  check(covered.size() == 1, "the coverage has one literal");
  if (!covered.empty()) {
    check(covered[0].length == 7, "the coverage records the length");
    check(covered[0].digest.size() == 64, "the coverage records a digest");
    check(covered[0].digest != "healthy", "the coverage does not carry the content");
  }
  check(!cs2::protect::write_coverage(config, *module, {}, {}, {}, {}, covered).has_value(),
        "the coverage is written");
  for (const auto& path : std::filesystem::directory_iterator(directory)) {
    for (const std::string& line : read_lines(path)) {
      check(line.find("healthy") == std::string::npos,
            "the coverage file does not carry the plaintext");
    }
  }

  // The digest is salted with the build seed: the same literal, hashed
  // with a seed and without one, gives two digests, and a coverage (or a
  // build log) that carried the unsalted one would yield the strings.
  llvm::LLVMContext salted_context;
  const auto salted_module = build_string_module(salted_context);
  cs2::protect::Policy salted_policy;
  salted_policy.strings = {"healthy"};
  salted_policy.strings_min_length = 4;
  salted_policy.seed = 0x0123456789ABCDEFULL;
  cs2::protect::Config salted_config;
  salted_config.policy = salted_policy;
  const cs2::protect::StringResult salted =
      cs2::protect::transform_strings(*salted_module, salted_config, {});
  check(!cs2::protect::salt_proof(salted_config).empty(),
        "a build with a seed has a proof of its salt");
  const std::vector<cs2::protect::CoverageString> salted_covered =
      cs2::protect::coverage_strings(salted);
  check(salted_covered.size() == 1, "the salted coverage has one literal");
  if (!salted_covered.empty() && !covered.empty()) {
    check(salted_covered[0].digest != covered[0].digest,
          "the digest of a literal is not salted with the build seed");
  }

  std::filesystem::remove_all(directory, error);
}

/// The transform: a literal in another address space is refused, and the
/// module comes out whole.
///
/// The blob and the buffer are created in the **default** address space, so
/// a literal in another one cannot be replaced by them: the users would
/// point at a buffer of the wrong address space and the module would come
/// out broken ("Broken module found"). The gate refuses such a literal, and
/// the refusal leaves it exactly as it is.
void test_string_address_space() {
  llvm::LLVMContext context;
  const auto module = build_string_module(context);
  if (module == nullptr) {
    return;
  }

  cs2::protect::Policy policy;
  policy.strings = {"aspace1"};
  policy.strings_min_length = 4;
  cs2::protect::Config config;
  config.policy = policy;

  const cs2::protect::StringResult result = cs2::protect::transform_strings(*module, config, {});
  check(result.protected_count == 0, "a literal in another address space is not protected");
  check(result.refused_count == 1, "the literal in another address space is refused");
  if (!result.literals.empty()) {
    check(result.literals[0].reason.has_value(), "the refusal has a reason");
    if (result.literals[0].reason.has_value()) {
      check_equal(cs2::protect::to_string(*result.literals[0].reason), "unsupported_address_space",
                  "the refusal names the address space");
    }
  }
  // The literal is left exactly as it is.
  check(module->getGlobalVariable(".str.aspace", /*AllowLocal=*/true) != nullptr,
        "the refused literal is still in the module");
  // And the module is whole: a broken module is the failure this refusal
  // exists to avoid.
  std::string message;
  llvm::raw_string_ostream stream(message);
  const bool broken = llvm::verifyModule(*module, &stream);
  check(!broken, "the module of a refused address space verifies");
  if (broken) {
    std::fprintf(stderr, "FAIL: the module does not verify: %s\n", stream.str().c_str());
  }
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
  test_mandatory_seed();
  test_per_build_mixer();
  test_qualified_name();
  test_selection();
  test_coverage();
  test_transform();
  test_transform_sret_other_target();
  test_transform_sret();
  test_transform_sret_member();
  test_transform_sret_functlet();
  test_transform_parameters();
  test_transform_sret_result();
  test_layout_permutation();
  test_level_gate();
  test_level_gate_two_entries();
  test_vanished();
  test_cross_module_policy();
  test_transform_cross_module();
  test_transform_cross_module_not_named();
  test_transform_cross_module_blocked();
  test_transform_cross_module_invoke();
  test_vm_virtualizable();
  test_vm_parallel_copy();
  test_vm_flags();
  test_vm_freeze();
  test_vm_refusals();
  test_vm_call();
  test_vm_vanished();
  test_string_policy();
  test_string_gate();
  test_string_transform();
  test_string_address_space();
  test_string_decode_dominance();
  if (g_failures != 0) {
    std::printf("protect: %d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("protect: ok");
  return 0;
}
