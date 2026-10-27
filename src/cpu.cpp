#include "psxreborn/cpu.hpp"

#include <limits>

namespace psxreborn {

Cpu::Cpu(Memory& memory) : memory_(memory) {
    reset();
}

void Cpu::reset() {
    regs_.fill(0);
    pc_ = 0xBFC00000u;
    hi_ = 0;
    lo_ = 0;
    pendingLoad_ = {};
    inDelaySlot_ = false;
    branchTaken_ = false;
    branchTarget_ = 0;
    halted_ = false;
    haltReason_ = CpuStopReason::None;
    haltPc_ = 0;
    gte_.reset();
    cop0_.fill(0);
    cop0_[12] = 0x10900000u;
    cop0_[15] = 0x00000002u;
    exceptionRaised_ = false;
    inException_ = false;
}

void Cpu::resetFromExe(uint32_t entryPoint, uint32_t globalPointer, uint32_t stackBase, uint32_t stackOffset) {
    reset();
    pc_ = entryPoint;
    regs_[28] = globalPointer;
    if (stackBase != 0) {
        const uint32_t stackPointer = stackBase + stackOffset;
        regs_[29] = stackPointer;
        regs_[30] = stackPointer;
    }
    regs_[4] = 1;
    regs_[5] = 0;
}

uint32_t Cpu::reg(uint8_t index) const {
    return regs_[index & 0x1Fu];
}

void Cpu::setReg(uint8_t index, uint32_t value) {
    if ((index & 0x1Fu) == 0) {
        return;
    }
    if (pendingLoad_.active && pendingLoad_.reg == (index & 0x1Fu)) {
        pendingLoad_ = {};
    }
    regs_[index & 0x1Fu] = value;
}

void Cpu::setHiLo(uint32_t hi, uint32_t lo) {
    hi_ = hi;
    lo_ = lo;
}

uint32_t Cpu::cop0(uint8_t index) const {
    return index < cop0_.size() ? cop0_[index] : 0;
}

void Cpu::setCop0(uint8_t index, uint32_t value) {
    if (index >= cop0_.size()) {
        return;
    }
    if (index == 13) {
        cop0_[13] = (cop0_[13] & ~0x0300u) | (value & 0x0300u);
        return;
    }
    cop0_[index] = value;
}

bool Cpu::validCop0Register(uint8_t index) {
    if (index > 15) {
        return true;
    }
    switch (index) {
        case 0:
        case 1:
        case 2:
        case 4:
        case 10:
            return false;
        default:
            return true;
    }
}

bool Cpu::cop0Usable() const {
    return (cop0_[12] & 0x2u) == 0 || (cop0_[12] & 0x10000000u) != 0;
}

void Cpu::syncInterruptCause() {
    const bool pending = interruptSource_ && interruptSource_();
    if (pending) {
        cop0_[13] |= 1u << 10;
    } else {
        cop0_[13] &= ~(1u << 10);
    }
}

bool Cpu::interruptPending() const {
    return (cop0_[12] & 1u) != 0 && (cop0_[12] & cop0_[13] & 0xFF00u) != 0;
}

void Cpu::rfe() {
    const uint32_t status = cop0_[12];
    cop0_[12] = (status & ~0x0000000Fu) | ((status & 0x0000003Cu) >> 2);
    inException_ = false;
}

void Cpu::raiseException(CpuException exception, uint32_t badAddress, uint8_t coprocessor) {
    if (exception == CpuException::AddressLoad || exception == CpuException::AddressStore) {
        cop0_[8] = badAddress;
    }
    uint32_t cause = cop0_[13] & 0x0000FF00u;
    cause |= static_cast<uint32_t>(exception) << 2;
    cause |= (static_cast<uint32_t>(coprocessor) & 0x3u) << 28;
    if (inDelaySlot_) {
        cause |= 0x80000000u;
        if (branchTaken_) {
            cause |= 0x40000000u;
            cop0_[6] = branchTarget_;
        }
    }
    cop0_[13] = cause;
    cop0_[14] = inDelaySlot_ ? pc_ - 4u : pc_;
    const uint32_t status = cop0_[12];
    cop0_[12] = (status & ~0x0000003Fu) | ((status & 0x0000000Fu) << 2);
    pc_ = (status & 0x00400000u) != 0 ? 0xBFC00180u : 0x80000080u;
    pendingLoad_ = {};
    inDelaySlot_ = false;
    branchTaken_ = false;
    branchTarget_ = 0;
    inException_ = true;
    exceptionRaised_ = true;
}

CpuStepResult Cpu::memoryFault(MemoryFault fault, uint32_t pc, uint32_t instruction, uint32_t address, bool store) {
    if (executionMode_ == CpuExecutionMode::Hardware) {
        switch (fault) {
            case MemoryFault::Unaligned:
                raiseException(store ? CpuException::AddressStore : CpuException::AddressLoad, address);
                return {};
            case MemoryFault::Unsupported:
            case MemoryFault::BusError:
                raiseException(CpuException::BusData);
                return {};
            case MemoryFault::None:
            default:
                return {};
        }
    }
    return stop(reasonForFault(fault), pc, instruction, address);
}

CpuStepResult Cpu::coprocessorUnusable(uint32_t pc, uint32_t instruction, uint8_t coprocessor) {
    if (executionMode_ == CpuExecutionMode::Hardware) {
        raiseException(CpuException::CoprocessorUnusable, 0, coprocessor);
        return {};
    }
    return stop(CpuStopReason::UnsupportedInstruction, pc, instruction, 0);
}

CpuStepResult Cpu::step() {
    if (halted_) {
        return {CpuStopReason::AlreadyHalted, haltPc_, 0, 0};
    }

    if (instructionHook_ && instructionHook_(pc_)) {
        return {};
    }

    syncInterruptCause();
    if (executionMode_ == CpuExecutionMode::Hardware && interruptPending()) {
        raiseException(CpuException::Interrupt);
        return {};
    }

    if ((pc_ & 3u) != 0) {
        if (executionMode_ == CpuExecutionMode::Hardware) {
            raiseException(CpuException::AddressLoad, pc_);
            return {};
        }
        return stop(CpuStopReason::AddressError, pc_, 0, pc_);
    }
    if (!memory_.isExecutable(pc_)) {
        if (executionMode_ == CpuExecutionMode::Hardware) {
            raiseException(CpuException::BusInstruction);
            return {};
        }
        return stop(CpuStopReason::BusError, pc_, 0, pc_);
    }

    MemoryFault fault = MemoryFault::None;
    uint32_t raw = 0;
    if (!memory_.read32(pc_, raw, &fault)) {
        if (executionMode_ == CpuExecutionMode::Hardware) {
            raiseException(CpuException::BusInstruction);
            return {};
        }
        return stop(reasonForFault(fault), pc_, 0, pc_);
    }

    const Instruction instruction = decodeInstruction(raw);
    if (!instruction.valid()) {
        if (executionMode_ == CpuExecutionMode::Hardware) {
            raiseException(CpuException::ReservedInstruction);
            return {};
        }
        return stop(CpuStopReason::UnsupportedInstruction, pc_, raw, 0);
    }

    if (inDelaySlot_ && instruction.isControlTransfer()) {
        return stop(CpuStopReason::UnsupportedInstruction, pc_, raw, 0);
    }

    PendingLoad carried = pendingLoad_;
    pendingLoad_ = {};

    const CpuStepResult result = execute(instruction, pc_, carried);
    if (carried.active) {
        regs_[carried.reg] = (regs_[carried.reg] & carried.mask) | carried.value;
    }
    if (exceptionRaised_) {
        exceptionRaised_ = false;
        return {};
    }
    if (result.reason != CpuStopReason::None) {
        return stop(result.reason, result.pc, result.instruction, result.address);
    }

    if (inDelaySlot_) {
        inDelaySlot_ = false;
        branchTaken_ = false;
        pc_ = branchTarget_;
    } else if (branchTaken_) {
        branchTaken_ = false;
        inDelaySlot_ = true;
        pc_ = pc_ + 4u;
    } else {
        pc_ = pc_ + 4u;
    }

    return {};
}

CpuRunResult Cpu::run(uint64_t maxSteps) {
    uint64_t steps = 0;
    while (steps < maxSteps) {
        const CpuStepResult result = step();
        if (result.reason != CpuStopReason::None) {
            return {result.reason, steps, result.pc, result.instruction, result.address};
        }
        ++steps;
        if (cycleCallback_) {
            cycleCallback_(1);
        }
    }
    return {CpuStopReason::StepLimit, steps, pc_, 0, 0};
}

CpuStepResult Cpu::execute(const Instruction& instruction, uint32_t pc, PendingLoad& carried) {
    const auto readReg = [this](uint8_t index) -> uint32_t {
        return index == 0 ? 0u : regs_[index];
    };

    const auto writeReg = [this, &carried](uint8_t index, uint32_t value) {
        if (index == 0) {
            return;
        }
        if (carried.active && carried.reg == index) {
            carried.active = false;
        }
        regs_[index] = value;
    };

    const auto scheduleLoad = [this, &carried](uint8_t index, uint32_t value) {
        if (index == 0) {
            return;
        }
        if (carried.active && carried.reg == index) {
            carried.active = false;
        }
        pendingLoad_ = {true, index, value, 0};
    };

    const auto schedulePartialLoad = [this](uint8_t index, uint32_t value, uint32_t mask) {
        if (index == 0) {
            return;
        }
        pendingLoad_ = {true, index, value, mask};
    };

    const auto branch = [this](uint32_t target) {
        branchTaken_ = true;
        branchTarget_ = target;
    };

    const uint32_t immediate = static_cast<uint32_t>(static_cast<int32_t>(instruction.imm));
    const uint8_t rs = instruction.rs;
    const uint8_t rt = instruction.rt;
    const uint8_t rd = instruction.rd;
    const uint32_t rsValue = readReg(rs);
    const uint32_t rtValue = readReg(rt);

    switch (instruction.opcode) {
        case Opcode::Invalid:
            return {CpuStopReason::UnsupportedInstruction, pc, instruction.raw, 0};

        case Opcode::Nop:
            return {};

        case Opcode::Add: {
            const uint32_t result = rsValue + rtValue;
            if ((((rsValue ^ result) & (rtValue ^ result)) >> 31) != 0) {
                if (executionMode_ == CpuExecutionMode::Hardware) {
                    raiseException(CpuException::Overflow);
                    return {};
                }
                return {CpuStopReason::ArithmeticOverflow, pc, instruction.raw, 0};
            }
            writeReg(rd, result);
            return {};
        }
        case Opcode::Addi: {
            const uint32_t result = rsValue + immediate;
            if ((((rsValue ^ result) & (immediate ^ result)) >> 31) != 0) {
                if (executionMode_ == CpuExecutionMode::Hardware) {
                    raiseException(CpuException::Overflow);
                    return {};
                }
                return {CpuStopReason::ArithmeticOverflow, pc, instruction.raw, 0};
            }
            writeReg(rt, result);
            return {};
        }
        case Opcode::Addiu:
            writeReg(rt, rsValue + immediate);
            return {};
        case Opcode::Addu:
            writeReg(rd, rsValue + rtValue);
            return {};
        case Opcode::Sub: {
            const uint32_t result = rsValue - rtValue;
            if ((((rsValue ^ result) & ((~rtValue) ^ result)) >> 31) != 0) {
                if (executionMode_ == CpuExecutionMode::Hardware) {
                    raiseException(CpuException::Overflow);
                    return {};
                }
                return {CpuStopReason::ArithmeticOverflow, pc, instruction.raw, 0};
            }
            writeReg(rd, result);
            return {};
        }
        case Opcode::Subu:
            writeReg(rd, rsValue - rtValue);
            return {};
        case Opcode::And:
            writeReg(rd, rsValue & rtValue);
            return {};
        case Opcode::Andi:
            writeReg(rt, rsValue & static_cast<uint32_t>(static_cast<uint16_t>(instruction.imm)));
            return {};
        case Opcode::Or:
            writeReg(rd, rsValue | rtValue);
            return {};
        case Opcode::Ori:
            writeReg(rt, rsValue | static_cast<uint32_t>(static_cast<uint16_t>(instruction.imm)));
            return {};
        case Opcode::Xor:
            writeReg(rd, rsValue ^ rtValue);
            return {};
        case Opcode::Xori:
            writeReg(rt, rsValue ^ static_cast<uint32_t>(static_cast<uint16_t>(instruction.imm)));
            return {};
        case Opcode::Nor:
            writeReg(rd, ~(rsValue | rtValue));
            return {};
        case Opcode::Sll:
            writeReg(rd, rtValue << instruction.shamt);
            return {};
        case Opcode::Srl:
            writeReg(rd, rtValue >> instruction.shamt);
            return {};
        case Opcode::Sra:
            writeReg(rd, static_cast<uint32_t>(static_cast<int32_t>(rtValue) >> instruction.shamt));
            return {};
        case Opcode::Sllv:
            writeReg(rd, rtValue << (rsValue & 0x1Fu));
            return {};
        case Opcode::Srlv:
            writeReg(rd, rtValue >> (rsValue & 0x1Fu));
            return {};
        case Opcode::Srav:
            writeReg(rd, static_cast<uint32_t>(static_cast<int32_t>(rtValue) >> (rsValue & 0x1Fu)));
            return {};
        case Opcode::Slt:
            writeReg(rd, static_cast<int32_t>(rsValue) < static_cast<int32_t>(rtValue) ? 1u : 0u);
            return {};
        case Opcode::Sltu:
            writeReg(rd, rsValue < rtValue ? 1u : 0u);
            return {};
        case Opcode::Slti:
            writeReg(rt, static_cast<int32_t>(rsValue) < static_cast<int32_t>(instruction.imm) ? 1u : 0u);
            return {};
        case Opcode::Sltiu:
            writeReg(rt, rsValue < immediate ? 1u : 0u);
            return {};
        case Opcode::Lui:
            writeReg(rt, static_cast<uint32_t>(static_cast<uint16_t>(instruction.imm)) << 16);
            return {};

        case Opcode::Mfhi:
            writeReg(rd, hi_);
            return {};
        case Opcode::Mflo:
            writeReg(rd, lo_);
            return {};
        case Opcode::Mthi:
            hi_ = rsValue;
            return {};
        case Opcode::Mtlo:
            lo_ = rsValue;
            return {};
        case Opcode::Mult: {
            const int64_t result = static_cast<int64_t>(static_cast<int32_t>(rsValue)) *
                                   static_cast<int64_t>(static_cast<int32_t>(rtValue));
            lo_ = static_cast<uint32_t>(result & 0xFFFFFFFFu);
            hi_ = static_cast<uint32_t>((static_cast<uint64_t>(result) >> 32) & 0xFFFFFFFFu);
            return {};
        }
        case Opcode::Multu: {
            const uint64_t result = static_cast<uint64_t>(rsValue) * static_cast<uint64_t>(rtValue);
            lo_ = static_cast<uint32_t>(result & 0xFFFFFFFFu);
            hi_ = static_cast<uint32_t>(result >> 32);
            return {};
        }
        case Opcode::Div: {
            const int32_t dividend = static_cast<int32_t>(rsValue);
            const int32_t divisor = static_cast<int32_t>(rtValue);
            if (divisor == 0) {
                hi_ = rsValue;
                lo_ = (rsValue & 0x80000000u) != 0 ? 1u : 0xFFFFFFFFu;
            } else if (rsValue == 0x80000000u && rtValue == 0xFFFFFFFFu) {
                lo_ = 0x80000000u;
                hi_ = 0;
            } else {
                lo_ = static_cast<uint32_t>(dividend / divisor);
                hi_ = static_cast<uint32_t>(dividend % divisor);
            }
            return {};
        }
        case Opcode::Divu: {
            if (rtValue == 0) {
                hi_ = rsValue;
                lo_ = 0xFFFFFFFFu;
            } else {
                lo_ = rsValue / rtValue;
                hi_ = rsValue % rtValue;
            }
            return {};
        }

        case Opcode::Beq:
            if (rsValue == rtValue) {
                branch(instruction.branchTarget(pc));
            }
            return {};
        case Opcode::Bne:
            if (rsValue != rtValue) {
                branch(instruction.branchTarget(pc));
            }
            return {};
        case Opcode::Blez:
            if (static_cast<int32_t>(rsValue) <= 0) {
                branch(instruction.branchTarget(pc));
            }
            return {};
        case Opcode::Bgtz:
            if (static_cast<int32_t>(rsValue) > 0) {
                branch(instruction.branchTarget(pc));
            }
            return {};
        case Opcode::Bltz:
            if (static_cast<int32_t>(rsValue) < 0) {
                branch(instruction.branchTarget(pc));
            }
            return {};
        case Opcode::Bgez:
            if (static_cast<int32_t>(rsValue) >= 0) {
                branch(instruction.branchTarget(pc));
            }
            return {};
        case Opcode::Bltzal:
            writeReg(31, pc + 8u);
            if (static_cast<int32_t>(rsValue) < 0) {
                branch(instruction.branchTarget(pc));
            }
            return {};
        case Opcode::Bgezal:
            writeReg(31, pc + 8u);
            if (static_cast<int32_t>(rsValue) >= 0) {
                branch(instruction.branchTarget(pc));
            }
            return {};
        case Opcode::J:
            branch(instruction.jumpTarget(pc));
            return {};
        case Opcode::Jal:
            writeReg(31, pc + 8u);
            branch(instruction.jumpTarget(pc));
            return {};
        case Opcode::Jr:
            branch(rsValue);
            return {};
        case Opcode::Jalr: {
            const uint32_t target = rsValue;
            if (rd != 0) {
                writeReg(rd, pc + 8u);
            }
            branch(target);
            return {};
        }

        case Opcode::Lb: {
            const uint32_t address = rsValue + immediate;
            uint8_t value = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read8(address, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, false);
            }
            scheduleLoad(rt, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(value))));
            return {};
        }
        case Opcode::Lbu: {
            const uint32_t address = rsValue + immediate;
            uint8_t value = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read8(address, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, false);
            }
            scheduleLoad(rt, value);
            return {};
        }
        case Opcode::Lh: {
            const uint32_t address = rsValue + immediate;
            uint16_t value = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read16(address, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, false);
            }
            scheduleLoad(rt, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value))));
            return {};
        }
        case Opcode::Lhu: {
            const uint32_t address = rsValue + immediate;
            uint16_t value = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read16(address, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, false);
            }
            scheduleLoad(rt, value);
            return {};
        }
        case Opcode::Lw: {
            const uint32_t address = rsValue + immediate;
            uint32_t value = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read32(address, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, false);
            }
            scheduleLoad(rt, value);
            return {};
        }
        case Opcode::Lwl: {
            const uint32_t address = rsValue + immediate;
            const uint32_t aligned = address & ~3u;
            uint32_t value = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read32(aligned, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, aligned, false);
            }
            static constexpr uint32_t masks[4] = {0x00FFFFFFu, 0x0000FFFFu, 0x000000FFu, 0x00000000u};
            static constexpr uint32_t byteMasks[4] = {0x000000FFu, 0x0000FFFFu, 0x00FFFFFFu, 0xFFFFFFFFu};
            static constexpr uint32_t shifts[4] = {24, 16, 8, 0};
            const uint32_t shift = address & 3u;
            value &= byteMasks[shift];
            schedulePartialLoad(rt, value << shifts[shift], masks[shift]);
            return {};
        }
        case Opcode::Lwr: {
            const uint32_t address = rsValue + immediate;
            const uint32_t aligned = address & ~3u;
            uint32_t value = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read32(aligned, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, aligned, false);
            }
            static constexpr uint32_t masks[4] = {0x00000000u, 0xFF000000u, 0xFFFF0000u, 0xFFFFFF00u};
            static constexpr uint32_t byteMasks[4] = {0xFFFFFFFFu, 0xFFFFFF00u, 0xFFFF0000u, 0xFF000000u};
            static constexpr uint32_t shifts[4] = {0, 8, 16, 24};
            const uint32_t shift = address & 3u;
            value &= byteMasks[shift];
            schedulePartialLoad(rt, value >> shifts[shift], masks[shift]);
            return {};
        }

        case Opcode::Sb: {
            const uint32_t address = rsValue + immediate;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.write8(address, static_cast<uint8_t>(rtValue & 0xFFu), &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, true);
            }
            return {};
        }
        case Opcode::Sh: {
            const uint32_t address = rsValue + immediate;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.write16(address, static_cast<uint16_t>(rtValue & 0xFFFFu), &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, true);
            }
            return {};
        }
        case Opcode::Sw: {
            const uint32_t address = rsValue + immediate;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.write32(address, rtValue, &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, true);
            }
            return {};
        }
        case Opcode::Swl: {
            const uint32_t address = rsValue + immediate;
            const uint32_t aligned = address & ~3u;
            uint32_t memory = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read32(aligned, memory, &fault)) {
                return memoryFault(fault, pc, instruction.raw, aligned, true);
            }
            static constexpr uint32_t masks[4] = {0xFFFFFF00u, 0xFFFF0000u, 0xFF000000u, 0x00000000u};
            static constexpr uint32_t shifts[4] = {24, 16, 8, 0};
            const uint32_t shift = address & 3u;
            const uint32_t value = (memory & masks[shift]) | (rtValue >> shifts[shift]);
            if (!memory_.write32(aligned, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, aligned, true);
            }
            return {};
        }
        case Opcode::Swr: {
            const uint32_t address = rsValue + immediate;
            const uint32_t aligned = address & ~3u;
            uint32_t memory = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read32(aligned, memory, &fault)) {
                return memoryFault(fault, pc, instruction.raw, aligned, true);
            }
            static constexpr uint32_t masks[4] = {0x00000000u, 0x000000FFu, 0x0000FFFFu, 0x00FFFFFFu};
            static constexpr uint32_t shifts[4] = {0, 8, 16, 24};
            const uint32_t shift = address & 3u;
            const uint32_t value = (memory & masks[shift]) | (rtValue << shifts[shift]);
            if (!memory_.write32(aligned, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, aligned, true);
            }
            return {};
        }

        case Opcode::Mfc0:
        case Opcode::Cfc0: {
            if (!cop0Usable()) {
                return coprocessorUnusable(pc, instruction.raw, 0);
            }
            if (!validCop0Register(rd)) {
                if (executionMode_ == CpuExecutionMode::Hardware) {
                    raiseException(CpuException::ReservedInstruction);
                    return {};
                }
                return {CpuStopReason::UnsupportedInstruction, pc, instruction.raw, 0};
            }
            scheduleLoad(rt, cop0(rd));
            return {};
        }
        case Opcode::Mtc0:
        case Opcode::Ctc0: {
            if (!cop0Usable()) {
                return coprocessorUnusable(pc, instruction.raw, 0);
            }
            if (!validCop0Register(rd)) {
                if (executionMode_ == CpuExecutionMode::Hardware) {
                    raiseException(CpuException::ReservedInstruction);
                    return {};
                }
                return {CpuStopReason::UnsupportedInstruction, pc, instruction.raw, 0};
            }
            setCop0(rd, rtValue);
            return {};
        }
        case Opcode::Rfe:
            if (!cop0Usable()) {
                return coprocessorUnusable(pc, instruction.raw, 0);
            }
            rfe();
            return {};
        case Opcode::Cop1:
        case Opcode::Lwc1:
        case Opcode::Swc1:
            return coprocessorUnusable(pc, instruction.raw, 1);
        case Opcode::Cop2:
            if ((cop0_[12] & 0x40000000u) == 0) {
                return coprocessorUnusable(pc, instruction.raw, 2);
            }
            gte_.execute(instruction.raw);
            return {};
        case Opcode::Mfc2:
        case Opcode::Cfc2:
            if ((cop0_[12] & 0x40000000u) == 0) {
                return coprocessorUnusable(pc, instruction.raw, 2);
            }
            scheduleLoad(rt, instruction.opcode == Opcode::Mfc2 ? gte_.readData(rd) : gte_.readControl(rd));
            return {};
        case Opcode::Mtc2:
        case Opcode::Ctc2:
            if ((cop0_[12] & 0x40000000u) == 0) {
                return coprocessorUnusable(pc, instruction.raw, 2);
            }
            if (instruction.opcode == Opcode::Mtc2) {
                gte_.writeData(rd, rtValue);
            } else {
                gte_.writeControl(rd, rtValue);
            }
            return {};
        case Opcode::Lwc2: {
            if ((cop0_[12] & 0x40000000u) == 0) {
                return coprocessorUnusable(pc, instruction.raw, 2);
            }
            const uint32_t address = rsValue + immediate;
            uint32_t value = 0;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.read32(address, value, &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, false);
            }
            gte_.writeData(rt, value);
            return {};
        }
        case Opcode::Swc2: {
            if ((cop0_[12] & 0x40000000u) == 0) {
                return coprocessorUnusable(pc, instruction.raw, 2);
            }
            const uint32_t address = rsValue + immediate;
            MemoryFault fault = MemoryFault::None;
            if (!memory_.write32(address, gte_.readData(rt), &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, true);
            }
            return {};
        }
        case Opcode::Syscall:
            if (executionMode_ == CpuExecutionMode::Hardware) {
                if (syscallHandler_ && syscallHandler_()) {
                    return {};
                }
                raiseException(CpuException::Syscall);
                return {};
            }
            return {CpuStopReason::Syscall, pc, instruction.raw, 0};
        case Opcode::Break:
            if (executionMode_ == CpuExecutionMode::Hardware) {
                raiseException(CpuException::Break);
                return {};
            }
            return {CpuStopReason::Breakpoint, pc, instruction.raw, 0};
    }

    return {CpuStopReason::UnsupportedInstruction, pc, instruction.raw, 0};
}

CpuStepResult Cpu::stop(CpuStopReason reason, uint32_t pc, uint32_t instruction, uint32_t address) {
    halted_ = true;
    haltReason_ = reason;
    haltPc_ = pc;
    return {reason, pc, instruction, address};
}

CpuStopReason Cpu::reasonForFault(MemoryFault fault) {
    switch (fault) {
        case MemoryFault::Unaligned:
            return CpuStopReason::AddressError;
        case MemoryFault::Unsupported:
            return CpuStopReason::UnsupportedMemory;
        case MemoryFault::BusError:
        case MemoryFault::None:
        default:
            return CpuStopReason::BusError;
    }
}

const char* toString(CpuStopReason reason) {
    switch (reason) {
        case CpuStopReason::None:
            return "none";
        case CpuStopReason::StepLimit:
            return "step limit";
        case CpuStopReason::Breakpoint:
            return "breakpoint";
        case CpuStopReason::Syscall:
            return "syscall";
        case CpuStopReason::UnsupportedInstruction:
            return "unsupported instruction";
        case CpuStopReason::UnsupportedMemory:
            return "unsupported memory region";
        case CpuStopReason::AddressError:
            return "address error";
        case CpuStopReason::BusError:
            return "bus error";
        case CpuStopReason::ArithmeticOverflow:
            return "arithmetic overflow";
        case CpuStopReason::AlreadyHalted:
            return "already halted";
    }
    return "unknown";
}

}  // namespace psxreborn
