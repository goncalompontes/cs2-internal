#pragma once

// The plugin's configuration channel.
//
// A statically linked pass plugin owns its own `cl` registry, so a
// `-mllvm -cs2-protect-...` option never reaches it: the driver parses
// `-mllvm` against *its own* registry and rejects the unknown name. The
// configuration therefore travels through the environment, which the compiler
// process hands down to the plugin unchanged (see tools/protect/README.md).

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cs2::protect {

/// How much work the transform (T19/T20) will do for a selected function.
enum class ProtectionLevel : std::uint8_t {
  call,         ///< encrypt the call sites of the function (T19).
  virtualized,  ///< move the body into an interpreter (T20).
};

[[nodiscard]] auto to_string(ProtectionLevel level) noexcept -> const char*;

/// Where a selection came from.
enum class SelectionSource : std::uint8_t {
  annotation,  ///< `__attribute__((annotate("cs2.protect")))`.
  policy,      ///< a `protect` entry of the policy file.
};

[[nodiscard]] auto to_string(SelectionSource source) noexcept -> const char*;

/// One `protect` entry of the policy file.
///
/// The pattern is a glob (`*` and `?`) matched against the mangled symbol and
/// against the demangled qualified name. The module scope is the source file
/// the entry belongs to; an empty scope means "any module".
struct PolicyEntry {
  std::string pattern;
  std::string module;
  ProtectionLevel level{ProtectionLevel::call};
};

/// The policy file: what to protect, what to refuse, and the build seed.
///
/// The `strings` section is a section of its own: the `protect` entries name
/// *functions*, and a string is not a function. The strings do not go through
/// the selection phase's function machinery -- they get their own selection
/// step in the transform phase.
struct Policy {
  std::vector<PolicyEntry> protect;
  std::vector<std::string> deny;
  /// The literals to protect: a glob (`*` and `?`) matched against the
  /// *content* of the literal. An exact string is a glob with no wildcard.
  ///
  /// Nothing is protected unless this list says so: the default is "protect
  /// no string", because a surprise is worse than a gap.
  std::vector<std::string> strings;
  /// The literals to refuse: a glob matched against the content of the
  /// literal. A deny entry wins over a select entry.
  std::vector<std::string> strings_deny;
  /// The shortest literal that is worth a decode.
  ///
  /// A literal shorter than this is refused (`too_short`). The default is
  /// four: a three-byte string carries too little to be worth the decode.
  std::size_t strings_min_length{4};
  std::optional<std::uint64_t> seed;
};

/// The resolved configuration of one compiler process.
struct Config {
  std::optional<Policy> policy;        ///< absent => no policy file was given.
  std::optional<std::uint64_t> seed;   ///< CS2_PROTECT_SEED.
  bool verbose{false};                 ///< CS2_PROTECT_VERBOSE.
  std::filesystem::path policy_path;   ///< the policy file that was read.
  std::string policy_digest;           ///< SHA-256 of the policy bytes.
  std::filesystem::path coverage_dir;  ///< where the per-module coverage goes.
};

/// A configuration problem. The plugin reports it and fails the compile.
struct ConfigError {
  std::string message;
};

/// Reads the configuration from the environment.
///
/// A malformed policy file is an error *value*, never an exception: the pass
/// must not throw out into the driver, which is built without exceptions.
[[nodiscard]] auto load_config() -> std::expected<Config, ConfigError>;

/// Parses the text of a policy file.
///
/// The reader is the SDK's (`llvm::json::parse`), with two checks on top of
/// it, because the SDK's parser alone is not enough:
///
/// * the SDK's parser recurses, so a file that nests thousands of levels
///   deep overflows the compiler's stack and kills it without a message —
///   the nesting depth is therefore bounded *before* the text is parsed;
/// * the SDK's parser keeps the *first* of two equal keys, so a duplicated
///   key would be silently ignored — the duplicate is rejected instead.
///
/// The policy *format* is the one of the spec; only the reader changed.
[[nodiscard]] auto parse_policy_text(std::string_view text) -> std::expected<Policy, ConfigError>;

}  // namespace cs2::protect
