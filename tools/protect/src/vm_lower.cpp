// The lowering of the front end of the virtualizer.
//
// The lowerer turns the optimised IR of a function into the instruction
// stream of the virtual machine. It never changes the function: the rewrite
// is the next task, and this one only produces a verdict.

#include "vm_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

namespace cs2::protect::detail {

namespace {
/// The comparison kind of an `icmp`.
[[nodiscard]] auto compare_kind(llvm::CmpInst::Predicate predicate) -> std::optional<VmCompare> {
  switch (predicate) {
    case llvm::CmpInst::ICMP_EQ:
      return VmCompare::eq;
    case llvm::CmpInst::ICMP_NE:
      return VmCompare::ne;
    case llvm::CmpInst::ICMP_UGT:
      return VmCompare::ugt;
    case llvm::CmpInst::ICMP_UGE:
      return VmCompare::uge;
    case llvm::CmpInst::ICMP_ULT:
      return VmCompare::ult;
    case llvm::CmpInst::ICMP_ULE:
      return VmCompare::ule;
    case llvm::CmpInst::ICMP_SGT:
      return VmCompare::sgt;
    case llvm::CmpInst::ICMP_SGE:
      return VmCompare::sge;
    case llvm::CmpInst::ICMP_SLT:
      return VmCompare::slt;
    case llvm::CmpInst::ICMP_SLE:
      return VmCompare::sle;
    default:
      return std::nullopt;
  }
}

/// The cast kind of a conversion.
[[nodiscard]] auto cast_kind(unsigned opcode) -> std::optional<VmCast> {
  switch (opcode) {
    case llvm::Instruction::ZExt:
      return VmCast::zext;
    case llvm::Instruction::SExt:
      return VmCast::sext;
    case llvm::Instruction::Trunc:
      return VmCast::trunc;
    case llvm::Instruction::BitCast:
      return VmCast::bitcast;
    default:
      return std::nullopt;
  }
}

// --- The lowerer ----------------------------------------------------------------

/// The lowerer of one function.
///
/// The lowerer turns the optimised IR of a function into the instruction
/// stream of the virtual machine. It never changes the function: the rewrite
/// is the next task, and this one only produces a verdict.
class Lowerer {
 public:
  Lowerer(llvm::Function& function, const VmOptions& options)
      : function_(function),
        options_(options),
        data_layout_(function.getParent()->getDataLayout()) {}

  /// Lowers the function. A success means that the function was lowered; the
  /// error holds the refusal.
  [[nodiscard]] auto run() -> std::expected<void, VmFailure> {
    // The gate is the gate of `vm_gate.cpp`: the eligibility is one
    // thing, and it is not written twice.
    if (auto failure = gate(function_, options_); !failure.has_value()) {
      return std::unexpected(failure.error());
    }
    if (auto failure = assign_slots(); !failure.has_value()) {
      return std::unexpected(failure.error());
    }
    if (auto failure = lower_body(); !failure.has_value()) {
      return std::unexpected(failure.error());
    }
    if (auto failure = place_locals(); !failure.has_value()) {
      return std::unexpected(failure.error());
    }
    if (auto failure = check_the_widths(); !failure.has_value()) {
      return std::unexpected(failure.error());
    }
    return {};
  }

  /// The instructions of the lowered function.
  [[nodiscard]] auto code() const -> const std::vector<VmInstruction>& {
    return code_;
  }

  /// The number of slots the function uses.
  [[nodiscard]] auto slot_count() const -> std::size_t {
    return next_slot_;
  }

  /// The number of cells the locals of the function take.
  [[nodiscard]] auto local_cells() const -> std::size_t {
    return local_cells_;
  }

  /// The entries of the pool.
  [[nodiscard]] auto pool() const -> const std::vector<VmPoolEntry>& {
    return pool_;
  }

  /// The call sites of the function.
  [[nodiscard]] auto call_sites() const -> const std::vector<VmCallSite>& {
    return call_sites_;
  }

 private:
  // --- the state ---

  llvm::Function& function_;
  const VmOptions& options_;
  const llvm::DataLayout& data_layout_;

  std::vector<VmInstruction> code_;
  std::vector<VmInstruction> prologue_;
  llvm::DenseMap<const llvm::Value*, std::uint16_t> value_slots_;
  llvm::DenseMap<const llvm::Constant*, std::uint16_t> constant_slots_;
  std::vector<VmPoolEntry> pool_;
  std::vector<VmCallSite> call_sites_;
  std::size_t next_slot_{0};
  /// The cells the locals of the function take.
  std::size_t local_cells_{0};
  /// The `alloca`s: the index of the instruction and the cell of the local
  /// region that the local takes. The byte offset of a local is only known
  /// when the lowering is over.
  std::vector<std::pair<std::size_t, std::size_t>> local_fixups_;

  /// The labels the branches point at. A label is an index into this table;
  /// the table is filled at the end, because a branch may point forward.
  std::vector<std::optional<std::size_t>> labels_;
  /// The branches that point at a label: (the instruction, the label).
  std::vector<std::pair<std::size_t, std::size_t>> fixups_;

  // --- the helpers ---

  [[nodiscard]] auto new_label() -> std::size_t {
    labels_.push_back(std::nullopt);
    return labels_.size() - 1;
  }

  void place_label(std::size_t label) {
    labels_[label] = code_.size();
  }

  void emit(VmInstruction instruction) {
    code_.push_back(instruction);
  }

  void branch_to(VmOpcode opcode, std::size_t label) {
    VmInstruction instruction;
    instruction.opcode = opcode;
    instruction.width = 8;  // a branch produces no value: the width is the cell's
    instruction.dst = k_no_slot;
    fixups_.emplace_back(code_.size(), label);
    code_.push_back(instruction);
  }

  /// Allocates a slot, or fails when the budget is exhausted.
  [[nodiscard]] auto allocate_slot() -> std::optional<std::uint16_t> {
    if (next_slot_ >= k_max_slots) {
      return std::nullopt;
    }
    return static_cast<std::uint16_t>(next_slot_++);
  }

  /// The slot of a value.
  [[nodiscard]] auto slot_of(const llvm::Value* value) -> std::optional<std::uint16_t> {
    if (const auto* constant = llvm::dyn_cast<llvm::Constant>(value)) {
      const auto found = constant_slots_.find(constant);
      if (found == constant_slots_.end()) {
        return std::nullopt;
      }
      return found->second;
    }
    const auto found = value_slots_.find(value);
    if (found == value_slots_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  /// The width of the value the function returns.
  ///
  /// A function that returns nothing returns zero, and the width of that
  /// zero is the width of a cell of the frame.
  [[nodiscard]] auto return_width() const -> std::uint8_t {
    if (function_.getReturnType()->isVoidTy()) {
      return 8;
    }
    return width_of(function_.getReturnType());
  }

  /// The name of a type, for the call sites.
  [[nodiscard]] auto type_name(llvm::Type* type) const -> std::string {
    std::string name;
    llvm::raw_string_ostream stream(name);
    type->print(stream);
    return stream.str();
  }

  // --- the slots ---

  /// Assigns a slot to every value of the function, and materialises the
  /// constants and the globals it refers to.
  [[nodiscard]] auto assign_slots() -> std::expected<void, VmFailure> {
    for (const llvm::Argument& argument : function_.args()) {
      const auto slot = allocate_slot();
      if (!slot.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
      }
      value_slots_[&argument] = *slot;
    }
    for (llvm::BasicBlock& block : function_) {
      for (llvm::Instruction& instruction : block) {
        if (instruction.getType()->isVoidTy()) {
          continue;
        }
        const auto slot = allocate_slot();
        if (!slot.has_value()) {
          return std::unexpected(
              VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
        }
        value_slots_[&instruction] = *slot;
      }
    }
    // The constants and the globals. The callee of a call is not one of
    // them: a call gets an entry of the pool of its own.
    for (llvm::BasicBlock& block : function_) {
      for (llvm::Instruction& instruction : block) {
        for (const llvm::Use& operand : instruction.operands()) {
          const auto* value = operand.get();
          if (llvm::isa<llvm::Instruction>(value) || llvm::isa<llvm::Argument>(value)) {
            continue;
          }
          if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
            if (call->getCalledOperand() == value) {
              continue;
            }
          }
          const auto* constant = llvm::dyn_cast<llvm::Constant>(value);
          if (constant == nullptr) {
            // An inline assembly: the lowering refuses it.
            continue;
          }
          if (auto failure = materialize_constant(constant); !failure.has_value()) {
            return std::unexpected(failure.error());
          }
        }
      }
    }
    // The case values of a switch are not operands: LLVM keeps them in a
    // side table of the instruction, so the walk above does not see them.
    // They are values all the same, and the comparison of the switch needs
    // a slot for each of them.
    for (llvm::BasicBlock& block : function_) {
      for (llvm::Instruction& instruction : block) {
        const auto* switcher = llvm::dyn_cast<llvm::SwitchInst>(&instruction);
        if (switcher == nullptr) {
          continue;
        }
        for (const auto& entry : switcher->cases()) {
          if (auto failure = materialize_constant(entry.getCaseValue()); !failure.has_value()) {
            return std::unexpected(failure.error());
          }
        }
      }
    }

    return {};
  }

  /// Materialises a constant into a slot.
  ///
  /// A constant is materialised in the entry block, so that every path finds
  /// it initialised. An `undef` (or a `poison`) is materialised as well: the
  /// value of an `undef` may be read twice, and the two reads have to agree,
  /// so the lowering freezes it into one slot.
  [[nodiscard]] auto materialize_constant(const llvm::Constant* constant)
      -> std::expected<void, VmFailure> {
    if (constant_slots_.count(constant) != 0) {
      return {};
    }
    const auto slot = allocate_slot();
    if (!slot.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
    }
    constant_slots_[constant] = *slot;

    VmInstruction instruction;
    instruction.dst = *slot;

    if (llvm::isa<llvm::UndefValue>(constant)) {
      // The frozen `undef`: a fixed value, zero. An `undef` may be any
      // value, and zero is one of them.
      instruction.opcode = VmOpcode::Const;
      instruction.width = width_of(constant->getType());
      if (instruction.width == 0) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_type, "the undef is not an integer nor a pointer"});
      }
      instruction.imm = 0;
      prologue_.push_back(instruction);
      return {};
    }
    if (const auto* integer = llvm::dyn_cast<llvm::ConstantInt>(constant)) {
      instruction.opcode = VmOpcode::Const;
      instruction.width = width_of(integer->getType());
      if (instruction.width == 0) {
        return std::unexpected(VmFailure{VmRefusal::unsupported_type, "the constant is too wide"});
      }
      instruction.imm = integer->getValue().zextOrTrunc(64).getZExtValue();
      prologue_.push_back(instruction);
      return {};
    }
    if (llvm::isa<llvm::ConstantPointerNull>(constant)) {
      // The null pointer: a value, not an address.
      instruction.opcode = VmOpcode::Const;
      instruction.width = 8;
      instruction.imm = 0;
      prologue_.push_back(instruction);
      return {};
    }
    if (const auto* global = llvm::dyn_cast<llvm::GlobalValue>(constant)) {
      // The address of a global is a *relocatable* value: the bytecode may
      // hold indices and relative offsets only, never an absolute address.
      // The address therefore comes out of the pool, which the mapper
      // relocates.
      instruction.opcode = VmOpcode::Pool;
      instruction.width = 8;  // the entry of the pool is a pointer
      instruction.imm = pool_.size();
      pool_.push_back(VmPoolEntry{global->getName().str(), false, global});
      prologue_.push_back(instruction);
      return {};
    }
    if (const auto* expression = llvm::dyn_cast<llvm::ConstantExpr>(constant)) {
      if (!is_relocatable_address(expression)) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_type,
                      "the constant is an absolute address: the bytecode may hold indices and "
                      "relative offsets only"});
      }
      // A GEP of a global: the linker folds the offset into the
      // relocation, so the expression itself is what the pool holds.
      instruction.opcode = VmOpcode::Pool;
      instruction.width = 8;  // the entry of the pool is a pointer
      instruction.imm = pool_.size();
      pool_.push_back(VmPoolEntry{constant_name(expression), false, expression});
      prologue_.push_back(instruction);
      return {};
    }
    return std::unexpected(VmFailure{VmRefusal::unsupported_type,
                                     "the constant is not an integer, a pointer or a global"});
  }

  /// Whether the constant is an address the linker relocates.
  ///
  /// The address of a global and the GEP of a global are relocatable: the
  /// mapper applies the fixups and the address is right at run time. An
  /// absolute address (a constant cast to a pointer) is not: the bytecode
  /// may hold indices and relative offsets only.
  [[nodiscard]] auto is_relocatable_address(const llvm::Constant* constant) const -> bool {
    if (llvm::isa<llvm::GlobalValue>(constant)) {
      return true;
    }
    const auto* expression = llvm::dyn_cast<llvm::ConstantExpr>(constant);
    if (expression == nullptr) {
      return false;
    }
    if (expression->getOpcode() == llvm::Instruction::GetElementPtr ||
        expression->getOpcode() == llvm::Instruction::BitCast) {
      return is_relocatable_address(expression->getOperand(0));
    }
    return false;
  }

  /// The name of a constant, for the report.
  [[nodiscard]] auto constant_name(const llvm::Constant* constant) const -> std::string {
    if (const auto* global = llvm::dyn_cast<llvm::GlobalValue>(constant)) {
      return global->getName().str();
    }
    std::string name;
    llvm::raw_string_ostream stream(name);
    constant->print(stream);
    return stream.str();
  }

  /// One index of a GEP.
  struct GepIndex {
    const llvm::Value* value{nullptr};  ///< the index itself.
    std::uint64_t scale{0};             ///< the multiplier of the index, in bytes.
    bool is_field{false};               ///< the index selects a field: the offset is the
                                        ///< offset of the field, not a multiple of it.
  };

  /// The type an index walks into.
  ///
  /// A struct is walked into by the *next* index (which selects a field),
  /// an array and a vector by the index itself.
  [[nodiscard]] auto element_type_of(llvm::Type* type) -> llvm::Type* {
    if (const auto* array = llvm::dyn_cast<llvm::ArrayType>(type)) {
      return array->getElementType();
    }
    if (const auto* vector = llvm::dyn_cast<llvm::VectorType>(type)) {
      return vector->getElementType();
    }
    return type;
  }

  /// The scale of every index of a GEP, in bytes.
  ///
  /// The scale of the first index is the size of the source element type;
  /// the scale of the others is the size of the element the index walks
  /// into (the *offset* of the field, for a struct, because a struct has
  /// padding -- and a field is selected, not multiplied).
  [[nodiscard]] auto gep_indices(llvm::Type* source_type,
                                 const std::vector<const llvm::Value*>& indices)
      -> std::optional<std::vector<GepIndex>> {
    std::vector<GepIndex> result;
    llvm::Type* current = source_type;
    for (std::size_t index = 0; index < indices.size(); ++index) {
      const auto* value = indices[index];
      GepIndex entry;
      entry.value = value;
      if (index == 0) {
        entry.scale = alloc_size(current);
        result.push_back(entry);
        current = element_type_of(current);
        continue;
      }
      if (const auto* array = llvm::dyn_cast<llvm::ArrayType>(current)) {
        entry.scale = alloc_size(array->getElementType());
        current = array->getElementType();
      } else if (const auto* vector = llvm::dyn_cast<llvm::VectorType>(current)) {
        const auto size = data_layout_.getTypeStoreSize(vector->getElementType());
        if (size.isScalable()) {
          return std::nullopt;
        }
        entry.scale = size.getFixedValue();
        current = vector->getElementType();
      } else if (auto* structure = llvm::dyn_cast<llvm::StructType>(current)) {
        const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(value);
        if (constant == nullptr) {
          return std::nullopt;  // a variable index into a struct
        }
        const auto field = constant->getZExtValue();
        if (field >= structure->getNumElements()) {
          return std::nullopt;
        }
        entry.scale = data_layout_.getStructLayout(structure)->getElementOffset(field);
        entry.is_field = true;
        current = structure->getElementType(field);
      } else {
        // A pointer or a scalar: the index steps over it.
        entry.scale = alloc_size(current);
      }
      result.push_back(entry);
    }
    return result;
  }

  [[nodiscard]] auto alloc_size(llvm::Type* type) -> std::uint64_t {
    const auto size = data_layout_.getTypeAllocSize(type);
    return size.isScalable() ? 0 : size.getFixedValue();
  }

  // --- the body ---

  [[nodiscard]] auto lower_body() -> std::expected<void, VmFailure> {
    llvm::BasicBlock& entry = function_.getEntryBlock();

    // The labels of the blocks.
    llvm::DenseMap<const llvm::BasicBlock*, std::size_t> block_labels;
    for (llvm::BasicBlock& block : function_) {
      block_labels[&block] = new_label();
    }

    // The prologue: the constants and the globals, in the entry block, so
    // that every path finds them initialised.
    place_label(block_labels[&entry]);
    for (const VmInstruction& instruction : prologue_) {
      code_.push_back(instruction);
    }

    for (llvm::BasicBlock& block : function_) {
      if (&block != &entry) {
        place_label(block_labels[&block]);
      }
      for (llvm::Instruction& instruction : block) {
        if (auto failure = lower_instruction(instruction, block_labels); !failure.has_value()) {
          return std::unexpected(failure.error());
        }
      }
    }

    // The branches that point forward.
    for (const auto& [instruction, label] : fixups_) {
      const auto target = labels_[label];
      if (!target.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "a branch points at no block"});
      }
      code_[instruction].aux = static_cast<std::uint32_t>(*target);
    }
    return {};
  }

  [[nodiscard]] auto lower_instruction(
      llvm::Instruction& instruction,
      const llvm::DenseMap<const llvm::BasicBlock*, std::size_t>& block_labels)
      -> std::expected<void, VmFailure> {
    if (auto* binary = llvm::dyn_cast<llvm::BinaryOperator>(&instruction)) {
      return lower_binary(*binary);
    }
    if (auto* compare = llvm::dyn_cast<llvm::ICmpInst>(&instruction)) {
      return lower_compare(*compare);
    }
    if (auto* cast = llvm::dyn_cast<llvm::CastInst>(&instruction)) {
      return lower_cast(*cast);
    }
    if (auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction)) {
      return lower_load(*load);
    }
    if (auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
      return lower_store(*store);
    }
    if (auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(&instruction)) {
      return lower_gep(*gep);
    }
    if (auto* intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(&instruction)) {
      // The intrinsics are checked before the calls: a `memcpy` is a call
      // too, and it has an opcode of its own.
      return lower_intrinsic(*intrinsic);
    }
    if (auto* call = llvm::dyn_cast<llvm::CallInst>(&instruction)) {
      return lower_call(*call);
    }
    if (auto* branch = llvm::dyn_cast<llvm::BranchInst>(&instruction)) {
      return lower_branch(*branch, block_labels);
    }
    if (auto* switcher = llvm::dyn_cast<llvm::SwitchInst>(&instruction)) {
      return lower_switch(*switcher, block_labels);
    }
    if (auto* select = llvm::dyn_cast<llvm::SelectInst>(&instruction)) {
      return lower_select(*select);
    }
    if (auto* ret = llvm::dyn_cast<llvm::ReturnInst>(&instruction)) {
      return lower_return(*ret);
    }
    if (auto* freeze = llvm::dyn_cast<llvm::FreezeInst>(&instruction)) {
      return lower_freeze(*freeze);
    }

    if (llvm::isa<llvm::PHINode>(instruction)) {
      // A PHI is an edge assignment: the moves are done by the predecessor,
      // before its branch.
      return {};
    }
    if (llvm::isa<llvm::UnreachableInst>(instruction)) {
      // The original is undefined behaviour, so any behaviour is allowed:
      // the virtual machine returns zero.
      //
      // The return is a `Ret` and not a destination-less instruction: the
      // ISA has no "stop" opcode, and the `Ret` is what stops the
      // interpreter. A `Const` with no destination would fall out of the
      // block and into the next one.
      const auto width = return_width();
      if (width == 0) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_type, "the return type is not an integer"});
      }
      const auto zero = allocate_slot();
      if (!zero.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
      }
      VmInstruction constant;
      constant.opcode = VmOpcode::Const;
      constant.width = width;
      constant.dst = *zero;
      constant.imm = 0;
      emit(constant);

      VmInstruction ret;
      ret.opcode = VmOpcode::Ret;
      ret.width = width;
      ret.dst = k_no_slot;
      ret.src1 = *zero;
      emit(ret);
      return {};
    }
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(&instruction)) {
      return lower_alloca(*alloca);
    }
    if (llvm::isa<llvm::AtomicRMWInst>(instruction) ||
        llvm::isa<llvm::AtomicCmpXchgInst>(instruction) ||
        llvm::isa<llvm::FenceInst>(instruction)) {
      return std::unexpected(VmFailure{VmRefusal::atomic_access, "the instruction is atomic"});
    }
    if (llvm::isa<llvm::ExtractValueInst>(instruction) ||
        llvm::isa<llvm::InsertValueInst>(instruction)) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_type, "the instruction works on an aggregate"});
    }
    return std::unexpected(VmFailure{
        VmRefusal::unsupported_instruction,
        "the instruction '" + std::string(instruction.getOpcodeName()) + "' is not modelled"});
  }

  /// Masks the result of an operation whose type is an `i1`.
  ///
  /// The width of the virtual machine is a byte, and the value of an `i1` is
  /// its low bit: an operation that leaves a bit set above the low one would
  /// make the value of the `i1` something else. A comparison produces zero
  /// or one and needs no mask.
  ///
  /// The mask is not optional: a function whose budget is exhausted is
  /// **refused**, never lowered with a missing mask (a fail-open would
  /// compute a wrong value).
  [[nodiscard]] auto mask_if_bool(llvm::Type* type, std::uint16_t slot)
      -> std::expected<void, VmFailure> {
    if (!is_bool(type)) {
      return {};
    }
    const auto one = allocate_slot();
    if (!one.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
    }
    VmInstruction constant;
    constant.opcode = VmOpcode::Const;
    constant.width = 1;
    constant.dst = *one;
    constant.imm = 1;
    emit(constant);

    VmInstruction mask;
    mask.opcode = VmOpcode::And;
    mask.width = 1;
    mask.dst = slot;
    mask.src1 = slot;
    mask.src2 = *one;
    emit(mask);
    return {};
  }

  /// Lowers an `alloca`: the local lives in the frame arena.
  ///
  /// The parent specification says that an `alloca` gets a byte offset into
  /// the frame arena, and the frame **is** an arena: the locals are the
  /// cells above the value slots, and the frame does not grow. The opcode
  /// carries the byte offset, so the bytecode holds a relative offset and
  /// never an address.
  ///
  /// The byte offset is only known when the lowering is over (the value
  /// slots above the locals are allocated as the lowering goes), so the
  /// instruction is patched at the end.
  [[nodiscard]] auto lower_alloca(llvm::AllocaInst& alloca) -> std::expected<void, VmFailure> {
    if (alloca.isArrayAllocation()) {
      return std::unexpected(VmFailure{VmRefusal::local_variable,
                                       "the alloca has a variable size: the frame is a constant"});
    }
    if (alloca.getAddressSpace() != 0) {
      return std::unexpected(VmFailure{VmRefusal::local_variable,
                                       "the alloca is in another address space: the frame is not"});
    }
    llvm::Type* type = alloca.getAllocatedType();
    const auto size = data_layout_.getTypeAllocSize(type);
    if (size.isScalable()) {
      return std::unexpected(
          VmFailure{VmRefusal::local_variable, "the size of the local is not constant"});
    }
    const auto alignment = alloca.getAlign().value();
    if (alignment > k_frame_cell_bytes) {
      return std::unexpected(
          VmFailure{VmRefusal::local_variable, "the local needs an alignment of " +
                                                   std::to_string(alignment) +
                                                   " bytes: the cells of the frame are 8 bytes"});
    }
    const auto bytes = size.getFixedValue();
    const auto cells = (bytes + k_frame_cell_bytes - 1) / k_frame_cell_bytes;

    const auto dst = slot_of(&alloca);
    if (!dst.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "the alloca has no slot"});
    }
    VmInstruction instruction;
    instruction.opcode = VmOpcode::Alloca;
    instruction.width = 8;
    instruction.dst = *dst;
    instruction.imm = 0;  // the byte offset is patched when the lowering is over
    local_fixups_.emplace_back(code_.size(), local_cells_);
    local_cells_ += cells;
    emit(instruction);
    return {};
  }

  /// Puts the locals in the frame, and checks that they fit.
  ///
  /// The locals are the cells above the value slots, so their byte offsets
  /// are only known when the lowering is over: the value slots above them
  /// are allocated as the lowering goes.
  /// Checks the post-condition of the encoding: **no instruction may carry a
  /// width of 0**.
  ///
  /// The width is the width of the value the instruction produces, and an
  /// interpreter written against the contract truncates its result to it — a
  /// width of 0 would truncate it to nothing. The width of every instruction
  /// is therefore one of 1, 2, 4 or 8, and this check is what makes that
  /// true **by construction** rather than by the care of each site that
  /// sets a width: the field's default is 0, so a site that forgets to set
  /// it is caught here instead of shipping a bytecode that returns 0 from
  /// every function.
  [[nodiscard]] auto check_the_widths() -> std::expected<void, VmFailure> {
    for (std::size_t index = 0; index < code_.size(); ++index) {
      const auto width = code_[index].width;
      if (width == 1 || width == 2 || width == 4 || width == 8) {
        continue;
      }
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction,
                    "the instruction " + std::to_string(index) + " carries the width " +
                        std::to_string(width) + ", and the contract allows only 1, 2, 4 or 8"});
    }
    return {};
  }

  /// Places the locals of the function in the frame arena.
  ///
  /// The locals are the cells above the value slots: the values and the
  /// locals share the value region of the frame, and the byte offset of a
  /// local is only known when the lowering is over.
  [[nodiscard]] auto place_locals() -> std::expected<void, VmFailure> {
    if (local_cells_ == 0) {
      return {};
    }
    if (next_slot_ + local_cells_ > k_frame_value_slots) {
      return std::unexpected(VmFailure{VmRefusal::local_variable,
                                       "the locals need " + std::to_string(local_cells_) +
                                           " cells and the value region of the frame has " +
                                           std::to_string(k_frame_value_slots - next_slot_) +
                                           " left"});
    }
    for (const auto& [instruction, cell] : local_fixups_) {
      code_[instruction].imm = static_cast<std::uint64_t>(next_slot_ + cell) * k_frame_cell_bytes;
    }
    return {};
  }

  [[nodiscard]] auto lower_binary(llvm::BinaryOperator& binary) -> std::expected<void, VmFailure> {
    const auto width = width_of(binary.getType());
    if (width == 0) {
      return std::unexpected(VmFailure{VmRefusal::unsupported_type,
                                       "the operand type is not an integer of at most 64 bits"});
    }
    const auto dst = slot_of(&binary);
    const auto src1 = slot_of(binary.getOperand(0));
    const auto src2 = slot_of(binary.getOperand(1));
    if (!dst.has_value() || !src1.has_value() || !src2.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "an operand of the operation has no slot"});
    }

    VmOpcode opcode = VmOpcode::Const;
    switch (binary.getOpcode()) {
      case llvm::Instruction::Add:
        opcode = VmOpcode::Add;
        break;
      case llvm::Instruction::Sub:
        opcode = VmOpcode::Sub;
        break;
      case llvm::Instruction::Mul:
        opcode = VmOpcode::Mul;
        break;
      case llvm::Instruction::And:
        opcode = VmOpcode::And;
        break;
      case llvm::Instruction::Or:
        opcode = VmOpcode::Or;
        break;
      case llvm::Instruction::Xor:
        opcode = VmOpcode::Xor;
        break;
      case llvm::Instruction::UDiv:
        opcode = VmOpcode::UDiv;
        break;
      case llvm::Instruction::SDiv:
        opcode = VmOpcode::SDiv;
        break;
      case llvm::Instruction::URem:
        opcode = VmOpcode::URem;
        break;
      case llvm::Instruction::SRem:
        opcode = VmOpcode::SRem;
        break;
      case llvm::Instruction::Shl:
        opcode = VmOpcode::Shl;
        break;
      case llvm::Instruction::LShr:
        opcode = VmOpcode::LShr;
        break;
      case llvm::Instruction::AShr:
        opcode = VmOpcode::AShr;
        break;
      default:
        return std::unexpected(VmFailure{
            VmRefusal::unsupported_instruction,
            "the operation '" + std::string(binary.getOpcodeName()) + "' is not modelled"});
    }

    std::uint8_t flags = 0;
    if (binary.hasNoSignedWrap()) {
      flags |= k_flag_nsw;
    }
    if (binary.hasNoUnsignedWrap()) {
      flags |= k_flag_nuw;
    }
    if (binary.isExact()) {
      flags |= k_flag_exact;
    }

    VmInstruction instruction;
    instruction.opcode = opcode;
    instruction.width = width;
    instruction.flags = flags;
    instruction.dst = *dst;
    instruction.src1 = *src1;
    instruction.src2 = *src2;
    emit(instruction);

    // `sub 0, x` is a negation and `xor x, -1` is a complement: the ISA
    // has an opcode for each, and the interpreter does not have to work
    // out which one it is.
    //
    // The negation takes the *subtrahend*: `sub a, b` is `a - b`, so a
    // `sub 0, x` negates `x` and not the zero.
    if (opcode == VmOpcode::Sub && is_zero(binary.getOperand(0)) &&
        !is_zero(binary.getOperand(1))) {
      code_.back().opcode = VmOpcode::Neg;
      code_.back().src1 = code_.back().src2;
      code_.back().src2 = k_no_slot;
      code_.back().flags = 0;  // the ISA defines no flag for a negation
    }
    if (opcode == VmOpcode::Xor && is_all_ones(binary.getOperand(1)) &&
        !is_all_ones(binary.getOperand(0))) {
      code_.back().opcode = VmOpcode::Not;
      code_.back().src2 = k_no_slot;
      code_.back().flags = 0;  // the ISA defines no flag for a complement
    }

    if (auto failure = mask_if_bool(binary.getType(), *dst); !failure.has_value()) {
      return std::unexpected(failure.error());
    }
    return {};
  }

  /// Whether the value is the constant zero.
  [[nodiscard]] auto is_zero(const llvm::Value* value) const -> bool {
    const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(value);
    return constant != nullptr && constant->isZero();
  }

  /// Whether the value is the constant of all ones of its width.
  [[nodiscard]] auto is_all_ones(const llvm::Value* value) const -> bool {
    const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(value);
    if (constant == nullptr) {
      return false;
    }
    return constant->getValue().isAllOnes();
  }

  [[nodiscard]] auto lower_compare(llvm::ICmpInst& compare) -> std::expected<void, VmFailure> {
    llvm::Type* type = compare.getOperand(0)->getType();
    std::uint8_t width = 0;
    if (type->isIntegerTy()) {
      width = width_of(type);
    } else if (type->isPointerTy()) {
      width = 8;
    }
    if (width == 0) {
      return std::unexpected(VmFailure{VmRefusal::unsupported_type,
                                       "the comparison is not on an integer or a pointer"});
    }
    const auto kind = compare_kind(compare.getPredicate());
    if (!kind.has_value()) {
      return std::unexpected(VmFailure{
          VmRefusal::unsupported_instruction,
          "the comparison '" + std::string(compare.getOpcodeName()) + "' is not modelled"});
    }
    const auto dst = slot_of(&compare);
    const auto src1 = slot_of(compare.getOperand(0));
    const auto src2 = slot_of(compare.getOperand(1));
    if (!dst.has_value() || !src1.has_value() || !src2.has_value()) {
      return std::unexpected(VmFailure{VmRefusal::unsupported_instruction,
                                       "an operand of the comparison has no slot"});
    }
    VmInstruction instruction;
    instruction.opcode = VmOpcode::Cmp;
    instruction.width = width;
    instruction.dst = *dst;
    instruction.src1 = *src1;
    instruction.src2 = *src2;
    instruction.aux = static_cast<std::uint32_t>(*kind);
    emit(instruction);
    return {};
  }

  [[nodiscard]] auto lower_cast(llvm::CastInst& cast) -> std::expected<void, VmFailure> {
    const auto dst = slot_of(&cast);
    const auto src = slot_of(cast.getOperand(0));
    if (!dst.has_value() || !src.has_value()) {
      return std::unexpected(VmFailure{VmRefusal::unsupported_instruction,
                                       "an operand of the conversion has no slot"});
    }
    const auto source_width = width_of(cast.getOperand(0)->getType());
    const auto target_width = width_of(cast.getType());
    if (source_width == 0 || target_width == 0) {
      return std::unexpected(VmFailure{VmRefusal::unsupported_type,
                                       "the conversion is not on an integer or a pointer"});
    }

    const auto kind = cast_kind(cast.getOpcode());
    if (kind.has_value()) {
      if (*kind == VmCast::bitcast && source_width != target_width) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_type,
                      "the bitcast changes the width: it is not a conversion the virtual "
                      "machine models"});
      }
      VmInstruction instruction;
      instruction.opcode = VmOpcode::Cast;
      instruction.width = target_width;
      instruction.dst = *dst;
      instruction.src1 = *src;
      instruction.aux =
          static_cast<std::uint32_t>(*kind) | (static_cast<std::uint32_t>(source_width) << 8U);
      emit(instruction);
      if (auto failure = mask_if_bool(cast.getType(), *dst); !failure.has_value()) {
        return std::unexpected(failure.error());
      }
      return {};
    }

    switch (cast.getOpcode()) {
      case llvm::Instruction::PtrToInt: {
        if (target_width == 8) {
          VmInstruction instruction;
          instruction.opcode = VmOpcode::Cast;
          instruction.width = 8;
          instruction.dst = *dst;
          instruction.src1 = *src;
          instruction.aux = static_cast<std::uint32_t>(VmCast::bitcast) | (8U << 8U);
          emit(instruction);
          return {};
        }
        // The low bytes of the pointer.
        VmInstruction instruction;
        instruction.opcode = VmOpcode::Cast;
        instruction.width = target_width;
        instruction.dst = *dst;
        instruction.src1 = *src;
        instruction.aux = static_cast<std::uint32_t>(VmCast::trunc) | (8U << 8U);
        emit(instruction);
        if (auto failure = mask_if_bool(cast.getType(), *dst); !failure.has_value()) {
          return std::unexpected(failure.error());
        }
        return {};
      }
      case llvm::Instruction::IntToPtr: {
        if (source_width == 8) {
          VmInstruction instruction;
          instruction.opcode = VmOpcode::Cast;
          instruction.width = 8;
          instruction.dst = *dst;
          instruction.src1 = *src;
          instruction.aux = static_cast<std::uint32_t>(VmCast::bitcast) | (8U << 8U);
          emit(instruction);
          return {};
        }
        // The integer is widened to the width of a pointer, and the bits
        // are then the bits of a pointer.
        const auto wide = allocate_slot();
        if (!wide.has_value()) {
          return std::unexpected(
              VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
        }
        VmInstruction extend;
        extend.opcode = VmOpcode::Cast;
        extend.width = 8;
        extend.dst = *wide;
        extend.src1 = *src;
        extend.aux = static_cast<std::uint32_t>(VmCast::zext) |
                     (static_cast<std::uint32_t>(source_width) << 8U);
        emit(extend);

        VmInstruction instruction;
        instruction.opcode = VmOpcode::Cast;
        instruction.width = 8;
        instruction.dst = *dst;
        instruction.src1 = *wide;
        instruction.aux = static_cast<std::uint32_t>(VmCast::bitcast) | (8U << 8U);
        emit(instruction);
        return {};
      }
      default:
        // A conversion to or from a float is a *type* the virtual machine
        // does not model, not an instruction it does not model.
        return std::unexpected(VmFailure{
            VmRefusal::unsupported_type,
            "the conversion '" + std::string(cast.getOpcodeName()) + "' is not modelled"});
    }
  }

  [[nodiscard]] auto lower_load(llvm::LoadInst& load) -> std::expected<void, VmFailure> {
    const auto width = width_of(load.getType());
    if (width == 0) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_type,
                    "the loaded type is not an integer of at most 64 bits nor a pointer"});
    }
    const auto dst = slot_of(&load);
    const auto pointer = slot_of(load.getPointerOperand());
    if (!dst.has_value() || !pointer.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "an operand of the load has no slot"});
    }
    VmInstruction instruction;
    instruction.opcode = VmOpcode::Load;
    instruction.width = width;
    instruction.aux = width;
    instruction.dst = *dst;
    instruction.src1 = *pointer;
    emit(instruction);
    if (auto failure = mask_if_bool(load.getType(), *dst); !failure.has_value()) {
      return std::unexpected(failure.error());
    }
    return {};
  }

  [[nodiscard]] auto lower_store(llvm::StoreInst& store) -> std::expected<void, VmFailure> {
    const auto width = width_of(store.getValueOperand()->getType());
    if (width == 0) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_type,
                    "the stored type is not an integer of at most 64 bits nor a pointer"});
    }
    const auto pointer = slot_of(store.getPointerOperand());
    const auto value = slot_of(store.getValueOperand());
    if (!pointer.has_value() || !value.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "an operand of the store has no slot"});
    }
    VmInstruction instruction;
    instruction.opcode = VmOpcode::Store;
    instruction.width = width;
    instruction.aux = width;
    instruction.dst = k_no_slot;
    instruction.src1 = *pointer;
    instruction.src2 = *value;
    emit(instruction);
    return {};
  }

  [[nodiscard]] auto lower_gep(llvm::GetElementPtrInst& gep) -> std::expected<void, VmFailure> {
    if (!gep.getType()->isPointerTy()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_type, "the GEP does not yield a pointer"});
    }
    std::vector<const llvm::Value*> indices;
    for (unsigned index = 0; index < gep.getNumIndices(); ++index) {
      indices.push_back(gep.getOperand(index + 1));
    }
    const auto described = gep_indices(gep.getSourceElementType(), indices);
    if (!described.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "the GEP is not modelled"});
    }
    const auto base = slot_of(gep.getPointerOperand());
    const auto dst = slot_of(&gep);
    if (!base.has_value() || !dst.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "an operand of the GEP has no slot"});
    }

    // The constant part in the offset.
    std::int64_t offset = 0;
    std::vector<std::pair<const llvm::Value*, std::uint64_t>> variable;
    for (const GepIndex& index : *described) {
      if (index.is_field) {
        // A field is selected, not multiplied: the offset is the offset of
        // the field.
        offset += static_cast<std::int64_t>(index.scale);
        continue;
      }
      const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(index.value);
      if (constant != nullptr) {
        offset += static_cast<std::int64_t>(constant->getSExtValue()) *
                  static_cast<std::int64_t>(index.scale);
        continue;
      }
      variable.emplace_back(index.value, index.scale);
    }

    // The address of the base, moved by the constant offset.
    std::uint16_t accumulator = *base;
    if (offset != 0) {
      VmInstruction instruction;
      instruction.opcode = VmOpcode::Gep;
      instruction.width = 8;
      instruction.dst = *dst;
      instruction.src1 = *base;
      instruction.imm = static_cast<std::uint64_t>(offset);
      emit(instruction);
      accumulator = *dst;
    }

    // The variable part: the index is widened to the width of a pointer,
    // multiplied by the scale, and added to the address.
    for (const auto& [value, scale] : variable) {
      if (width_of(value->getType()) == 0) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_type, "the index of the GEP is not an integer"});
      }
      const auto index_slot = slot_of(value);
      if (!index_slot.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "an index of the GEP has no slot"});
      }
      const auto scale_slot = allocate_slot();
      if (!scale_slot.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
      }
      // The scale is materialised here, and not in the prologue: the
      // prologue holds the constants the *source* uses, and this one is
      // the compiler's own.
      VmInstruction scale_value;
      scale_value.opcode = VmOpcode::Const;
      scale_value.width = 8;
      scale_value.dst = *scale_slot;
      scale_value.imm = scale;
      emit(scale_value);
      const auto wide = allocate_slot();
      const auto product = allocate_slot();
      if (!wide.has_value() || !product.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
      }

      // The index is signed, and it is widened before it is multiplied.
      VmInstruction extend;
      extend.opcode = VmOpcode::Cast;
      extend.width = 8;
      extend.dst = *wide;
      extend.src1 = *index_slot;
      extend.aux = static_cast<std::uint32_t>(VmCast::sext) |
                   (static_cast<std::uint32_t>(width_of(value->getType())) << 8U);
      emit(extend);

      VmInstruction multiply;
      multiply.opcode = VmOpcode::Mul;
      multiply.width = 8;
      multiply.dst = *product;
      multiply.src1 = *wide;
      multiply.src2 = *scale_slot;
      emit(multiply);

      VmInstruction add;
      add.opcode = VmOpcode::Add;
      add.width = 8;
      add.dst = *dst;
      add.src1 = accumulator;
      add.src2 = *product;
      emit(add);
      accumulator = *dst;
    }

    if (offset == 0 && variable.empty()) {
      // The GEP moves the address by nothing: the value is the base.
      VmInstruction instruction;
      instruction.opcode = VmOpcode::Move;
      instruction.width = 8;
      instruction.dst = *dst;
      instruction.src1 = *base;
      emit(instruction);
    }
    return {};
  }

  [[nodiscard]] auto lower_call(llvm::CallInst& call) -> std::expected<void, VmFailure> {
    const auto* callee = call.getCalledFunction();
    if (callee == nullptr) {
      return std::unexpected(
          VmFailure{VmRefusal::indirect_call, "the call goes through a pointer"});
    }
    if (callee->isIntrinsic()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "the call is an intrinsic"});
    }

    // The arguments are pushed onto the argument stack, in the order of the
    // call: the first argument of the call is the first one pushed, and
    // the interpreter copies them out of the stack in that order.
    for (const llvm::Use& argument : call.args()) {
      const auto slot = slot_of(argument.get());
      if (!slot.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "an argument of the call has no slot"});
      }
      VmInstruction push;
      push.opcode = VmOpcode::Arg;
      push.width = 8;  // a cell of the argument stack holds a value
      push.dst = k_no_slot;
      push.src1 = *slot;
      emit(push);
    }

    VmCallSite site;
    site.callee = callee->getName().str();
    site.argument_count = call.arg_size();
    site.pool_index = pool_.size();
    pool_.push_back(VmPoolEntry{site.callee, true, callee});
    for (const llvm::Argument& argument : callee->args()) {
      site.argument_types.push_back(type_name(argument.getType()));
    }
    site.return_type = type_name(callee->getReturnType());
    call_sites_.push_back(site);

    const auto result = slot_of(&call);
    if (!call.getType()->isVoidTy() && !result.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "the result of the call has no slot"});
    }

    VmInstruction instruction;
    instruction.opcode = VmOpcode::Call;
    // The call goes through the uniform signature, which returns a 64-bit
    // value: a call that returns nothing carries the width of that value.
    instruction.width = callee->getReturnType()->isVoidTy() ? 8 : width_of(callee->getReturnType());
    instruction.dst = result.value_or(k_no_slot);
    instruction.imm = site.pool_index;
    instruction.aux = static_cast<std::uint32_t>(site.argument_count);
    emit(instruction);
    // The x64 ABI returns an `i1` in the low byte of the register, and the
    // other seven bits of the byte are undefined: the result is masked, so
    // that the value of the `i1` is its low bit.
    if (result.has_value()) {
      if (auto failure = mask_if_bool(call.getType(), *result); !failure.has_value()) {
        return std::unexpected(failure.error());
      }
    }
    return {};
  }

  [[nodiscard]] auto lower_branch(
      llvm::BranchInst& branch,
      const llvm::DenseMap<const llvm::BasicBlock*, std::size_t>& block_labels)
      -> std::expected<void, VmFailure> {
    if (branch.isConditional()) {
      const auto condition = slot_of(branch.getCondition());
      if (!condition.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "the condition has no slot"});
      }
      // The moves of the PHIs of a successor belong to the **edge** that
      // leads to it: the edge that is not taken must not perform them. The
      // taken edge therefore gets a trampoline, and the fall-through keeps
      // the moves of the other edge.
      const auto taken = new_label();
      VmInstruction conditional;
      conditional.opcode = VmOpcode::BrIf;
      conditional.width = 8;  // a branch produces no value: the width is the cell's
      conditional.dst = k_no_slot;
      conditional.src1 = *condition;
      fixups_.emplace_back(code_.size(), taken);
      code_.push_back(conditional);

      // The fall-through: the edge to the second successor.
      if (auto failure = emit_phi_copies(*branch.getSuccessor(1), *branch.getParent());
          !failure.has_value()) {
        return std::unexpected(failure.error());
      }
      branch_to(VmOpcode::Br, block_labels.at(branch.getSuccessor(1)));

      // The taken edge: the trampoline.
      place_label(taken);
      if (auto failure = emit_phi_copies(*branch.getSuccessor(0), *branch.getParent());
          !failure.has_value()) {
        return std::unexpected(failure.error());
      }
      branch_to(VmOpcode::Br, block_labels.at(branch.getSuccessor(0)));
      return {};
    }
    // The unconditional branch has one edge, and the moves of it are on it.
    if (auto failure = emit_phi_copies(*branch.getSuccessor(0), *branch.getParent());
        !failure.has_value()) {
      return std::unexpected(failure.error());
    }
    branch_to(VmOpcode::Br, block_labels.at(branch.getSuccessor(0)));
    return {};
  }

  [[nodiscard]] auto lower_switch(
      llvm::SwitchInst& switcher,
      const llvm::DenseMap<const llvm::BasicBlock*, std::size_t>& block_labels)
      -> std::expected<void, VmFailure> {
    const auto value = slot_of(switcher.getCondition());
    if (!value.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "the condition of the switch has no slot"});
    }
    const auto width = width_of(switcher.getCondition()->getType());
    if (width == 0) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_type, "the condition of the switch is not an integer"});
    }

    // A switch is a chain of comparisons: the value of the specification's
    // table says that it is not refused.
    //
    // The moves of the PHIs of a successor belong to the **edge** that
    // leads to it, so every case gets a trampoline of its own: the moves of
    // a case that is not taken are not performed.
    std::vector<std::size_t> case_labels;
    for (const auto& entry : switcher.cases()) {
      const auto case_slot = slot_of(entry.getCaseValue());
      if (!case_slot.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "a case of the switch has no slot"});
      }
      const auto temporary = allocate_slot();
      if (!temporary.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
      }
      VmInstruction compare;
      compare.opcode = VmOpcode::Cmp;
      compare.width = width;
      compare.dst = *temporary;
      compare.src1 = *value;
      compare.src2 = *case_slot;
      compare.aux = static_cast<std::uint32_t>(VmCompare::eq);
      emit(compare);

      const auto case_label = new_label();
      case_labels.push_back(case_label);
      VmInstruction conditional;
      conditional.opcode = VmOpcode::BrIf;
      conditional.width = 8;  // a branch produces no value: the width is the cell's
      conditional.dst = k_no_slot;
      conditional.src1 = *temporary;
      fixups_.emplace_back(code_.size(), case_label);
      code_.push_back(conditional);
    }

    // The default: the edge that is taken when no case matches.
    if (auto failure = emit_phi_copies(*switcher.getDefaultDest(), *switcher.getParent());
        !failure.has_value()) {
      return std::unexpected(failure.error());
    }
    branch_to(VmOpcode::Br, block_labels.at(switcher.getDefaultDest()));

    // The trampolines: one per case.
    std::size_t index = 0;
    for (const auto& entry : switcher.cases()) {
      place_label(case_labels[index]);
      ++index;
      if (auto failure = emit_phi_copies(*entry.getCaseSuccessor(), *switcher.getParent());
          !failure.has_value()) {
        return std::unexpected(failure.error());
      }
      branch_to(VmOpcode::Br, block_labels.at(entry.getCaseSuccessor()));
    }
    return {};
  }

  [[nodiscard]] auto lower_select(llvm::SelectInst& select) -> std::expected<void, VmFailure> {
    const auto condition = slot_of(select.getCondition());
    const auto true_value = slot_of(select.getTrueValue());
    const auto false_value = slot_of(select.getFalseValue());
    const auto dst = slot_of(&select);
    if (!condition.has_value() || !true_value.has_value() || !false_value.has_value() ||
        !dst.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "an operand of the select has no slot"});
    }

    // The select is a branch: the value is written on the edge that is
    // taken. The two edges meet again at the merge point.
    const auto width = width_of(select.getType());
    if (width == 0) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_type, "the select is not on an integer nor a pointer"});
    }
    const auto true_label = new_label();
    const auto merge_label = new_label();

    VmInstruction conditional;
    conditional.opcode = VmOpcode::BrIf;
    conditional.width = 8;  // a branch produces no value: the width is the cell's
    conditional.dst = k_no_slot;
    conditional.src1 = *condition;
    fixups_.emplace_back(code_.size(), true_label);
    code_.push_back(conditional);

    VmInstruction false_move;
    false_move.opcode = VmOpcode::Move;
    false_move.width = width;
    false_move.dst = *dst;
    false_move.src1 = *false_value;
    code_.push_back(false_move);

    branch_to(VmOpcode::Br, merge_label);

    place_label(true_label);
    VmInstruction true_move;
    true_move.opcode = VmOpcode::Move;
    true_move.width = width;
    true_move.dst = *dst;
    true_move.src1 = *true_value;
    code_.push_back(true_move);

    place_label(merge_label);
    return {};
  }

  [[nodiscard]] auto lower_return(llvm::ReturnInst& ret) -> std::expected<void, VmFailure> {
    std::uint16_t value = k_no_slot;
    std::uint8_t width = 8;
    if (ret.getNumOperands() == 0) {
      // A function that returns nothing returns zero. The zero is
      // materialised here, and not in the prologue: it belongs to this
      // edge, and the prologue is emitted before the body.
      const auto zero = allocate_slot();
      if (!zero.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::too_many_slots, "the function needs too many slots"});
      }
      VmInstruction constant;
      constant.opcode = VmOpcode::Const;
      constant.width = 8;
      constant.dst = *zero;
      constant.imm = 0;
      emit(constant);
      value = *zero;
    } else {
      const auto slot = slot_of(ret.getOperand(0));
      if (!slot.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "the returned value has no slot"});
      }
      value = *slot;
      // The width of the `Ret` is the width of the **returned value**: the
      // type of the `ret` itself is `void`, and a void return carries the
      // width of a cell.
      width = width_of(ret.getReturnValue()->getType());
      if (width == 0) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_type, "the returned value is not an integer"});
      }
    }
    VmInstruction instruction;
    instruction.opcode = VmOpcode::Ret;
    instruction.width = width;
    instruction.dst = k_no_slot;
    instruction.src1 = value;
    emit(instruction);
    return {};
  }

  [[nodiscard]] auto lower_freeze(llvm::FreezeInst& freeze) -> std::expected<void, VmFailure> {
    // Every value of the virtual machine already sits in a slot, and a slot
    // holds one value: the freeze is a copy.
    const auto dst = slot_of(&freeze);
    const auto src = slot_of(freeze.getOperand(0));
    if (!dst.has_value() || !src.has_value()) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction, "an operand of the freeze has no slot"});
    }
    const auto width = width_of(freeze.getType());
    if (width == 0) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_type, "the freeze is not on an integer nor a pointer"});
    }
    VmInstruction instruction;
    instruction.opcode = VmOpcode::Move;
    instruction.width = width;
    instruction.dst = *dst;
    instruction.src1 = *src;
    emit(instruction);
    return {};
  }

  [[nodiscard]] auto lower_intrinsic(llvm::IntrinsicInst& intrinsic)
      -> std::expected<void, VmFailure> {
    if (const auto* copy = llvm::dyn_cast<llvm::MemCpyInst>(&intrinsic)) {
      if (copy->isVolatile()) {
        return std::unexpected(VmFailure{VmRefusal::volatile_access, "the memcpy is volatile"});
      }
      const auto length = constant_length(*copy);
      if (!length.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::dynamic_memcpy, "the length of the memcpy is not a constant"});
      }
      const auto destination = slot_of(copy->getRawDest());
      const auto source = slot_of(copy->getRawSource());
      if (!destination.has_value() || !source.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "an operand of the memcpy has no slot"});
      }
      VmInstruction instruction;
      instruction.opcode = VmOpcode::MemCpy;
      instruction.width = 1;  // the copy is byte-wise: the length is in the imm
      instruction.dst = k_no_slot;
      instruction.src1 = *destination;
      instruction.src2 = *source;
      instruction.imm = *length;
      emit(instruction);
      return {};
    }
    if (const auto* set = llvm::dyn_cast<llvm::MemSetInst>(&intrinsic)) {
      if (set->isVolatile()) {
        return std::unexpected(VmFailure{VmRefusal::volatile_access, "the memset is volatile"});
      }
      const auto length = constant_length(*set);
      if (!length.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::dynamic_memcpy, "the length of the memset is not a constant"});
      }
      const auto destination = slot_of(set->getRawDest());
      const auto value = slot_of(set->getArgOperand(1));
      if (!destination.has_value() || !value.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "an operand of the memset has no slot"});
      }
      VmInstruction instruction;
      instruction.opcode = VmOpcode::MemSet;
      instruction.width = 1;  // the set is byte-wise: the length is in the imm
      instruction.dst = k_no_slot;
      instruction.src1 = *destination;
      instruction.src2 = *value;
      instruction.imm = *length;
      emit(instruction);
      return {};
    }
    if (intrinsic.getIntrinsicID() == llvm::Intrinsic::memmove) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_instruction,
                    "the memmove may copy overlapping ranges: the virtual machine has no "
                    "opcode for it"});
    }

    // The intrinsics that have no effect: the debug information, the
    // lifetime markers, the assumptions and the alias scopes.
    const std::string name = intrinsic.getName().str();
    if (name.starts_with("llvm.dbg.") || name.starts_with("llvm.lifetime.") ||
        name == "llvm.assume" || name == "llvm.experimental.noalias.scope.decl" ||
        name == "llvm.launder.invariant.group" || name == "llvm.strip.invariant.group" ||
        name == "llvm.prefetch") {
      return {};
    }
    return std::unexpected(VmFailure{VmRefusal::unsupported_instruction,
                                     "the intrinsic '" + name + "' is not modelled"});
  }

  /// The moves of the PHIs of the successor, on the edge from the
  /// predecessor.
  ///
  /// The moves are a *parallel* copy: every source is pushed onto the
  /// argument stack, and every destination is popped in reverse order. A
  /// sequential copy breaks a swapping back-edge (the classic `a, b = b, a`).
  [[nodiscard]] auto emit_phi_copies(llvm::BasicBlock& successor, llvm::BasicBlock& predecessor)
      -> std::expected<void, VmFailure> {
    std::vector<llvm::PHINode*> phis;
    for (llvm::PHINode& phi : successor.phis()) {
      phis.push_back(&phi);
    }
    if (phis.empty()) {
      return {};
    }
    if (phis.size() > k_frame_argument_slots) {
      return std::unexpected(VmFailure{VmRefusal::too_many_slots,
                                       "the parallel copy needs " + std::to_string(phis.size()) +
                                           " cells of the argument stack"});
    }
    for (llvm::PHINode* phi : phis) {
      const auto* incoming = phi->getIncomingValueForBlock(&predecessor);
      if (incoming == nullptr) {
        return std::unexpected(VmFailure{VmRefusal::unsupported_instruction,
                                         "a phi of the successor has no value for the edge"});
      }
      const auto slot = slot_of(incoming);
      if (!slot.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "a value of the copy has no slot"});
      }
      VmInstruction push;
      push.opcode = VmOpcode::Arg;
      push.width = 8;  // a cell of the argument stack holds a value
      push.dst = k_no_slot;
      push.src1 = *slot;
      emit(push);
    }
    for (auto phi = phis.rbegin(); phi != phis.rend(); ++phi) {
      const auto dst = slot_of(*phi);
      if (!dst.has_value()) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_instruction, "a destination of the copy has no slot"});
      }
      VmInstruction pop;
      pop.opcode = VmOpcode::Pop;
      pop.width = 8;  // a cell of the argument stack holds a value
      pop.dst = *dst;
      emit(pop);
    }
    return {};
  }
};

}  // namespace

auto lower(llvm::Function& function, const VmOptions& options)
    -> std::expected<Lowered, VmFailure> {
  Lowerer lowerer(function, options);
  if (auto failure = lowerer.run(); !failure.has_value()) {
    return std::unexpected(failure.error());
  }
  Lowered result;
  result.code = lowerer.code();
  result.pool = lowerer.pool();
  result.call_sites = lowerer.call_sites();
  result.slots = lowerer.slot_count();
  result.local_cells = lowerer.local_cells();
  return result;
}

}  // namespace cs2::protect::detail
