#pragma once

// What the pass reports.
//
// The report is the acceptance evidence for the protection pass: it names
// every selected function, and — with CS2_PROTECT_VERBOSE=1 — every function
// that was skipped and why. The transform phase adds its own lines: the
// number of calls it rewrote, the number it skipped per reason, and the
// size of the table and of the descriptor it added to the module.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "selection.hpp"
#include "strings.hpp"
#include "transform.hpp"

namespace llvm {
class Module;
}  // namespace llvm

namespace cs2::protect {

/// The state of a policy entry, as the coverage records it.
enum class EntryState : std::uint8_t {
  matched,        ///< the entry named a function that is protected.
  unmatched,      ///< the entry named no function in this module.
  blocked,        ///< the entry named a function that cannot be protected.
  unimplemented,  ///< the entry named a function whose level is not implemented.
  vanished,       ///< the entry named a selected function that did not survive
                  ///< to the transform phase.
};

[[nodiscard]] auto to_string(EntryState state) noexcept -> const char*;

/// One entry of the coverage file.
struct CoverageEntry {
  std::string pattern;
  EntryState state{EntryState::unmatched};
  std::string reason;  ///< the blocker or the unimplemented level ("" if none).
};

/// What the transform did to one selected function.
struct CoverageFunction {
  std::string symbol;
  std::size_t rewritten{0};
  std::size_t skipped{0};
};

/// What the transform put into the module.
///
/// The payload check needs it to decode the descriptor of the *image* and to
/// compare the resolved address with the table entry: the check has to prove
/// that the dispatched callee is the right one, not only that the number of
/// the dispatches is right.
struct CoverageProtect {
  std::size_t table_size{0};
  std::size_t descriptor_size{0};
  /// The parts of the key, in the order the call site combines them.
  std::vector<std::uint64_t> key_parts;
  /// The multiplier of the mixer, as the call site uses it.
  std::uint64_t mix_multiplier{0};
  /// The xor constant of the mixer, as the call site uses it.
  std::uint64_t mix_xor{0};
};

/// One literal of the coverage file.
///
/// The coverage **must not** carry the content of a protected string: the
/// coverage is a file, and a file holds the strings. It carries the length
/// and a digest of the content instead, which is enough for the payload
/// check to prove that the plaintext is not in the image. The digest is
/// **salted with the build seed**, so that the coverage cannot be brute
/// forced back into the strings.
struct CoverageString {
  std::size_t length{0};
  /// The salted SHA-256 of the content, in hexadecimal.
  std::string digest;
  std::size_t uses{0};
  std::size_t functions{0};
  bool protected_literal{false};
  /// The refusal reason ("" when the literal was protected).
  std::string reason;
};

/// Prints the selection report of one module to stderr.
void write_report(const llvm::Module& module, const SelectionResult& result, const Config& config);

/// Prints the transform report of one module to stderr.
///
/// The report names the functions that were transformed, the number of calls
/// that were rewritten and the number that were skipped per reason. With
/// CS2_PROTECT_VERBOSE=1 it names every skipped call.
void write_transform_report(const llvm::Module& module, const TransformResult& result,
                            const Config& config);

/// Prints the string report of one module to stderr.
///
/// The report names every literal the policy selected, and whether it was
/// protected or refused (and why). The report goes to stderr, so it may name
/// the content: the *coverage* may not.
void write_string_report(const llvm::Module& module, const StringResult& result,
                          const Config& config);

/// The coverage entries of a selection result.
[[nodiscard]] auto coverage_entries(const SelectionResult& result) -> std::vector<CoverageEntry>;

/// The coverage entries of a string result.
///
/// The content of a literal is **not** in the coverage: the entry carries the
/// length and the digest of the content, and that is enough for the payload
/// check to prove that the plaintext is not in the image.
[[nodiscard]] auto coverage_strings(const StringResult& result) -> std::vector<CoverageString>;

/// The coverage entries of a recorded selection.
///
/// An entry whose selected function did not survive to the transform phase is
/// recorded as *vanished*: the protection would be silently lost, and the
/// whole-build check has to fail for it.
[[nodiscard]] auto coverage_entries(const RecordedSelection& selection, std::string_view module_id,
                                    const std::vector<RecordedFunction>& vanished)
    -> std::vector<CoverageEntry>;

/// Writes the machine-readable coverage of one module.
///
/// A pass sees one translation unit at a time, so it cannot know whether a
/// policy entry matched *somewhere* in the build. The coverage file records
/// what each module decided; tools/protect/check_policy.py reads them all
/// after the build and fails it when an entry matched nothing.
///
/// The file name carries the policy digest and the module's identity *inside
/// the source tree* (its path relative to the root the policy and the source
/// share), so that the coverage of one policy is never mistaken for the
/// coverage of another, and so that two sources never write the same file.
/// The name does not depend on the build directory: two worktrees of one
/// tree write the *same* file, so a rebuild overwrites the coverage instead
/// of leaving a stale copy of it behind (a stale copy would make the
/// payload check count the rewrites twice).
///
/// The file also carries the digest of the source file and a digest of its
/// own content, so that a reader can tell a coverage file of *this* build
/// from one of another build or from a file that was edited by hand.
///
/// The `strings_salt` names the salt of the digests of the literals ("" when
/// there is none): the salt itself is **not** in the file, so that the file
/// cannot be brute forced back into the strings.
[[nodiscard]] auto write_coverage(const Config& config, const llvm::Module& module,
                                  const std::vector<CoverageEntry>& entries,
                                  const std::vector<CoverageFunction>& functions,
                                  const CoverageProtect& protect,
                                  const std::vector<CoverageString>& strings = {},
                                  std::string_view strings_salt = {}) -> std::optional<std::string>;

/// Writes the machine-readable coverage of one module from a selection.
[[nodiscard]] auto write_coverage(const Config& config, const llvm::Module& module,
                                  const SelectionResult& result) -> std::optional<std::string>;

}  // namespace cs2::protect
