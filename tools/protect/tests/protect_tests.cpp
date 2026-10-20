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
//     blocked entry must be recorded as blocked.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include "config.hpp"
#include "report.hpp"
#include "selection.hpp"

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

}  // namespace

auto main() -> int {
  test_policy_reader();
  test_qualified_name();
  test_selection();
  test_coverage();
  if (g_failures != 0) {
    std::printf("protect: %d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("protect: ok");
  return 0;
}
