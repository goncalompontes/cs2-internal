#pragma once

// The per-build layout: the pass permutes the module's function list.
//
// The shipped file must not be a signature, and the seed is what makes it
// one build's file and not another's. The seed drives the protection's own
// constants (the mixer), the keys and the tables -- but the **code** of the
// payload was the same in every build: the same 1.7 MB of `.text`, byte for
// byte, whatever the seed was.
//
// The lever is the order of the functions of a translation unit. Measured:
// the object writer emits the functions in the module's function list order,
// the linker keeps the order of an object's sections, and so a pass that
// reorders the list moves the code in the image. Renaming the symbols
// instead moves **nothing**: the layout follows the definition order, not the
// name.
//
// The permutation keeps **every name**, so the coverage, the report, the
// policy matching and the payload check (which works on addresses) keep
// working untouched. That is the whole reason it is the chosen mechanism.
//
// The reach is bounded and it is stated plainly: the permutation is *inside*
// a translation unit, so the units the plugin does not compile (the MSVC
// STL's, for instance) keep their offsets.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace llvm {
class Module;
}  // namespace llvm

namespace cs2::protect {

/// What the permutation did to one module.
struct LayoutResult {
  /// The number of the definitions of the module.
  std::size_t functions{0};
  /// The number of the definitions that moved.
  ///
  /// It is never zero for a module with more than one definition: a
  /// permutation that leaves the order unchanged is a no-op, and a no-op is
  /// not a layout.
  std::size_t moved{0};
  /// The seed the permutation came from.
  std::uint64_t seed{0};
};

/// The permutation of `count` items, from the seed.
///
/// The result is a permutation: every index of [0, count) appears exactly
/// once, so no function is lost and none is duplicated.
[[nodiscard]] auto layout_permutation(std::size_t count, std::uint64_t seed)
    -> std::vector<std::size_t>;

/// Permutes the definitions of the module with the seed.
///
/// The declarations stay where they are: they have no code, and the order of
/// the declarations is not the pass's to change.
[[nodiscard]] auto permute_layout(llvm::Module& module, std::uint64_t seed) -> LayoutResult;

}  // namespace cs2::protect
