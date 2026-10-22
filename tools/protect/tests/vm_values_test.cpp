// The value-level test of the virtualizer's front end.
//
// The unit test (`protect_tests.cpp`) checks the **shape** of the bytecode:
// the opcodes, the counts, the slot numbers and the refusal reasons. It
// never checks what the bytecode *computes*, and a bytecode that computes
// the wrong values is the failure mode nothing else notices.
//
// This test closes the gap. It reads the **container** (the encrypted
// blob), decrypts it with the keystream of the contract, decodes the
// instruction stream and **executes** it in a small interpreter written
// from the documented contract (README §14.1-§14.6 and `src/isa.def`).
//
// The interpreter is deliberately **independent** of the encoder: it is
// written from the documentation, so a disagreement between the two is a
// real disagreement and not a shared bug. The mixer, the key derivation,
// the keystream and the inverse of the opcode permutation are all written
// here, from the contract, and not taken from the plugin's own code.
//
// The cases are the ones a shape-based test cannot see:
//
//   * the negation (`sub 0, x`), which the encoder rewrites;
//   * the switch, which is lowered to a chain of comparisons;
//   * the swap of a **conditional** back edge, whose moves belong to the
//     edge and not to the block;
//   * a call that returns an `i1`, whose low bit is the only defined bit;
//   * the slot budget at the limit, where a missing mask would change a
//     value;
//   * a local variable, which lives in the frame arena.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include "vm.hpp"

namespace {

int g_failures = 0;

void fail(const std::string& what) {
  std::fprintf(stderr, "FAIL: %s\n", what.c_str());
  ++g_failures;
}

void expect(bool condition, const std::string& what) {
  if (!condition) {
    fail(what);
  }
}

// --- The contract, written out ------------------------------------------------
//
// The three primitives of the cipher, written from the contract (README
// §14.5 and `src/cipher.hpp`): the mixer, the seeded stream and the
// keystream of the container.
//
// The mixer is **per build**: its multiplier, its xor constant and its
// rotation come from the module's seed. The contract says how, and this
// copy is written from the contract and not from the plugin's code.

constexpr std::uint64_t k_mixer_salt = 0x4D495845525F5345ULL;

/// The mixer of a build.
struct Mixer {
  std::uint64_t multiplier{1};
  std::uint64_t xor_constant{1};
  unsigned rotate{31};

  auto mix64(std::uint64_t value) const noexcept -> std::uint64_t {
    value *= multiplier;
    value ^= xor_constant;
    return (value << rotate) | (value >> (64U - rotate));
  }
};

auto splitmix64(std::uint64_t& state) noexcept -> std::uint64_t {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t value = state;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

/// The mixer of a build, from the seed.
auto mixer_for(std::uint64_t seed) noexcept -> Mixer {
  std::uint64_t state = seed ^ k_mixer_salt;
  Mixer mixer;
  mixer.multiplier = splitmix64(state) | 1ULL;
  mixer.xor_constant = splitmix64(state);
  if (mixer.xor_constant == 0) {
    mixer.xor_constant = 1;
  }
  mixer.rotate = 1U + static_cast<unsigned>(splitmix64(state) % 63ULL);
  return mixer;
}

/// The key of the container: the three parts of the key, combined.
///
/// The parts must differ: two equal parts cancel each other in the xor.
auto key_of(std::uint64_t seed) noexcept -> std::uint64_t {
  std::uint64_t state = seed;
  std::uint64_t parts[3] = {0, 0, 0};
  for (std::size_t index = 0; index < 3; ++index) {
    parts[index] = splitmix64(state);
    if (parts[index] == 0) {
      parts[index] = 1;
    }
  }
  for (std::size_t index = 0; index < 3; ++index) {
    for (std::size_t other = index + 1; other < 3; ++other) {
      while (parts[index] == parts[other]) {
        parts[other] = splitmix64(state);
        if (parts[other] == 0) {
          parts[other] = 1;
        }
      }
    }
  }
  return mixer_for(seed).mix64(parts[0] ^ parts[1] ^ parts[2]);
}

/// The byte of the keystream at the offset `position`.
auto keystream_byte(const Mixer& mixer, std::uint64_t key, std::size_t position) noexcept
    -> std::uint8_t {
  const std::uint64_t word = position / 8U;
  const std::uint64_t mixed = mixer.mix64(key + word * mixer.multiplier);
  return static_cast<std::uint8_t>((mixed >> (8U * (position % 8U))) & 0xFFU);
}

/// The inverse of the opcode permutation, with the extended Euclidean
/// algorithm.
auto modular_inverse(std::uint64_t value, std::uint64_t modulus) noexcept -> std::uint64_t {
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

// --- The width model ----------------------------------------------------------

/// The value of a width, zero-extended.
auto mask_to(std::uint64_t value, std::uint8_t bytes) noexcept -> std::uint64_t {
  if (bytes >= 8U) {
    return value;
  }
  const std::uint64_t bits = static_cast<std::uint64_t>(bytes) * 8U;
  return value & ((1ULL << bits) - 1ULL);
}

/// The value of a width, read as a signed number.
auto signed_of(std::uint64_t value, std::uint8_t bytes) noexcept -> std::int64_t {
  const std::uint64_t bits = static_cast<std::uint64_t>(bytes) * 8U;
  if (bits >= 64U) {
    return static_cast<std::int64_t>(value);
  }
  return static_cast<std::int64_t>(value << (64U - bits)) >> (64U - bits);
}

// --- The decoded instruction --------------------------------------------------

/// The name of an opcode, from the ISA itself.
///
/// The names are not part of the contract's *values*, but a failure message
/// that names the opcode is a failure message that can be acted on.
auto name_of(cs2::protect::VmOpcode opcode) -> const char* {
#define CS2_VM_OPCODE(name, number, comment)    \
  if (opcode == cs2::protect::VmOpcode::name) { \
    return #name;                               \
  }
#include "isa.def"
#undef CS2_VM_OPCODE
  return "?";
}

/// One instruction of the container, decoded.
///
/// The encoding is the contract (README §14.2): 24 bytes, little endian.
struct Decoded {
  std::uint8_t opcode{0};
  std::uint8_t width{0};
  std::uint8_t flags{0};
  std::uint16_t dst{0};
  std::uint16_t src1{0};
  std::uint16_t src2{0};
  std::uint16_t src3{0};
  std::uint32_t aux{0};
  std::uint64_t imm{0};
};

auto read_le16(const std::vector<std::uint8_t>& buffer, std::size_t offset) -> std::uint16_t {
  return static_cast<std::uint16_t>(buffer[offset]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(buffer[offset + 1]) << 8U);
}

auto read_le32(const std::vector<std::uint8_t>& buffer, std::size_t offset) -> std::uint32_t {
  std::uint32_t value = 0;
  for (std::size_t byte = 0; byte < 4; ++byte) {
    value |= static_cast<std::uint32_t>(buffer[offset + byte]) << (8U * byte);
  }
  return value;
}

auto read_le64(const std::vector<std::uint8_t>& buffer, std::size_t offset) -> std::uint64_t {
  std::uint64_t value = 0;
  for (std::size_t byte = 0; byte < 8; ++byte) {
    value |= static_cast<std::uint64_t>(buffer[offset + byte]) << (8U * byte);
  }
  return value;
}

// --- The interpreter ----------------------------------------------------------

/// A native function of the test: the arguments in the order of the call.
using Native = std::function<std::uint64_t(const std::vector<std::uint64_t>&)>;

/// The interpreter of the bytecode.
///
/// The frame is a flat array of cells, and it is a **constant**: the
/// rewritten function allocates exactly this, so the slot indices are
/// absolute. The frame is real memory here too: a local variable is a
/// byte offset into it, and a load and a store are real loads and stores.
class Interpreter {
 public:
  Interpreter(const cs2::protect::VmHeader& header, const std::vector<Decoded>& code,
              const std::map<std::string, Native>& natives)
      : header_(header), code_(code), natives_(natives), frame_(cs2::protect::k_frame_bytes, 0) {}

  /// Runs the bytecode with the arguments, and returns the value of the
  /// return slot.
  auto run(const std::vector<std::uint64_t>& arguments) -> std::uint64_t {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      write_slot(index, arguments[index]);
    }
    std::size_t pc = 0;
    for (std::uint64_t steps = 0; steps < 1000000; ++steps) {
      if (pc >= code_.size()) {
        fail("the interpreter ran off the end of the stream");
        return 0;
      }
      const Decoded& instruction = code_[pc];
      switch (static_cast<cs2::protect::VmOpcode>(instruction.opcode)) {
        case cs2::protect::VmOpcode::Const:
          write_slot(instruction.dst, mask_to(instruction.imm, instruction.width));
          ++pc;
          break;
        case cs2::protect::VmOpcode::Move:
          write_slot(instruction.dst, mask_to(read_slot(instruction.src1), instruction.width));
          ++pc;
          break;
        case cs2::protect::VmOpcode::Add:
        case cs2::protect::VmOpcode::Sub:
        case cs2::protect::VmOpcode::Mul:
        case cs2::protect::VmOpcode::And:
        case cs2::protect::VmOpcode::Or:
        case cs2::protect::VmOpcode::Xor:
        case cs2::protect::VmOpcode::SDiv:
        case cs2::protect::VmOpcode::UDiv:
        case cs2::protect::VmOpcode::SRem:
        case cs2::protect::VmOpcode::URem:
        case cs2::protect::VmOpcode::Shl:
        case cs2::protect::VmOpcode::LShr:
        case cs2::protect::VmOpcode::AShr:
        case cs2::protect::VmOpcode::Neg:
        case cs2::protect::VmOpcode::Not: {
          const std::uint64_t left = read_slot(instruction.src1);
          const std::uint64_t right =
              instruction.src2 == cs2::protect::k_no_slot ? 0 : read_slot(instruction.src2);
          std::uint64_t result = 0;
          switch (static_cast<cs2::protect::VmOpcode>(instruction.opcode)) {
            case cs2::protect::VmOpcode::Add:
              result = left + right;
              break;
            case cs2::protect::VmOpcode::Sub:
              result = left - right;
              break;
            case cs2::protect::VmOpcode::Mul:
              result = left * right;
              break;
            case cs2::protect::VmOpcode::And:
              result = left & right;
              break;
            case cs2::protect::VmOpcode::Or:
              result = left | right;
              break;
            case cs2::protect::VmOpcode::Xor:
              result = left ^ right;
              break;
            case cs2::protect::VmOpcode::SDiv: {
              const auto a = signed_of(left, instruction.width);
              const auto b = signed_of(right, instruction.width);
              result = b == 0 ? 0 : static_cast<std::uint64_t>(a / b);
              break;
            }
            case cs2::protect::VmOpcode::UDiv:
              result = right == 0
                           ? 0
                           : mask_to(left, instruction.width) / mask_to(right, instruction.width);
              break;
            case cs2::protect::VmOpcode::SRem: {
              const auto a = signed_of(left, instruction.width);
              const auto b = signed_of(right, instruction.width);
              result = b == 0 ? 0 : static_cast<std::uint64_t>(a % b);
              break;
            }
            case cs2::protect::VmOpcode::URem:
              result = right == 0
                           ? 0
                           : mask_to(left, instruction.width) % mask_to(right, instruction.width);
              break;
            case cs2::protect::VmOpcode::Shl: {
              // The amount is masked to `width * 8 - 1`: the width is in
              // bytes, so the mask is 7, 15, 31 or 63.
              const std::uint64_t bits = static_cast<std::uint64_t>(instruction.width) * 8U;
              result = mask_to(left, bits) << (right & (bits - 1U));
              break;
            }
            case cs2::protect::VmOpcode::LShr: {
              const std::uint64_t bits = static_cast<std::uint64_t>(instruction.width) * 8U;
              result = mask_to(left, bits) >> (right & (bits - 1U));
              break;
            }
            case cs2::protect::VmOpcode::AShr: {
              const std::uint64_t bits = static_cast<std::uint64_t>(instruction.width) * 8U;
              result = static_cast<std::uint64_t>(signed_of(left, instruction.width) >>
                                                  (right & (bits - 1U)));
              break;
            }
            case cs2::protect::VmOpcode::Neg:
              result = 0ULL - left;
              break;
            case cs2::protect::VmOpcode::Not:
              result = ~left;
              break;
            default:
              break;
          }
          write_slot(instruction.dst, mask_to(result, instruction.width));
          ++pc;
          break;
        }
        case cs2::protect::VmOpcode::Cmp: {
          const std::uint64_t left = mask_to(read_slot(instruction.src1), instruction.width);
          const std::uint64_t right = mask_to(read_slot(instruction.src2), instruction.width);
          const auto a = signed_of(left, instruction.width);
          const auto b = signed_of(right, instruction.width);
          bool holds = false;
          switch (static_cast<cs2::protect::VmCompare>(instruction.aux)) {
            case cs2::protect::VmCompare::eq:
              holds = left == right;
              break;
            case cs2::protect::VmCompare::ne:
              holds = left != right;
              break;
            case cs2::protect::VmCompare::ugt:
              holds = left > right;
              break;
            case cs2::protect::VmCompare::uge:
              holds = left >= right;
              break;
            case cs2::protect::VmCompare::ult:
              holds = left < right;
              break;
            case cs2::protect::VmCompare::ule:
              holds = left <= right;
              break;
            case cs2::protect::VmCompare::sgt:
              holds = a > b;
              break;
            case cs2::protect::VmCompare::sge:
              holds = a >= b;
              break;
            case cs2::protect::VmCompare::slt:
              holds = a < b;
              break;
            case cs2::protect::VmCompare::sle:
              holds = a <= b;
              break;
          }
          write_slot(instruction.dst, holds ? 1U : 0U);
          ++pc;
          break;
        }
        case cs2::protect::VmOpcode::Cast: {
          const auto kind = static_cast<cs2::protect::VmCast>(instruction.aux & 0xFFU);
          const auto source_width = static_cast<std::uint8_t>((instruction.aux >> 8U) & 0xFFU);
          const std::uint64_t source = read_slot(instruction.src1);
          std::uint64_t result = 0;
          switch (kind) {
            case cs2::protect::VmCast::trunc:
              result = mask_to(source, instruction.width);
              break;
            case cs2::protect::VmCast::bitcast:
              result = mask_to(source, instruction.width);
              break;
            case cs2::protect::VmCast::zext:
              result = mask_to(source, source_width);
              break;
            case cs2::protect::VmCast::sext:
              result = static_cast<std::uint64_t>(signed_of(source, source_width));
              break;
          }
          write_slot(instruction.dst, mask_to(result, instruction.width));
          ++pc;
          break;
        }
        case cs2::protect::VmOpcode::Load: {
          const std::uint64_t address = read_slot(instruction.src1);
          std::uint64_t value = 0;
          std::memcpy(&value, reinterpret_cast<const void*>(address), instruction.aux);
          write_slot(instruction.dst, mask_to(value, instruction.width));
          ++pc;
          break;
        }
        case cs2::protect::VmOpcode::Store: {
          const std::uint64_t address = read_slot(instruction.src1);
          const std::uint64_t value = read_slot(instruction.src2);
          std::memcpy(reinterpret_cast<void*>(address), &value, instruction.aux);
          ++pc;
          break;
        }
        case cs2::protect::VmOpcode::Gep: {
          const std::uint64_t base = read_slot(instruction.src1);
          const auto offset = static_cast<std::int64_t>(instruction.imm);
          write_slot(instruction.dst,
                     static_cast<std::uint64_t>(static_cast<std::int64_t>(base) + offset));
          ++pc;
          break;
        }
        case cs2::protect::VmOpcode::Br:
          pc = instruction.aux;
          break;
        case cs2::protect::VmOpcode::BrIf:
          pc = read_slot(instruction.src1) != 0 ? instruction.aux : pc + 1U;
          break;
        case cs2::protect::VmOpcode::Arg:
          push(read_slot(instruction.src1));
          ++pc;
          break;
        case cs2::protect::VmOpcode::Pop:
          write_slot(instruction.dst, pop());
          ++pc;
          break;
        case cs2::protect::VmOpcode::Call: {
          const std::size_t count = instruction.aux;
          if (stack_depth_ < count) {
            fail("the interpreter called with too few arguments on the stack");
            return 0;
          }
          // The arguments are the top cells of the argument stack, in the
          // order they were pushed: the first argument of the call is the
          // first one pushed.
          std::vector<std::uint64_t> call_arguments;
          call_arguments.reserve(count);
          for (std::size_t index = stack_depth_ - count; index < stack_depth_; ++index) {
            call_arguments.push_back(read_slot(static_cast<std::uint16_t>(stack_cell(index))));
          }
          stack_depth_ -= count;
          const auto found = natives_.find(call_target_);
          std::uint64_t result = 0;
          if (found == natives_.end()) {
            fail("the interpreter has no native for '" + call_target_ + "'");
          } else {
            result = found->second(call_arguments);
          }
          if (instruction.dst != cs2::protect::k_no_slot) {
            write_slot(instruction.dst, mask_to(result, instruction.width));
          }
          ++pc;
          break;
        }
        case cs2::protect::VmOpcode::Ret:
          return read_slot(instruction.src1);
        case cs2::protect::VmOpcode::MemCpy: {
          const std::uint64_t destination = read_slot(instruction.src1);
          const std::uint64_t source = read_slot(instruction.src2);
          std::memcpy(reinterpret_cast<void*>(destination), reinterpret_cast<const void*>(source),
                      static_cast<std::size_t>(instruction.imm));
          ++pc;
          break;
        }
        case cs2::protect::VmOpcode::MemSet: {
          const std::uint64_t destination = read_slot(instruction.src1);
          std::memset(reinterpret_cast<void*>(destination),
                      static_cast<int>(read_slot(instruction.src2) & 0xFFU),
                      static_cast<std::size_t>(instruction.imm));
          ++pc;
          break;
        }
        case cs2::protect::VmOpcode::Pool:
          // The pool holds the relocatable addresses, and the test does
          // not map the image: the entry is zero here.
          write_slot(instruction.dst, 0);
          ++pc;
          break;
        case cs2::protect::VmOpcode::Alloca:
          // The local is a byte offset into the frame arena: the
          // interpreter adds the base of the frame to it.
          write_slot(instruction.dst,
                     static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(frame_.data())) +
                         instruction.imm);
          ++pc;
          break;
      }
    }
    fail("the interpreter did not return");
    return 0;
  }

  /// The name of the callee of the next call: the test sets it before the
  /// run, because the pool of the container is not readable here.
  void set_call_target(std::string target) {
    call_target_ = std::move(target);
  }

 private:
  auto read_slot(std::uint16_t slot) const -> std::uint64_t {
    if (slot == cs2::protect::k_no_slot || slot >= cs2::protect::k_frame_cells) {
      fail("the interpreter read the slot " + std::to_string(slot));
      return 0;
    }
    std::uint64_t value = 0;
    std::memcpy(&value, frame_.data() + static_cast<std::size_t>(slot) * 8U, sizeof(value));
    return value;
  }

  void write_slot(std::uint16_t slot, std::uint64_t value) {
    if (slot == cs2::protect::k_no_slot) {
      return;  // "no slot": the instruction has no destination
    }
    if (slot >= cs2::protect::k_frame_cells) {
      fail("the interpreter wrote the slot " + std::to_string(slot));
      return;
    }
    std::memcpy(frame_.data() + static_cast<std::size_t>(slot) * 8U, &value, sizeof(value));
  }

  /// The argument stack is a region of the frame: the cells above the
  /// value slots and the locals.
  auto stack_cell(std::size_t depth) const -> std::size_t {
    return header_.slot_count + header_.local_cells + depth;
  }

  void push(std::uint64_t value) {
    if (stack_depth_ >= header_.arg_slots) {
      fail("the argument stack overflowed");
      return;
    }
    write_slot(static_cast<std::uint16_t>(stack_cell(stack_depth_)), value);
    ++stack_depth_;
  }

  auto pop() -> std::uint64_t {
    if (stack_depth_ == 0) {
      fail("the interpreter popped an empty stack");
      return 0;
    }
    --stack_depth_;
    return read_slot(static_cast<std::uint16_t>(stack_cell(stack_depth_)));
  }

  cs2::protect::VmHeader header_;
  const std::vector<Decoded>& code_;
  const std::map<std::string, Native>& natives_;
  std::vector<std::uint8_t> frame_;
  std::size_t stack_depth_{0};
  std::string call_target_;
};

// --- The helpers --------------------------------------------------------------

/// The seed of the tests: the key and the permutation come from it.
constexpr std::uint64_t k_seed = 0x5EED5EED5EED5EEDULL;

/// The IR of a module, parsed.
auto parse(const std::string& ir) -> std::unique_ptr<llvm::Module> {
  static llvm::LLVMContext context;  // the context outlives the module
  llvm::SMDiagnostic error;
  auto module = llvm::parseAssemblyString(ir, error, context);
  if (module == nullptr) {
    std::string message;
    llvm::raw_string_ostream stream(message);
    error.print("vm-values-test", stream);
    fail("the IR does not parse: " + stream.str());
    return nullptr;
  }
  return module;
}

/// The container of a function, decrypted and decoded.
///
/// The keystream and the inverse of the permutation are the ones of the
/// contract, not the plugin's.
auto decode(const cs2::protect::VmFunction& function, std::vector<Decoded>& code)
    -> cs2::protect::VmHeader {
  const Mixer mixer = mixer_for(k_seed);
  const std::uint64_t key = key_of(k_seed);
  expect(key == function.key, "the key of the module is not the key of the contract");

  std::vector<std::uint8_t> plain = function.blob;
  for (std::size_t index = 0; index < plain.size(); ++index) {
    plain[index] ^= keystream_byte(mixer, key, index);
  }

  cs2::protect::VmHeader header;
  header.magic = read_le32(plain, 0);
  header.version = read_le32(plain, 4);
  header.slot_count = read_le32(plain, 8);
  header.arg_slots = read_le32(plain, 12);
  header.call_slots = read_le32(plain, 16);
  header.instruction_count = read_le32(plain, 20);
  header.flags = read_le32(plain, 24);
  header.local_cells = read_le32(plain, 28);

  const std::uint64_t count = cs2::protect::k_opcode_count;
  const std::uint64_t inverse = modular_inverse(function.perm_a % count, count);
  code.clear();
  for (std::size_t index = 0; index < header.instruction_count; ++index) {
    const std::size_t offset =
        cs2::protect::k_header_bytes + index * cs2::protect::k_instruction_bytes;
    Decoded instruction;
    const std::uint64_t permuted = plain[offset];
    instruction.opcode = static_cast<std::uint8_t>(
        ((permuted + count - (function.perm_b % count)) * inverse) % count);
    instruction.width = plain[offset + 1];
    instruction.flags = plain[offset + 2];
    instruction.dst = read_le16(plain, offset + 4);
    instruction.src1 = read_le16(plain, offset + 6);
    instruction.src2 = read_le16(plain, offset + 8);
    instruction.src3 = read_le16(plain, offset + 10);
    instruction.aux = read_le32(plain, offset + 12);
    instruction.imm = read_le64(plain, offset + 16);
    code.push_back(instruction);
  }
  return header;
}

/// The widths of the contract: 1, 2, 4 or 8 bytes, and **never 0**.
///
/// The width of an instruction is the width of the value it produces, and
/// an instruction that produces no value carries the width of a cell of the
/// frame (8), except `MemCpy` and `MemSet`, which are byte-wise (1). A
/// width of 0 would make an interpreter that truncates its result to the
/// width return zero from every function.
void check_the_widths(const cs2::protect::VmFunction& function, const std::string& what) {
  std::vector<Decoded> code;
  decode(function, code);
  for (std::size_t index = 0; index < code.size(); ++index) {
    const Decoded& instruction = code[index];
    const auto opcode = static_cast<cs2::protect::VmOpcode>(instruction.opcode);
    const std::string where =
        what + ": the instruction " + std::to_string(index) + " (" + name_of(opcode) + ")";
    expect(instruction.width == 1 || instruction.width == 2 || instruction.width == 4 ||
               instruction.width == 8,
           where + " carries the width " + std::to_string(instruction.width) +
               ", and not one of 1, 2, 4 or 8");
    switch (opcode) {
      case cs2::protect::VmOpcode::Br:
      case cs2::protect::VmOpcode::BrIf:
      case cs2::protect::VmOpcode::Arg:
      case cs2::protect::VmOpcode::Pop:
        expect(instruction.width == 8, where + " produces no value: the width must be 8, and not " +
                                           std::to_string(instruction.width));
        break;
      case cs2::protect::VmOpcode::Pool:
        expect(instruction.width == 8, where + " yields a pointer: the width must be 8, and not " +
                                           std::to_string(instruction.width));
        break;
      case cs2::protect::VmOpcode::MemCpy:
      case cs2::protect::VmOpcode::MemSet:
        expect(instruction.width == 1, where + " is byte-wise: the width must be 1, and not " +
                                           std::to_string(instruction.width));
        break;
      default:
        break;
    }
  }
}

/// The destinations of the contract.
///
/// An instruction that produces a value has a destination, and an
/// instruction that produces no value has none: `0xFFFF` is the encoding of
/// "no slot", and the interpreter does not have to special-case a
/// destination-less instruction that produces a value.
void check_the_destinations(const cs2::protect::VmFunction& function, const std::string& what) {
  for (std::size_t index = 0; index < function.code.size(); ++index) {
    const cs2::protect::VmInstruction& instruction = function.code[index];
    const std::string where = what + ": the instruction " + std::to_string(index) + " (" +
                              name_of(instruction.opcode) + ")";
    switch (instruction.opcode) {
      case cs2::protect::VmOpcode::Br:
      case cs2::protect::VmOpcode::BrIf:
      case cs2::protect::VmOpcode::Arg:
      case cs2::protect::VmOpcode::Store:
      case cs2::protect::VmOpcode::MemCpy:
      case cs2::protect::VmOpcode::MemSet:
      case cs2::protect::VmOpcode::Ret:
        expect(instruction.dst == cs2::protect::k_no_slot,
               where + " produces no value: it must have no destination");
        break;
      case cs2::protect::VmOpcode::Const:
      case cs2::protect::VmOpcode::Move:
      case cs2::protect::VmOpcode::Add:
      case cs2::protect::VmOpcode::Sub:
      case cs2::protect::VmOpcode::Mul:
      case cs2::protect::VmOpcode::And:
      case cs2::protect::VmOpcode::Or:
      case cs2::protect::VmOpcode::Xor:
      case cs2::protect::VmOpcode::SDiv:
      case cs2::protect::VmOpcode::UDiv:
      case cs2::protect::VmOpcode::SRem:
      case cs2::protect::VmOpcode::URem:
      case cs2::protect::VmOpcode::Shl:
      case cs2::protect::VmOpcode::LShr:
      case cs2::protect::VmOpcode::AShr:
      case cs2::protect::VmOpcode::Neg:
      case cs2::protect::VmOpcode::Not:
      case cs2::protect::VmOpcode::Cmp:
      case cs2::protect::VmOpcode::Cast:
      case cs2::protect::VmOpcode::Load:
      case cs2::protect::VmOpcode::Gep:
      case cs2::protect::VmOpcode::Pop:
      case cs2::protect::VmOpcode::Pool:
      case cs2::protect::VmOpcode::Alloca:
        expect(instruction.dst != cs2::protect::k_no_slot,
               where + " produces a value: it must have a destination");
        break;
      case cs2::protect::VmOpcode::Call:
        // A call that returns nothing has no destination; a call that
        // returns something has one.
        break;
    }
  }
}

/// The function of a module, lowered.
auto analyze(llvm::Module& module, const std::string& name) -> cs2::protect::VmFunction {
  llvm::Function* function = module.getFunction(name);
  if (function == nullptr) {
    fail("the module has no function '" + name + "'");
    return {};
  }
  cs2::protect::VmOptions options;
  options.seed = k_seed;
  const auto result = cs2::protect::analyze_function(*function, options);
  if (result.virtualizable) {
    // Every instruction of every fixture carries a width of the contract.
    check_the_widths(result, name);
    check_the_destinations(result, name);
  }
  return result;
}

/// The first instruction of an opcode, or null when there is none.
auto first_of(const cs2::protect::VmFunction& function, cs2::protect::VmOpcode opcode)
    -> const cs2::protect::VmInstruction* {
  for (const cs2::protect::VmInstruction& instruction : function.code) {
    if (instruction.opcode == opcode) {
      return &instruction;
    }
  }
  return nullptr;
}

/// Every instruction of an opcode, in order.
auto all_of(const cs2::protect::VmFunction& function, cs2::protect::VmOpcode opcode)
    -> std::vector<const cs2::protect::VmInstruction*> {
  std::vector<const cs2::protect::VmInstruction*> instructions;
  for (const cs2::protect::VmInstruction& instruction : function.code) {
    if (instruction.opcode == opcode) {
      instructions.push_back(&instruction);
    }
  }
  return instructions;
}

/// Runs the bytecode of a function and returns the value it computes.
auto run_of(const cs2::protect::VmFunction& function, const std::vector<std::uint64_t>& arguments,
            const std::map<std::string, Native>& natives = {}, const std::string& call_target = {})
    -> std::uint64_t {
  std::vector<Decoded> code;
  const auto header = decode(function, code);
  Interpreter interpreter(header, code, natives);
  interpreter.set_call_target(call_target);
  return interpreter.run(arguments);
}

// --- The tests ----------------------------------------------------------------

/// The container: the header and every field of every instruction survive
/// the encryption.
void test_the_container_round_trip() {
  static const std::string k_ir = R"(
define i32 @leaf(i32 %x) {
entry:
  %sum = add i32 %x, 1
  %product = mul i32 %sum, 3
  ret i32 %product
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }
  const auto function = analyze(*module, "leaf");
  expect(function.virtualizable, "the leaf is refused");
  if (!function.virtualizable) {
    return;
  }

  // The container is encrypted: not one byte of the header is in the clear.
  expect(
      function.blob[0] != static_cast<std::uint8_t>(cs2::protect::k_vm_magic & 0xFFU) ||
          function.blob[1] != static_cast<std::uint8_t>((cs2::protect::k_vm_magic >> 8U) & 0xFFU) ||
          function.blob[2] !=
              static_cast<std::uint8_t>((cs2::protect::k_vm_magic >> 16U) & 0xFFU) ||
          function.blob[3] != static_cast<std::uint8_t>((cs2::protect::k_vm_magic >> 24U) & 0xFFU),
      "the container is not encrypted: the magic is in the clear");

  std::vector<Decoded> code;
  const auto header = decode(function, code);
  expect(header.magic == cs2::protect::k_vm_magic, "the decrypted magic is wrong");
  expect(header.version == cs2::protect::k_vm_version, "the decrypted version is wrong");
  expect(header.slot_count == function.slots, "the decrypted slot count is wrong");
  expect(header.arg_slots == cs2::protect::k_frame_argument_slots,
         "the decrypted argument region is wrong");
  expect(header.call_slots == cs2::protect::k_frame_call_slots,
         "the decrypted call scratch is wrong");
  expect(header.instruction_count == function.instructions,
         "the decrypted instruction count is wrong");
  expect(header.local_cells == function.local_cells, "the decrypted local count is wrong");
  expect(code.size() == function.code.size(), "the decoded stream has the wrong length");

  // Every field of every instruction comes back.
  for (std::size_t index = 0; index < code.size() && index < function.code.size(); ++index) {
    const Decoded& decoded = code[index];
    const cs2::protect::VmInstruction& encoded = function.code[index];
    const std::string where = "the instruction " + std::to_string(index);
    expect(decoded.opcode == static_cast<std::uint8_t>(encoded.opcode),
           where + ": the opcode does not survive the round trip");
    expect(decoded.width == encoded.width, where + ": the width does not survive");
    expect(decoded.flags == encoded.flags, where + ": the flags do not survive");
    expect(decoded.dst == encoded.dst, where + ": the destination does not survive");
    expect(decoded.src1 == encoded.src1, where + ": the first source does not survive");
    expect(decoded.src2 == encoded.src2, where + ": the second source does not survive");
    expect(decoded.src3 == encoded.src3, where + ": the third source does not survive");
    expect(decoded.aux == encoded.aux, where + ": the auxiliary word does not survive");
    expect(decoded.imm == encoded.imm, where + ": the immediate does not survive");
  }
}

/// The arithmetic, the casts and the comparisons: the values.
void test_the_arithmetic() {
  static const std::string k_ir = R"(
define i32 @arith(i32 %x) {
entry:
  %sum = add i32 %x, 5
  %diff = sub i32 %sum, 3
  %product = mul i32 %diff, 2
  %bits = and i32 %product, 255
  %or = or i32 %bits, 16
  %xor = xor i32 %or, 1
  %neg = sub i32 3, %xor
  %inverted = xor i32 %neg, -1
  %div = sdiv i32 %inverted, 4
  %rem = srem i32 %inverted, 7
  %udiv = udiv i32 %inverted, 4
  %urem = urem i32 %inverted, 7
  %shl = shl i32 %udiv, 3
  %lshr = lshr i32 %urem, 2
  %ashr = ashr i32 %div, 1
  %a = add i32 %shl, %lshr
  %b = add i32 %a, %ashr
  %c = add i32 %b, %rem
  ret i32 %c
}

define i32 @casts(i32 %x) {
entry:
  %wide = zext i32 %x to i64
  %signed = sext i32 %x to i64
  %sum = add i64 %wide, %signed
  %narrow = trunc i64 %sum to i32
  %small = trunc i32 %narrow to i8
  %back = zext i8 %small to i32
  %result = add i32 %back, 0
  ret i32 %result
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }

  {
    const auto function = analyze(*module, "arith");
    expect(function.virtualizable, "the arithmetic is refused");
    if (!function.virtualizable) {
      return;
    }
    auto reference = [](std::uint32_t x) -> std::uint32_t {
      const std::uint32_t sum = x + 5U;
      const std::uint32_t diff = sum - 3U;
      const std::uint32_t product = diff * 2U;
      const std::uint32_t bits = product & 255U;
      const std::uint32_t ored = bits | 16U;
      const std::uint32_t xored = ored ^ 1U;
      const std::uint32_t neg = 3U - xored;
      const std::uint32_t inverted = neg ^ 0xFFFFFFFFU;
      const auto signed_not = static_cast<std::int32_t>(inverted);
      const std::uint32_t div = static_cast<std::uint32_t>(signed_not / 4);
      const std::uint32_t rem = static_cast<std::uint32_t>(signed_not % 7);
      const std::uint32_t udiv = inverted / 4U;
      const std::uint32_t urem = inverted % 7U;
      const std::uint32_t shl = udiv << 3U;
      const std::uint32_t lshr = urem >> 2U;
      const std::uint32_t ashr = static_cast<std::uint32_t>(div >> 1);
      return shl + lshr + ashr + rem;
    };
    for (std::uint32_t input : {0U, 1U, 7U, 1000U, 0x80000000U}) {
      const auto result = static_cast<std::uint32_t>(run_of(function, {input}));
      const auto expected = reference(input);
      expect(result == expected, "the arithmetic of " + std::to_string(input) +
                                     ": the bytecode returned " + std::to_string(result) +
                                     " and not " + std::to_string(expected));
    }
  }

  {
    const auto function = analyze(*module, "casts");
    expect(function.virtualizable, "the casts are refused");
    if (!function.virtualizable) {
      return;
    }
    for (std::uint32_t input : {0U, 5U, 0xFFFFFFFFU}) {
      // The sum is truncated to an `i8` and back: the value is its low byte.
      const auto expected = static_cast<std::uint32_t>((input + input) & 0xFFU);
      const auto result = static_cast<std::uint32_t>(run_of(function, {input}));
      expect(result == expected, "the casts of " + std::to_string(input) +
                                     ": the bytecode returned " + std::to_string(result) +
                                     " and not " + std::to_string(expected));
    }
  }
}

/// The negation: `sub 0, x` is a negation, and the opcode the encoder
/// emits takes the subtrahend.
void test_the_negation() {
  static const std::string k_ir = R"(
define i32 @negate(i32 %x) {
entry:
  %r = sub i32 0, %x
  ret i32 %r
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }
  const auto function = analyze(*module, "negate");
  expect(function.virtualizable, "the negation is refused");
  if (!function.virtualizable) {
    return;
  }
  const auto result = static_cast<std::uint32_t>(run_of(function, {5}));
  expect(result == 0xFFFFFFFBU, "the negation of 5: the bytecode returned " +
                                    std::to_string(result) + " and not 4294967291");
}

/// The switch: a chain of comparisons, and the moves of every case belong
/// to the case's own edge.
void test_the_switch() {
  static const std::string k_ir = R"(
define i32 @switch_phis(i32 %x) {
entry:
  switch i32 %x, label %default [ i32 0, label %case0
                                i32 1, label %case1 ]

case0:
  %a = phi i32 [ 10, %entry ]
  br label %merge

case1:
  %b = phi i32 [ 20, %entry ]
  br label %merge

default:
  %c = phi i32 [ 30, %entry ]
  br label %merge

merge:
  %m = phi i32 [ %a, %case0 ], [ %b, %case1 ], [ %c, %default ]
  ret i32 %m
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }
  const auto function = analyze(*module, "switch_phis");
  expect(function.virtualizable, "the switch is refused");
  if (!function.virtualizable) {
    return;
  }
  for (std::uint64_t input = 0; input < 4; ++input) {
    const auto expected = input < 2 ? 10U + 10U * input : 30U;
    const auto result = static_cast<std::uint32_t>(run_of(function, {input}));
    expect(result == expected, "the switch of " + std::to_string(input) +
                                   ": the bytecode returned " + std::to_string(result) +
                                   " and not " + std::to_string(expected));
  }
}

/// The swap of a **conditional** back edge.
///
/// The moves of the PHIs belong to the edge, so the swap happens on the
/// edge that goes back to the loop and on no other. The program returns
/// the value of `a` *before* the swap: the ground truth (the same IR,
/// compiled by the compiler) is 10.
void test_the_swap_on_a_conditional_back_edge() {
  static const std::string k_ir = R"(
define i32 @swap_exit(i32 %n) {
entry:
  br label %loop

loop:
  %a = phi i32 [ 10, %entry ], [ %b, %loop ]
  %b = phi i32 [ 20, %entry ], [ %a, %loop ]
  %cond = icmp slt i32 %a, %n
  br i1 %cond, label %loop, label %exit

exit:
  %r = phi i32 [ %a, %loop ]
  ret i32 %r
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }
  const auto function = analyze(*module, "swap_exit");
  expect(function.virtualizable, "the swapping back edge is refused");
  if (!function.virtualizable) {
    return;
  }
  const auto result = static_cast<std::uint32_t>(run_of(function, {0}));
  expect(result == 10U, "the swap of a conditional back edge: the bytecode returned " +
                            std::to_string(result) + " and not 10");
}

/// A call that returns an `i1`: the ABI defines the low bit of the byte
/// and nothing else.
void test_the_call_that_returns_a_bool() {
  static const std::string k_ir = R"(
declare i1 @flag(i32)

define i32 @uses_flag(i32 %x) {
entry:
  %value = call i1 @flag(i32 %x)
  %wide = zext i1 %value to i32
  ret i32 %wide
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }
  const auto function = analyze(*module, "uses_flag");
  expect(function.virtualizable, "the call that returns an i1 is refused");
  if (!function.virtualizable) {
    return;
  }
  // The native returns a byte whose bits above the low one are garbage.
  const std::map<std::string, Native> natives = {
      {"flag", [](const std::vector<std::uint64_t>&) { return 0xFEULL; }}};
  const auto result = run_of(function, {0}, natives, "flag");
  expect(result == 0, "the call that returns an i1: the bytecode returned " +
                          std::to_string(result) + " and not 0");
}

/// The arguments of a call are in the order of the call.
void test_the_order_of_the_arguments() {
  static const std::string k_ir = R"(
declare i32 @three(i32, i32, i32)

define i32 @caller(i32 %x) {
entry:
  %value = call i32 @three(i32 %x, i32 10, i32 100)
  ret i32 %value
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }
  const auto function = analyze(*module, "caller");
  expect(function.virtualizable, "the caller is refused");
  if (!function.virtualizable) {
    return;
  }
  const std::map<std::string, Native> natives = {
      {"three", [](const std::vector<std::uint64_t>& arguments) {
         // The thunk receives scratch[0..n-1]: the first argument first.
         std::uint64_t sum = 0;
         for (const std::uint64_t argument : arguments) {
           sum += argument;
         }
         return sum;
       }}};
  const auto result = run_of(function, {5}, natives, "three");
  expect(result == 115,
         "the call: the bytecode returned " + std::to_string(result) + " and not 115");
}

/// The slot budget at the limit: a function that needs the whole budget
/// is refused, and a function that fits computes the right values.
///
/// A mask of an `i1` that is silently skipped is a fail-open: the value
/// of the program changes.
void test_the_slot_budget() {
  for (int additions = 240; additions <= 252; ++additions) {
    std::string ir = "define i32 @wide(i32 %x) {\nentry:\n";
    std::string value = "%x";
    for (int index = 0; index < additions; ++index) {
      ir += "  %v" + std::to_string(index) + " = add i32 " + value + ", 1\n";
      value = "%v" + std::to_string(index);
    }
    ir += "  %cmp = icmp eq i32 " + value + ", 0\n";
    ir += "  %bit = and i1 %cmp, true\n";
    ir += "  %neg = sub i1 false, %bit\n";
    ir += "  %wide = zext i1 %neg to i32\n";
    ir += "  ret i32 %wide\n}\n";
    const auto module = parse(ir);
    if (module == nullptr) {
      return;
    }
    const auto function = analyze(*module, "wide");
    if (!function.virtualizable) {
      expect(function.failure.reason == cs2::protect::VmRefusal::too_many_slots,
             "the function at the limit is refused with '" +
                 std::string(cs2::protect::to_string(function.failure.reason)) + "'");
      continue;
    }
    // The program returns 1 when the sum is zero, and 0 otherwise.
    const auto zero = run_of(
        function,
        {static_cast<std::uint64_t>(static_cast<std::uint32_t>(-static_cast<int>(additions)))});
    const auto other = run_of(function, {0});
    expect(zero == 1, "the function at the limit returns " + std::to_string(zero) + " and not 1");
    expect(other == 0, "the function at the limit returns " + std::to_string(other) + " and not 0");
  }
}

/// A local variable: the local is a byte offset into the frame arena.
void test_the_local_variable() {
  static const std::string k_ir = R"(
define i32 @with_alloca(i32 %x) {
entry:
  %slot = alloca i32
  store i32 %x, ptr %slot
  %value = load i32, ptr %slot
  %doubled = mul i32 %value, 2
  store i32 %doubled, ptr %slot
  %read = load i32, ptr %slot
  ret i32 %read
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }
  const auto function = analyze(*module, "with_alloca");
  expect(function.virtualizable, "the function with a local is refused");
  if (!function.virtualizable) {
    return;
  }
  expect(function.local_cells == 1, "the local takes one cell of the frame");
  const auto result = static_cast<std::uint32_t>(run_of(function, {21}));
  expect(result == 42U,
         "the local variable: the bytecode returned " + std::to_string(result) + " and not 42");
}

/// The widths of the instructions.
///
/// The width is the width of the value the instruction produces, and it is
/// never 0: an instruction that produces no value carries the width of a
/// cell of the frame (8), the two byte-wise ones carry 1, and the `Ret`
/// carries the width of the value it returns (8 for a function that
/// returns nothing).
void test_the_widths() {
  static const std::string k_ir = R"(
@counter = global i32 0

declare i32 @add_one(i32)
declare void @poke()

define i32 @wide(i32 %x, i32 %y) {
entry:
  %slot = alloca i32
  %global = load i32, ptr @counter
  %called = call i32 @add_one(i32 %x)
  call void @poke()
  %chosen = select i1 true, i32 %called, i32 %global
  %bigger = icmp sgt i32 %chosen, %y
  br i1 %bigger, label %then, label %other

then:
  %a = phi i32 [ %chosen, %entry ]
  br label %merge

other:
  %b = phi i32 [ %global, %entry ]
  br label %merge

merge:
  %m = phi i32 [ %a, %then ], [ %b, %other ]
  store i32 %m, ptr %slot
  %read = load i32, ptr %slot
  ret i32 %read
}

define void @nothing() {
entry:
  ret void
}

define void @copies(ptr %destination, ptr %source) {
entry:
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %destination, ptr align 4 %source, i64 16, i1 false)
  call void @llvm.memset.p0.i64(ptr align 4 %destination, i8 0, i64 16, i1 false)
  ret void
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }

  {
    const auto function = analyze(*module, "wide");
    expect(function.virtualizable, "the function with every shape is refused");
    if (!function.virtualizable) {
      return;
    }
    // The pool yields a pointer.
    const auto* pool = first_of(function, cs2::protect::VmOpcode::Pool);
    expect(pool != nullptr, "the global is reached through the pool");
    if (pool != nullptr) {
      expect(pool->width == 8, "the pool yields a pointer: the width must be 8, and not " +
                                   std::to_string(pool->width));
    }
    // The two calls: one returns an `i32`, the other returns nothing.
    const auto calls = all_of(function, cs2::protect::VmOpcode::Call);
    expect(calls.size() == 2, "the function makes two calls");
    if (calls.size() == 2) {
      expect(calls[0]->width == 4, "the call that returns an i32 has the width 4, and not " +
                                       std::to_string(calls[0]->width));
      expect(calls[1]->width == 8, "the call that returns nothing has the width 8, and not " +
                                       std::to_string(calls[1]->width));
    }
    // The return carries the width of the value it returns.
    const auto* ret = first_of(function, cs2::protect::VmOpcode::Ret);
    expect(ret != nullptr, "the function returns");
    if (ret != nullptr) {
      expect(ret->width == 4, "the return of an i32 function has the width 4, and not " +
                                  std::to_string(ret->width));
    }
    // The branches, the pushes and the pops carry the width of a cell.
    for (const cs2::protect::VmOpcode opcode :
         {cs2::protect::VmOpcode::Br, cs2::protect::VmOpcode::BrIf, cs2::protect::VmOpcode::Arg,
          cs2::protect::VmOpcode::Pop}) {
      for (const cs2::protect::VmInstruction* instruction : all_of(function, opcode)) {
        expect(instruction->width == 8, std::string(name_of(opcode)) +
                                            " produces no value: the width must be 8, and not " +
                                            std::to_string(instruction->width));
      }
    }
  }

  {
    // A function that returns nothing returns zero, and the width of that
    // zero is the width of a cell.
    const auto function = analyze(*module, "nothing");
    expect(function.virtualizable, "the function that returns nothing is refused");
    if (!function.virtualizable) {
      return;
    }
    const auto* ret = first_of(function, cs2::protect::VmOpcode::Ret);
    expect(ret != nullptr, "the function returns");
    if (ret != nullptr) {
      expect(ret->width == 8, "the return of a void function has the width 8, and not " +
                                  std::to_string(ret->width));
    }
  }

  {
    // The two byte-wise instructions carry 1.
    const auto function = analyze(*module, "copies");
    expect(function.virtualizable, "the function with the copies is refused");
    if (!function.virtualizable) {
      return;
    }
    const auto* copy = first_of(function, cs2::protect::VmOpcode::MemCpy);
    expect(copy != nullptr, "the copy is lowered");
    if (copy != nullptr) {
      expect(copy->width == 1, "the memcpy is byte-wise: the width must be 1, and not " +
                                   std::to_string(copy->width));
    }
    const auto* set = first_of(function, cs2::protect::VmOpcode::MemSet);
    expect(set != nullptr, "the set is lowered");
    if (set != nullptr) {
      expect(set->width == 1,
             "the memset is byte-wise: the width must be 1, and not " + std::to_string(set->width));
    }
  }
}

/// The `unreachable` **stops** the interpreter.
///
/// The original is undefined behaviour, so the value does not matter: what
/// matters is that the bytecode does not fall out of the block and into the
/// next one. The ISA has no "stop" opcode, and the `Ret` is what stops the
/// interpreter.
void test_the_unreachable_stops() {
  static const std::string k_ir = R"(
define i32 @stops() {
entry:
  unreachable
}
)";
  const auto module = parse(k_ir);
  if (module == nullptr) {
    return;
  }
  const auto function = analyze(*module, "stops");
  expect(function.virtualizable, "the function with an unreachable is refused");
  if (!function.virtualizable) {
    return;
  }
  expect(!function.code.empty(), "the function has instructions");
  if (function.code.empty()) {
    return;
  }
  const auto& last = function.code.back();
  expect(last.opcode == cs2::protect::VmOpcode::Ret,
         "the unreachable lowers to a return, and not to " + std::string(name_of(last.opcode)));
  expect(last.width == 8 || last.width == 4,
         "the return of the unreachable carries the width of the returned value");
  // The interpreter stops at the return: it does not run off the end of
  // the stream.
  const auto result = run_of(function, {});
  expect(result == 0, "the unreachable returns zero, and not " + std::to_string(result));
}

/// The opcode permutation: a bijection of the whole opcode space, and a
/// multiplier that is odd and coprime with the number of the opcodes, for
/// every seed.
void test_the_permutation() {
  const std::size_t count = cs2::protect::k_opcode_count;
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    const auto [multiplier, offset] = cs2::protect::opcode_permutation(seed);
    expect(multiplier % 2 == 1,
           "the multiplier of the seed " + std::to_string(seed) + " is not odd");
    expect(std::gcd(multiplier, static_cast<std::uint64_t>(count)) == 1,
           "the multiplier of the seed " + std::to_string(seed) + " is not coprime");
    const std::uint64_t inverse = modular_inverse(multiplier % count, count);
    for (std::size_t canonical = 0; canonical < count; ++canonical) {
      const std::uint64_t permuted = (canonical * multiplier + offset) % count;
      expect(permuted < count, "the permuted opcode is out of the space");
      const std::uint64_t back = ((permuted + count - (offset % count)) * inverse) % count;
      expect(back == canonical,
             "the permutation of the seed " + std::to_string(seed) + " is not invertible");
    }
  }
}

}  // namespace

auto main() -> int {
  test_the_container_round_trip();
  test_the_arithmetic();
  test_the_negation();
  test_the_switch();
  test_the_swap_on_a_conditional_back_edge();
  test_the_call_that_returns_a_bool();
  test_the_order_of_the_arguments();
  test_the_slot_budget();
  test_the_local_variable();
  test_the_widths();
  test_the_unreachable_stops();
  test_the_permutation();
  if (g_failures != 0) {
    std::printf("vm-values: %d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("vm-values: ok");
  return 0;
}
