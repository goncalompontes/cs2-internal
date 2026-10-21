#pragma once

// Which functions the protection pass selects.
//
// Two sources, both optional: an `__attribute__((annotate("cs2.protect")))`
// on the definition, and a `protect` entry of the policy file.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"

namespace llvm {
class Module;
}  // namespace llvm

namespace cs2::protect {

/// Why a function was not protected.
enum class SkipReason : std::uint8_t {
  not_annotated,  ///< neither annotated nor named by the policy.
  denied,         ///< a `deny` entry matched.
  optnone,        ///< the frontend marked it `optnone` (-O0).
  always_inline,  ///< `alwaysinline`: it is inlined by definition.
  declaration,    ///< no body in this module.
  intrinsic,      ///< a compiler intrinsic.
};

[[nodiscard]] auto to_string(SkipReason reason) noexcept -> const char*;

/// A function the pass selected.
struct Selection {
  std::string symbol;     ///< the mangled name.
  std::string qualified;  ///< the demangled qualified name ("" if unknown).
  SelectionSource source{SelectionSource::policy};
  ProtectionLevel level{ProtectionLevel::call};
  std::uint64_t seed{0};
  bool may_inline{false};       ///< the function is not `noinline`: it can be inlined away.
  bool marked_noinline{false};  ///< the pass added the `noinline` attribute to it.
};

/// A function the pass did not select, and why.
struct Skipped {
  std::string symbol;
  std::string qualified;
  SkipReason reason{SkipReason::not_annotated};
};

/// What one policy entry did in one module.
///
/// An entry is *matched* when it named a function that the pass protects, and
/// *blocked* when it named a function that cannot be protected (an
/// `alwaysinline` function, a declaration, a denied function, ...). A
/// blocked entry is **not** a matched entry: the build has to fail, because a
/// policy that names a function that cannot be protected protects nothing.
///
/// An entry that named a function whose *level* is not implemented yet (the
/// `virtualized` level of T20) is neither matched nor blocked: it named a
/// function, but this task does not transform it, so the build has to fail
/// for it as well.
struct PolicyMatch {
  std::string pattern;                           ///< the glob of the entry.
  std::string module;                            ///< the scope of the entry ("" = any module).
  ProtectionLevel level{ProtectionLevel::call};  ///< the level of the entry.
  bool applicable{false};                        ///< the scope of the entry names this module.
  bool matched{false};                           ///< the entry named a function that is protected.
  bool blocked{false};        ///< the entry named a function that cannot be protected.
  bool unimplemented{false};  ///< the entry named a function whose level is not
                              ///< implemented yet.
  std::optional<SkipReason> blocked_reason;  ///< why it cannot be protected.
};

/// What the pass decided for one module.
struct SelectionResult {
  std::vector<Selection> selected;
  std::vector<Skipped> skipped;
  std::vector<PolicyMatch> policy_matches;
};

/// Decides what to protect in one module. Never throws.
[[nodiscard]] auto select_functions(const llvm::Module& module, const Config& config)
    -> SelectionResult;

/// The glob the policy entries use: `*` matches any run of characters, `?`
/// matches exactly one.
[[nodiscard]] auto glob_match(std::string_view pattern, std::string_view text) noexcept -> bool;

/// The qualified name of a mangled symbol ("cs2::aim::solve"), or "" when it
/// cannot be derived.
[[nodiscard]] auto qualified_name(std::string_view symbol) -> std::string;

/// The per-function seed for a build that has no global one. It is derived
/// from the module id and the symbol, so a build stays reproducible.
[[nodiscard]] auto derive_seed(std::string_view module_id, std::string_view symbol) noexcept
    -> std::uint64_t;

/// Whether a policy entry's module scope names this source file.
[[nodiscard]] auto module_applies(std::string_view scope, std::string_view module_id) noexcept
    -> bool;

}  // namespace cs2::protect
