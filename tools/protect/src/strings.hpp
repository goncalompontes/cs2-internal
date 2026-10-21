#pragma once

// The string encryption.
//
// The payload's operational strings -- the feature keys, the JSON keys, the
// convar names -- are plaintext in the shipped DLL: an analyst with the file
// reads them in IDA and learns the feature list without running anything.
//
// This transform encrypts the literals the policy selects, **deletes** the
// plaintext literal from the module, and emits a decode into each using
// function: the decode fills a stack buffer, and every use of the literal
// now points at that buffer.
//
// The form is fixed by the specification (§3.1): the plaintext lives in a
// stack buffer, one `alloca` in the entry block of the using function and
// one decode call before the first use. The alternatives lose:
//
//   * the micro-slot (every byte in a global of its own) is the strongest
//     anti-dump property, but the payload's uses are `std::string_view`
//     comparisons, which need a *contiguous* buffer;
//   * a cached global keeps the plaintext in the image's memory forever, and
//     needs an initialiser, which needs a constructor, which is forbidden:
//     nothing in a manually mapped image is ever initialised;
//   * a constructor decode is forbidden outright.
//
// The eligibility gate is **fail closed**: a literal is protected only if it
// passes every check, and a refusal leaves the literal exactly as it is.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "transform.hpp"

namespace llvm {
class GlobalVariable;
class Module;
}  // namespace llvm

namespace cs2::protect {

/// Why a literal was not protected.
///
/// The order of the enumerators is the order of the specification's table:
/// the first rule that holds is the reason that is reported.
enum class StringSkipReason : std::uint8_t {
  escapes_to_initializer,  ///< the literal's address ends up in a global
                           ///< initializer: a pointer to a decoded buffer
                           ///< cannot exist before the program runs.
  unsupported_shape,       ///< the literal is not a `ConstantDataSequential`
                           ///< of `i8`.
  empty,                   ///< the literal is empty.
  too_short,               ///< the literal is shorter than the policy's
                           ///< `strings_min_length`.
  optnone_function,        ///< a user of it is in an `optnone` function.
  virtualized_function,    ///< a user of it is in a function that is selected
                           ///< at the `virtualized` level.
  eh_use,                  ///< a user of it is in a function with an EH pad.
  unsupported_use,         ///< the use is not a plain pointer use.
};

[[nodiscard]] auto to_string(StringSkipReason reason) noexcept -> const char*;

/// The verdict for one literal.
struct StringLiteral {
  /// The plaintext.
  ///
  /// The report may name it (the report goes to stderr); the coverage may
  /// not (the coverage is a file, and a file holds the strings).
  std::string content;
  /// The length of the content, without the trailing NUL.
  std::size_t length{0};
  /// The digest of the content, in hexadecimal.
  ///
  /// The digest is **salted with the build seed**: an unsalted SHA-256 of
  /// a four-to-twelve byte literal is a dictionary attack away from the
  /// plaintext, so a coverage (or a build log) that carries it would still
  /// hold the strings.
  std::string digest;
  /// The number of uses the pass rewrote.
  std::size_t uses{0};
  /// The number of functions the pass rewrote a use in.
  std::size_t functions{0};
  /// Whether the literal was protected.
  bool protected_literal{false};
  /// Why it was not, when it was not.
  std::optional<StringSkipReason> reason;
};

/// What the string transform did to one module.
struct StringResult {
  /// One entry per literal the policy selected.
  std::vector<StringLiteral> literals;
  /// The number of protected literals.
  std::size_t protected_count{0};
  /// The number of refused literals.
  std::size_t refused_count{0};
  /// The bytes of plaintext that left the image.
  std::size_t plaintext_bytes{0};
  /// The bytes of ciphertext the blobs took.
  std::size_t blob_bytes{0};
  /// The number of decode calls the pass emitted.
  std::size_t decodes{0};
  /// Whether the digests of the literals are salted with the build seed.
  bool salted{false};
};

/// Whether the policy selects a literal with this content.
///
/// A literal is selected when its content matches one of the policy's
/// `strings` globs and none of its `strings_deny` globs. An exact string is
/// a glob with no wildcard.
[[nodiscard]] auto string_selected(const Policy& policy, std::string_view content) noexcept -> bool;

/// The content of a literal: the bytes of its initializer, without the
/// trailing NUL that a C string carries.
///
/// `std::nullopt` when the literal is not a `ConstantDataSequential` of `i8`:
/// such a literal has no content, so the policy cannot select it.
[[nodiscard]] auto string_content(const llvm::GlobalVariable& literal) -> std::optional<std::string>;

/// The eligibility gate.
///
/// `std::nullopt` when the literal may be protected. The gate never changes
/// the module: it only decides.
[[nodiscard]] auto string_gate(llvm::GlobalVariable& literal, const Config& config,
                               const RecordedSelection& selection)
    -> std::optional<StringSkipReason>;

/// The digest of a literal, salted with the build seed.
///
/// The salt is the seed of the build: the environment's `CS2_PROTECT_SEED`
/// when it is set, else the policy's `seed`. A build without a seed has no
/// salt, and the digests are unsalted.
[[nodiscard]] auto salt_proof(const Config& config) -> std::string;

/// Encrypts the selected literals of one module and rewrites their users.
///
/// The module is changed in place: the plaintext literals are deleted, the
/// blobs are added, and the decode is emitted into every using function.
[[nodiscard]] auto transform_strings(llvm::Module& module, const Config& config,
                                     const RecordedSelection& selection) -> StringResult;

}  // namespace cs2::protect
