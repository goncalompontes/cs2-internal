#include "layout.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include "cipher.hpp"

namespace cs2::protect {

namespace {

/// The salt of the permutation.
///
/// The module's seed is not enough on its own: the permutation of the
/// layout and the permutation of the table are different things, and a
/// stream that is used for both would make them correlated.
constexpr std::uint64_t k_layout_salt = 0x4C41594F55545F50ULL;

}  // namespace

auto layout_permutation(std::size_t count, std::uint64_t seed) -> std::vector<std::size_t> {
  std::vector<std::size_t> permutation(count);
  for (std::size_t index = 0; index < count; ++index) {
    permutation[index] = index;
  }

  // The Fisher-Yates walk: the permutation is seeded, and it is a
  // permutation by construction (every index is swapped at most once).
  std::uint64_t state = seed ^ k_layout_salt;
  for (std::size_t size = count; size > 1; --size) {
    const auto pick = static_cast<std::size_t>(splitmix64(state) % size);
    std::swap(permutation[size - 1], permutation[pick]);
  }

  // A permutation that leaves the order unchanged is a no-op: the module
  // would have the layout of every other build. The first two are swapped,
  // so that a module with more than one definition always moves at least one
  // function -- and the payload check can say so.
  if (count > 1 && std::is_sorted(permutation.begin(), permutation.end())) {
    std::swap(permutation[0], permutation[1]);
  }
  return permutation;
}

auto permute_layout(llvm::Module& module, std::uint64_t seed) -> LayoutResult {
  LayoutResult result;
  result.seed = seed;

  // The module's list, in order: the definitions and the declarations.
  std::vector<llvm::Function*> functions;
  for (llvm::Function& function : module) {
    functions.push_back(&function);
  }

  // The definitions, in the order the module lists them.
  std::vector<llvm::Function*> definitions;
  for (llvm::Function* function : functions) {
    if (function->isDeclaration()) {
      continue;
    }
    definitions.push_back(function);
  }
  result.functions = definitions.size();
  if (definitions.size() < 2) {
    return result;
  }

  const std::vector<std::size_t> permutation = layout_permutation(definitions.size(), seed);

  // The new order of the list: the declarations keep their slots, and the
  // definitions fill the definition slots in the permuted order.
  std::vector<llvm::Function*> reordered;
  reordered.reserve(functions.size());
  std::size_t next = 0;
  for (llvm::Function* function : functions) {
    if (function->isDeclaration()) {
      reordered.push_back(function);
      continue;
    }
    reordered.push_back(definitions[permutation[next]]);
    ++next;
  }

  // The number of the definitions that moved.
  for (std::size_t index = 0; index < reordered.size(); ++index) {
    if (reordered[index] != functions[index]) {
      ++result.moved;
    }
  }

  // The move itself: the functions are spliced into their new places. The
  // splice is the list's own way of moving a node, so the symbol table of
  // the module stays as it is.
  //
  // The object writer emits the functions in the module's function list
  // order, and the linker keeps the order of an object's sections, so this
  // is what moves the code in the image.
  llvm::Module::FunctionListType& list = module.getFunctionList();
  for (std::size_t index = 0; index < reordered.size(); ++index) {
    llvm::Function* wanted = reordered[index];
    auto target = list.begin();
    std::advance(target, static_cast<std::ptrdiff_t>(index));
    auto found = list.begin();
    while (found != list.end() && &*found != wanted) {
      ++found;
    }
    if (found == list.end() || found == target) {
      continue;
    }
    list.splice(target, list, found);
  }
  return result;
}

}  // namespace cs2::protect
