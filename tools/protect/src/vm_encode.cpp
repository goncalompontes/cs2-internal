// The encoder of the front end of the virtualizer.
//
// The encoder writes the container: the header, the instruction stream and
// the encryption. It also holds the per-build opcode permutation and the
// entry points of the analysis.

#include "vm_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include "selection.hpp"

namespace cs2::protect {

namespace {
/// The greatest common divisor, for the permutation's multiplier.
[[nodiscard]] auto greatest_common_divisor(std::uint64_t left, std::uint64_t right) noexcept
    -> std::uint64_t {
  while (right != 0) {
    const std::uint64_t rest = left % right;
    left = right;
    right = rest;
  }
  return left;
}

/// The inverse of a number modulo another, with the extended Euclidean
/// algorithm.
[[nodiscard]] auto modular_inverse(std::uint64_t value, std::uint64_t modulus) noexcept
    -> std::uint64_t {
  std::int64_t old_remainder = static_cast<std::int64_t>(value % modulus);
  std::int64_t remainder = static_cast<std::int64_t>(modulus);
  std::int64_t old_coefficient = 1;
  std::int64_t coefficient = 0;
  while (remainder != 0) {
    const std::int64_t quotient = old_remainder / remainder;
    const std::int64_t rest = old_remainder - quotient * remainder;
    old_remainder = remainder;
    remainder = rest;
    const std::int64_t next = old_coefficient - quotient * coefficient;
    old_coefficient = coefficient;
    coefficient = next;
  }
  std::int64_t inverse = old_coefficient % static_cast<std::int64_t>(modulus);
  if (inverse < 0) {
    inverse += static_cast<std::int64_t>(modulus);
  }
  return static_cast<std::uint64_t>(inverse);
}

/// Writes a little-endian value into a buffer.
template <typename T>
void write_le(std::vector<std::uint8_t>& buffer, std::size_t offset, T value) {
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    buffer[offset + index] = static_cast<std::uint8_t>((value >> (8U * index)) & 0xFFU);
  }
}

/// The keystream of the container: the byte at the offset `position`.
///
/// The cipher is a counter-mode stream: the word at the position is the
/// mixer of the key and of the position, and the byte is one of its eight.
/// The interpreter decrypts on fetch, so the blob stays read-only and the
/// interpreter is re-entrant.
[[nodiscard]] auto keystream_byte(std::uint64_t key, std::size_t position) noexcept
    -> std::uint8_t {
  const std::uint64_t word = position / 8U;
  const std::uint64_t mixed = mix64(key + word * k_mix_multiplier);
  return static_cast<std::uint8_t>((mixed >> (8U * (position % 8U))) & 0xFFU);
}

/// The container of a lowered function: the header, the instructions and the
/// encryption.
[[nodiscard]] auto encode(const std::vector<VmInstruction>& code, const VmHeader& header,
                          std::uint64_t key, std::uint64_t perm_a, std::uint64_t perm_b)
    -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> buffer(k_header_bytes + code.size() * k_instruction_bytes, 0);

  write_le<std::uint32_t>(buffer, 0, header.magic);
  write_le<std::uint32_t>(buffer, 4, header.version);
  write_le<std::uint32_t>(buffer, 8, header.slot_count);
  write_le<std::uint32_t>(buffer, 12, header.arg_slots);
  write_le<std::uint32_t>(buffer, 16, header.call_slots);
  write_le<std::uint32_t>(buffer, 20, header.instruction_count);
  write_le<std::uint32_t>(buffer, 24, header.flags);
  write_le<std::uint32_t>(buffer, 28, header.local_cells);

  for (std::size_t index = 0; index < code.size(); ++index) {
    const VmInstruction& instruction = code[index];
    const auto offset = k_header_bytes + index * k_instruction_bytes;
    buffer[offset] = permuted_opcode(instruction.opcode, perm_a, perm_b);
    buffer[offset + 1] = instruction.width;
    buffer[offset + 2] = instruction.flags;
    buffer[offset + 3] = 0;
    write_le<std::uint16_t>(buffer, offset + 4, instruction.dst);
    write_le<std::uint16_t>(buffer, offset + 6, instruction.src1);
    write_le<std::uint16_t>(buffer, offset + 8, instruction.src2);
    write_le<std::uint16_t>(buffer, offset + 10, instruction.src3);
    write_le<std::uint32_t>(buffer, offset + 12, instruction.aux);
    write_le<std::uint64_t>(buffer, offset + 16, instruction.imm);
  }

  // The encryption: the whole container, the header included, so that an
  // analyst reads nothing but noise.
  for (std::size_t index = 0; index < buffer.size(); ++index) {
    buffer[index] ^= keystream_byte(key, index);
  }
  return buffer;
}

}  // namespace

auto permuted_opcode(VmOpcode opcode, std::uint64_t a, std::uint64_t b) noexcept -> std::uint8_t {
  const auto number = static_cast<std::uint64_t>(opcode);
  return static_cast<std::uint8_t>((number * a + b) % k_opcode_count);
}

auto canonical_opcode(std::uint8_t permuted, std::uint64_t a, std::uint64_t b) noexcept
    -> VmOpcode {
  const auto inverse = modular_inverse(a % k_opcode_count, k_opcode_count);
  const auto value = static_cast<std::uint64_t>(permuted);
  const auto number = ((value + k_opcode_count - (b % k_opcode_count)) * inverse) % k_opcode_count;
  return static_cast<VmOpcode>(number);
}

auto opcode_permutation(std::uint64_t seed) noexcept -> std::pair<std::uint64_t, std::uint64_t> {
  std::uint64_t state = seed ^ 0x564D5045524D5554ULL;
  std::uint64_t a = splitmix64(state) % k_opcode_count;
  // The multiplier has to be **odd** and coprime with the number of the
  // opcodes: the permutation is a permutation only if it is coprime, and
  // the contract (the specification and the README) says it is odd. The
  // walk below keeps both properties: `+1` makes an even multiplier odd,
  // and `+2` keeps it odd while it looks for a coprime one. One is always
  // odd and always coprime, so the walk terminates.
  for (std::size_t step = 0; step < 2 * k_opcode_count; ++step) {
    if (a % 2 == 0) {
      a = (a + 1) % k_opcode_count;
      continue;
    }
    if (greatest_common_divisor(a, k_opcode_count) == 1) {
      break;
    }
    a = (a + 2) % k_opcode_count;
    if (a == 0) {
      a = 1;
    }
  }
  const auto b = splitmix64(state) % k_opcode_count;
  return {a, b};
}

auto analyze_function(llvm::Function& function, const VmOptions& options) -> VmFunction {
  VmFunction result;
  result.symbol = function.getName().str();
  result.qualified = qualified_name(result.symbol);

  const auto [perm_a, perm_b] = opcode_permutation(options.seed);
  result.perm_a = perm_a;
  result.perm_b = perm_b;
  result.key_parts = key_parts(options.seed);
  result.key = mix64(result.key_parts[0] ^ result.key_parts[1] ^ result.key_parts[2]);

  const auto lowered = detail::lower(function, options);
  if (!lowered.has_value()) {
    result.virtualizable = false;
    result.failure = lowered.error();
    result.frame_bytes = k_frame_bytes;
    return result;
  }

  result.virtualizable = true;
  result.slots = lowered->slots;
  result.local_cells = lowered->local_cells;
  result.instructions = lowered->code.size();
  result.calls = lowered->call_sites.size();
  result.pool = lowered->pool;
  result.call_sites = lowered->call_sites;
  result.code = lowered->code;
  result.frame_bytes = k_frame_bytes;

  result.header.magic = k_vm_magic;
  result.header.version = k_vm_version;
  result.header.slot_count = static_cast<std::uint32_t>(result.slots);
  result.header.arg_slots = static_cast<std::uint32_t>(k_frame_argument_slots);
  result.header.call_slots = static_cast<std::uint32_t>(k_frame_call_slots);
  result.header.instruction_count = static_cast<std::uint32_t>(result.instructions);
  result.header.flags = k_vm_flag_encrypted | k_vm_flag_permuted;
  result.header.local_cells = static_cast<std::uint32_t>(result.local_cells);

  result.blob = encode(result.code, result.header, result.key, perm_a, perm_b);
  result.bytecode_bytes = result.blob.size();
  return result;
}

auto analyze_module(llvm::Module& module, const Config& config,
                    std::span<const std::string> symbols,
                    std::span<const std::string> virtualized_symbols) -> std::vector<VmFunction> {
  VmOptions options;
  options.seed = module_seed(config, module.getSourceFileName());
  options.virtualized_symbols = virtualized_symbols;

  std::vector<VmFunction> result;
  result.reserve(symbols.size());
  for (const std::string& symbol : symbols) {
    llvm::Function* function = module.getFunction(symbol);
    if (function == nullptr) {
      // The function did not survive to this phase. It gets a verdict, so
      // that the operator can tell "not selected" from "selected and
      // deleted by the optimiser": the transform reports the same thing
      // the same way.
      VmFunction vanished;
      vanished.symbol = symbol;
      vanished.qualified = qualified_name(symbol);
      vanished.virtualizable = false;
      vanished.failure =
          VmFailure{VmRefusal::vanished, "the function did not survive to this phase"};
      vanished.frame_bytes = k_frame_bytes;
      result.push_back(std::move(vanished));
      continue;
    }
    result.push_back(analyze_function(*function, options));
  }
  return result;
}

}  // namespace cs2::protect
