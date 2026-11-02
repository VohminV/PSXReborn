#include "psxreborn/cpu.hpp"
#include "psxreborn/memory_watch.hpp"

#include <cstdio>
#include <cstdlib>

#include "psxreborn/jit.hpp"

#include <limits>
#include <unordered_map>

namespace psxreborn {
namespace {

// Live bounded trace window around the failing pad-driver wait (runtime debug only).
int gWaitWinArmed = 0;

int gCellWrites = 0;

void traceCellWrite(const char* op, uint32_t address, uint32_t value, uint32_t pc) {
    if (address == 0x1F801070u) {
        static int iStatPrints = 0;
        if (iStatPrints < 2000) {
            ++iStatPrints;
            std::fprintf(stderr, "[PW] istat-write %s v=%08X pc=0x%08X\n", op, value, pc);
        }
        return;
    }
    if ((address & ~0xCu) == 0x801CD370u && gCellWrites < 40) {
        ++gCellWrites;
        std::fprintf(stderr, "[PW] cell-write %s addr=%08X v=%08X pc=0x%08X\n", op, address, value,
                     pc);
    }
}

void waitWinArm() {
    if (const char* v = std::getenv("PSXREBORN_WAIT_WIN"); v != nullptr && gWaitWinArmed == 0) {
        gWaitWinArmed = std::atoi(v);
        if (gWaitWinArmed <= 0 || gWaitWinArmed > 100000) gWaitWinArmed = 128;
    }
}

int gIstatWinArmed = 0;

}  // anonymous namespace

// Load watch shared with the JIT path (see include/psxreborn/memory_watch.hpp).
bool memoryWatchEnabled() {
    static bool enabled = (std::getenv("PSXREBORN_WATCH") != nullptr);
    return enabled;
}

void memoryWatchLoad(uint32_t address, const uint32_t* value, uint32_t pc, uint32_t ra) {
    static bool initialized = false;
    static uint32_t base = 0;
    static uint32_t len = 0;
    if (!initialized) {
        initialized = true;
        const char* spec = std::getenv("PSXREBORN_WATCH");
        if (spec == nullptr) {
            len = 0;
        } else {
            uint32_t b = 0;
            unsigned l = 0;
            if (std::sscanf(spec, "%x:%x", &b, &l) == 2 && l != 0 && l < 0x100000) {
                base = b & 0x7FFFFFFFu;
                len = l;
                std::fprintf(stderr, "[WATCH] enabled %08X len %X\n", base, len);
            } else {
                len = 0;
            }
        }
    }
    if (len == 0) {
        return;
    }
    static std::unordered_map<uint32_t, uint32_t> last;
    const uint32_t phys = address & 0x7FFFFFFFu;
    if (phys < base || phys >= base + len) {
        return;
    }
    const uint32_t newVal = value != nullptr ? *value : 0;
    const auto it = last.find(phys);
    if (it == last.end() || it->second != newVal) {
        if (it == last.end()) {
            last.emplace(phys, newVal);
        } else {
            it->second = newVal;
        }
        std::fprintf(stderr, "[WATCH] v=%08X addr=%08X pc=%08X ra=%08X\n", newVal, address, pc,
                     ra);
    }
}

void memoryWatchStore(uint32_t address, uint32_t value, uint32_t pc) {
    static bool initialized = false;
    static uint32_t base = 0;
    static uint32_t len = 0;
    if (!initialized) {
        initialized = true;
        const char* spec = std::getenv("PSXREBORN_WSTORE");
        if (spec == nullptr) {
            len = 0;
        } else {
            uint32_t b = 0;
            unsigned l = 0;
            if (std::sscanf(spec, "%x:%x", &b, &l) == 2 && l != 0 && l < 0x100000) {
                base = b & 0x7FFFFFFFu;
                len = l;
                std::fprintf(stderr, "[WSTORE] enabled %08X len %X\n", base, len);
            } else {
                len = 0;
            }
        }
    }
    if (len == 0) {
        return;
    }
    const uint32_t phys = address & 0x7FFFFFFFu;
    if (phys < base || phys >= base + len) {
        return;
    }
    std::fprintf(stderr, "[WSTORE] addr=%08X v=%08X pc=%08X ra=%08X\n", address, value, pc, pc);
}

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

Cpu::~Cpu() = default;

void Cpu::setJitEnabled(bool enabled) {
    if (enabled && !jit_) {
        jit_ = std::make_unique<Jit>(*this, memory_);
    }
    jitEnabled_ = enabled;
    if (enabled && jit_) {
        jit_->invalidateAll();
    }
}

uint64_t Cpu::jitCompiledBlocks() const {
    return jit_ ? jit_->compiledBlocks() : 0;
}

uint64_t Cpu::jitExecutedBlocks() const {
    return jit_ ? jit_->executedBlocks() : 0;
}

CpuJitState Cpu::jitState() {
    CpuJitState state;
    state.regs = regs_.data();
    state.pc = &pc_;
    state.hi = &hi_;
    state.lo = &lo_;
    state.cop0 = cop0_.data();
    state.pendingActive = &pendingLoad_.active;
    state.pendingReg = &pendingLoad_.reg;
    state.pendingValue = &pendingLoad_.value;
    state.pendingMask = &pendingLoad_.mask;
    state.halted = &halted_;
    state.gte = &gte_;
    state.memory = &memory_;
    return state;
}

CpuStepResult Cpu::jitMemoryFault(MemoryFault fault, uint32_t pc, uint32_t instruction,
                                  uint32_t address, bool store, bool inDelaySlot,
                                  bool branchTaken, uint32_t branchTarget) {
    pc_ = pc;
    inDelaySlot_ = inDelaySlot;
    branchTaken_ = branchTaken;
    branchTarget_ = branchTarget;
    return memoryFault(fault, pc, instruction, address, store);
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

bool Cpu::pollInterrupt() {
    syncInterruptCause();
    if (executionMode_ == CpuExecutionMode::Hardware && interruptPending()) {
        raiseException(CpuException::Interrupt);
        return true;
    }
    return false;
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
    ++stats_.exceptions[static_cast<unsigned>(exception) & 0xFu];
    if (exception == CpuException::Interrupt) {
        ++stats_.interruptExceptions;
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
    static const bool exceptionTrace = std::getenv("PSXREBORN_EXCEPTION_LOG") != nullptr;
    if (exceptionTrace) {
        std::fprintf(stderr,
                     "[EXC] type=%u pc=%08x epc=%08x bad=%08x status=%08x inDelay=%u\n",
                     static_cast<unsigned>(exception), pc_, cop0_[14], badAddress,
                     cop0_[12], inDelaySlot_ ? 1u : 0u);
    }
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
    softTrace(pc_);
    if (gIstatWinArmed > 0 && pc_ >= 0x8004F420u && pc_ <= 0x8004F540u) {
        uint32_t istat = 0;
        uint32_t sr = 0;
        memory_.read32(0x1F801070u, istat, nullptr);
        memory_.read32(0x1F801044u, sr, nullptr);
        std::fprintf(stderr, "[IW] pc=%08X raw=%08X istat=%03X sr=%04X v0=%08X a0=%08X\n", pc_, raw,
                     istat & 0x3FFu, sr & 0xFFFFu, reg(2), reg(4));
        --gIstatWinArmed;
    }
    if (gWaitWinArmed > 0) {
        std::fprintf(stderr, "[CPUW] pc=0x%08X raw=%08X a4=%08X r9=%08X v0=%08X v1=%08X\n", pc_, raw,
                     reg(4), reg(9), reg(2), reg(3));
        --gWaitWinArmed;
    }
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
    if (jitEnabled_ && jit_ && executionMode_ == CpuExecutionMode::Hardware &&
        (!instructionHook_ || jitHookCompatible_)) {
        return jit_->run(maxSteps);
    }
    uint64_t steps = 0;
    const bool tracePc = std::getenv("PSXREBORN_PC_TRACE") != nullptr;
    static const char* trapEnv = std::getenv("PSXREBORN_PC_TRAP");
    static const uint32_t trapPc = trapEnv != nullptr
                                       ? static_cast<uint32_t>(std::strtoul(trapEnv, nullptr, 16))
                                       : 0u;
    static const char* trapBelowEnv = std::getenv("PSXREBORN_PC_TRAP_BELOW");
    static const uint32_t trapBelow = trapBelowEnv != nullptr
                                          ? static_cast<uint32_t>(
                                                std::strtoul(trapBelowEnv, nullptr, 16))
                                          : 0u;
    static const char* sampleEnv = std::getenv("PSXREBORN_PC_SAMPLE");
    static const uint64_t sampleInterval =
        sampleEnv != nullptr ? std::strtoull(sampleEnv, nullptr, 10) : 0;
    static uint32_t pcHistory[8192] = {};
    static unsigned pcHistoryCount = 0;
    static unsigned pcHistoryNext = 0;
    while (steps < maxSteps) {
        if (trapPc != 0 || trapBelow != 0) {
            pcHistory[pcHistoryNext] = pc_;
            pcHistoryNext = (pcHistoryNext + 1) % 8192;
            if (pcHistoryCount < 8192) {
                ++pcHistoryCount;
            }
            if ((trapPc != 0 && pc_ == trapPc) || (trapBelow != 0 && pc_ >= 0x80000000u && pc_ < trapBelow)) {
                std::fprintf(stderr, "[TRAP] pc=%08x steps=%llu history:\n", pc_,
                             static_cast<unsigned long long>(steps));
                for (unsigned index = 0; index < pcHistoryCount; ++index) {
                    const unsigned slot =
                        (pcHistoryNext + 8192 - pcHistoryCount + index) % 8192;
                    std::fprintf(stderr, "  %08x\n", pcHistory[slot]);
                }
                for (unsigned index = 1; index < 32; ++index) {
                    std::fprintf(stderr, "  r%u=%08x\n", index, regs_[index]);
                }
                return {CpuStopReason::Breakpoint, steps, pc_, 0, 0};
            }
        }
        if (tracePc) {
            std::fprintf(stderr, "T %llu %08x\n",
                         static_cast<unsigned long long>(steps), pc_);
        }
        if (sampleInterval != 0 && (steps % sampleInterval) == 0) {
            std::fprintf(stderr, "[PC] steps=%llu pc=%08x\n",
                         static_cast<unsigned long long>(steps), pc_);
        }
        const CpuStepResult result = step();
        if (result.reason != CpuStopReason::None) {
            return {result.reason, steps, result.pc, result.instruction, result.address};
        }
        ++steps;
        if (cycleCallback_) {
            cycleCallback_(kCyclesPerInstruction);
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
            if (std::getenv("PSXREBORN_TRACE_SIO_COMPILED") != nullptr &&
                (address & ~0x60000000u) == 0x1F801044u) {
                std::fprintf(stderr, "[CPU] lb STAT=%02X pc=%08X ra=%08X\n", value, pc, reg(31));
            }
            {
                const uint32_t v32 = value;
                psxreborn::memoryWatchLoad(address, &v32, pc, reg(31));
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
            if (std::getenv("PSXREBORN_TRACE_SIO_COMPILED") != nullptr &&
                (address & ~0x60000000u) == 0x1F801044u) {
                std::fprintf(stderr, "[CPU] lbu STAT=%02X pc=%08X ra=%08X\n", value, pc, reg(31));
            }
            {
                const uint32_t v32 = value;
                psxreborn::memoryWatchLoad(address, &v32, pc, reg(31));
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
            if (std::getenv("PSXREBORN_TRACE_SIO_COMPILED") != nullptr &&
                (address & ~0x60000000u) == 0x1F801044u) {
                std::fprintf(stderr, "[CPU] lh STAT=%04X pc=%08X ra=%08X\n", value, pc, reg(31));
            }
            psxreborn::memoryWatchLoad(address, nullptr, pc, reg(31));
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
            {
                const uint32_t v32 = value;
                psxreborn::memoryWatchLoad(address, &v32, pc, reg(31));
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
            if (std::getenv("PSXREBORN_TRACE_SIO_COMPILED") != nullptr &&
                (address & ~0x60000000u) == 0x1F801044u) {
                std::fprintf(stderr, "[CPU] lw STAT=%04X pc=%08X ra=%08X\n", value & 0xFFFFu, pc,
                             reg(31));
            }
            psxreborn::memoryWatchLoad(address, &value, pc, reg(31));
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
            psxreborn::memoryWatchLoad(address, &value, pc, reg(31));
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
            psxreborn::memoryWatchLoad(address, &value, pc, reg(31));
            schedulePartialLoad(rt, value >> shifts[shift], masks[shift]);
            return {};
        }

        case Opcode::Sb: {
            const uint32_t address = rsValue + immediate;
            if (const char* t = std::getenv("PSXREBORN_TRACE_SIO_COMPILED");
                t != nullptr &&
                ((address >= 0x1F801040u && address < 0x1F801050u) ||
                 (address >= 0x1F801120u && address < 0x1F80112Cu))) {
                std::fprintf(stderr, "[CPU] sb pc=0x%08X addr=%08X v=%02X ra=0x%08X\n", pc,
                             address, static_cast<unsigned>(rtValue & 0xFFu),
                             reg(31));
            }
            MemoryFault fault = MemoryFault::None;
            traceCellWrite("sb", address, rtValue & 0xFFu, pc);
            psxreborn::memoryWatchStore(address, rtValue & 0xFFu, pc);
            if (!memory_.write8(address, static_cast<uint8_t>(rtValue & 0xFFu), &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, true);
            }
            return {};
        }
        case Opcode::Sh: {
            const uint32_t address = rsValue + immediate;
            if (const char* t = std::getenv("PSXREBORN_TRACE_SIO_COMPILED");
                t != nullptr &&
                ((address >= 0x1F801040u && address < 0x1F801050u) ||
                 (address >= 0x1F801120u && address < 0x1F80112Cu))) {
                std::fprintf(stderr, "[CPU] sh pc=0x%08X addr=%08X v=%04X ra=0x%08X\n", pc,
                             address, static_cast<unsigned>(rtValue & 0xFFFFu), reg(31));
                static bool ramDumped = false;
                if (!ramDumped && pc == 0x8004F654u) {
                    ramDumped = true;
                    FILE* dump = std::fopen("/tmp/opencode/sio_ram.bin", "wb");
                    if (dump != nullptr) {
                        for (uint32_t base = 0x80000000u; base < 0x80200000u; base += 4) {
                            uint32_t word = 0;
                            memory_.read32(base, word, nullptr);
                            std::fwrite(&word, 1, 4, dump);
                        }
                        std::fclose(dump);
                    }
                }
            }
            MemoryFault fault = MemoryFault::None;
            traceCellWrite("sh", address, rtValue & 0xFFFFu, pc);
            psxreborn::memoryWatchStore(address, rtValue & 0xFFFFu, pc);
            if (!memory_.write16(address, static_cast<uint16_t>(rtValue & 0xFFFFu), &fault)) {
                return memoryFault(fault, pc, instruction.raw, address, true);
            }
            return {};
        }
        case Opcode::Sw: {
            const uint32_t address = rsValue + immediate;
            if (const char* t = std::getenv("PSXREBORN_TRACE_SIO_COMPILED");
                t != nullptr &&
                ((address >= 0x1F801040u && address < 0x1F801050u) ||
                 (address >= 0x1F801120u && address < 0x1F80112Cu))) {
                std::fprintf(stderr, "[CPU] sw pc=0x%08X addr=%08X v=%08X ra=0x%08X\n", pc,
                             address, rtValue, reg(31));
            }
            MemoryFault fault = MemoryFault::None;
            traceCellWrite("sw", address, rtValue, pc);
            psxreborn::memoryWatchStore(address, rtValue, pc);
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

void Cpu::softTrace(uint32_t pc) {
    static const bool enabled = std::getenv("PSXREBORN_SOFT_TRACE") != nullptr;
    if (!enabled) {
        return;
    }
    static uint64_t seq = 0;
    uint32_t phase = 0;
    uint32_t scratch = 0;
    auto rd32 = [&](uint32_t a, uint32_t& v) { memory_.read32(a, v, nullptr); };
    auto rd8v = [&](uint32_t a, uint8_t& v) { memory_.read8(a, v, nullptr); };
    switch (pc) {
        case 0x8004A5E4u: {
            static uint32_t fwCount = 0;
            if (++fwCount % 5000u != 1u) break;
            uint32_t statusPtr = 0;
            uint32_t qPtr = 0;
            memory_.read32(0x8006978u, statusPtr, nullptr);
            memory_.read32(0x8006980u, qPtr, nullptr);
            uint8_t statusByte = 0;
            uint32_t qCount = 0;
            if (statusPtr != 0) memory_.read8(statusPtr, statusByte, nullptr);
            if (qPtr != 0) memory_.read32(qPtr, qCount, nullptr);
            uint8_t slotByte = 0;
            memory_.read8(0x800672F0u, slotByte, nullptr);
            uint32_t b4C4 = 0;
            uint32_t b6BC = 0;
            memory_.read32(0x8006B4C4u, b4C4, nullptr);
            memory_.read32(0x80066BCu, b6BC, nullptr);
            uint8_t b6C0 = 0;
            memory_.read8(0x80066C0u, b6C0, nullptr);
            fprintf(stderr,
                    "[PW] frame-worker slot=%u status=%02X q=%04X b4C4=%04X b6BC=%04X b6C0=%04X\n",
                    slotByte, statusByte, qCount & 0xFFFFu, b4C4 & 0xFFFFu, b6BC & 0xFFFFu,
                    b6C0);
            break;
        }
        case 0x8004A3F8u: {
            uint32_t semAddr = 0;
            uint32_t semType = 0;
            memory_.read32(0x8006B0ACu, semAddr, nullptr);
            memory_.read32(0x8006B0A4u, semType, nullptr);
            fprintf(stderr, "[PW] [SEMFERM] a0=%08X a1=%08X sem=%08X type=%08X\n", reg(4), reg(5),
                    semAddr, semType);
            break;
        }
        case 0x8004E970u:
        case 0x8004E9B8u: {
            static unsigned copyIdx = 0;
            ++copyIdx;
            if (copyIdx % 40u == 1u) {
                char path[80];
                std::snprintf(path, sizeof(path), "/tmp/opencode/padgame%03u.bin", (copyIdx / 40u));
                FILE* dump = std::fopen(path, "wb");
                if (dump != nullptr) {
                    for (uint32_t base = 0x801BD2A0u; base < 0x801BD400u; base += 4) {
                        uint32_t word = 0;
                        memory_.read32(base, word, nullptr);
                        std::fwrite(&word, 1, 4, dump);
                    }
                    std::fclose(dump);
                }
            }
            uint32_t dst = reg(4);
            uint32_t src = reg(5);
            fprintf(stderr, "[PW] padcopy pc=%08X dst=%08X src=%08X:", pc + 8u, dst, src);
            if (src != 0) {
                for (uint32_t i = 0; i < 8; ++i) {
                    uint8_t byte = 0;
                    memory_.read8(src + i, byte, nullptr);
                    fprintf(stderr, " %02X", byte);
                }
            }
            fprintf(stderr, "\n");
            break;
        }
        case 0x80051364u: {
            static uint64_t seq51364 = 0;
            ++seq51364;
            uint32_t dst1 = 0;
            uint32_t s0v = reg(16);
            memory_.read32(s0v + 0x30u, dst1, nullptr);
            if (seq51364 % 8u == 1u) {
                fprintf(stderr, "[PW4] copydone s0=%08X dst=%08X:", s0v, dst1);
                for (uint32_t i = 0; i < 8; ++i) {
                    uint8_t byte = 0;
                    memory_.read8(dst1 + i, byte, nullptr);
                    fprintf(stderr, " %02X", byte);
                }
                fprintf(stderr, " n308=");
                for (uint32_t i = 0; i < 8; ++i) {
                    uint8_t byte = 0;
                    memory_.read8(0x801BD308u + i, byte, nullptr);
                    fprintf(stderr, " %02X", byte);
                }
                fprintf(stderr, "\n");
            }
            break;
        }
        case 0x80051340u: {
            static uint32_t s51340 = 0;
            static uint64_t seq51340 = 0;
            ++seq51340;
            s51340 = reg(16);
            uint32_t dstPtr = 0;
            if (s51340 != 0) memory_.read32(s51340 + 0x30u, dstPtr, nullptr);
            if (seq51340 % 64u == 1u) {
                uint8_t b35 = 0;
                uint8_t b46 = 0;
                rd8v(s51340 + 0x35u, b35);
                rd8v(s51340 + 0x46u, b46);
                fprintf(stderr, "[PW3] bytecopy s0=%08X dst=%08X count35=%02X cnt46=%02X\n",
                        s51340, dstPtr, b35, b46);
            }
            break;
        }
        case 0x8004F048u:
            rd32(0x80067490u, phase);
            fprintf(stderr, "[PW] dispatch-in phase=%u seq=%llu\n", phase,
                    (unsigned long long)(seq++));
            if (phase == 4) {
                static uint64_t dbIdx = 0;
                ++dbIdx;
                uint32_t bufPtr = 0;
                memory_.read32(0x801BD3C0u, bufPtr, nullptr);
                fprintf(stderr, "[PW2] poll-done#%llu bufptr=%08X buf=",
                        (unsigned long long)dbIdx, bufPtr);
                for (uint32_t i = 0; i < 8; ++i) {
                    uint8_t byte = 0;
                    memory_.read8(bufPtr + i, byte, nullptr);
                    fprintf(stderr, " %02X", byte);
                }
                fprintf(stderr, " node308=");
                for (uint32_t i = 0; i < 8; ++i) {
                    uint8_t byte = 0;
                    memory_.read8(0x801BD308u + i, byte, nullptr);
                    fprintf(stderr, " %02X", byte);
                }
                fprintf(stderr, " s0c4=");
                for (uint32_t i = 0x44; i < 0x4C; ++i) {
                    uint8_t byte = 0;
                    memory_.read8(0x801BD384u + i, byte, nullptr);
                    fprintf(stderr, " %02X", byte);
                }
                fprintf(stderr, "\n");
            }
            if (const char* env = std::getenv("PSXREBORN_STRUCT_DUMP");
                env != nullptr && phase == 0) {
                static unsigned dumpIdx = 0;
                static uint64_t dumpEvery = 0;
                static uint64_t dumpCount = 0;
                if (dumpEvery == 0) dumpEvery = std::strtoull(env, nullptr, 0);
                ++dumpCount;
                if (dumpCount % dumpEvery == 0) {
                    char path[80];
                    std::snprintf(path, sizeof(path), "/tmp/opencode/struct%04u.bin", dumpIdx++);
                    FILE* dump = std::fopen(path, "wb");
                    if (dump != nullptr) {
                        for (uint32_t base = 0x801BD380u; base < 0x801BD5C0u; base += 4) {
                            uint32_t word = 0;
                            memory_.read32(base, word, nullptr);
                            std::fwrite(&word, 1, 4, dump);
                        }
                        std::fclose(dump);
                    }
                }
            }
            break;
        case 0x8004F088u:
            rd32(0x80067490u, phase);
            fprintf(stderr, "[PW] dispatch-handler-ret a0=%08X phase=%u\n", reg(4), phase);
            break;
        case 0x8004F0C4u:
            rd32(0x80067490u, phase);
            fprintf(stderr, "[PW] post-wait phase=%u buffer0=%u\n", phase,
                    (rd32(0x80067490u, scratch), scratch = 0, 0));
            break;
        case 0x8004F0D4u:
            fprintf(stderr, "[PW] waitack ret v0=%08X\n", reg(2));
            break;
        case 0x8004F0E4u:
            fprintf(stderr, "[PW] deselect-err-fn a0=%08X\n", reg(4));
            break;
        case 0x8004F120u:
            fprintf(stderr, "[PW] deselect-fn a0=%08X ra=%08X\n", reg(4), reg(31));
            break;
        case 0x80050A0Cu:
            fprintf(stderr, "[PW] deselect-enter a0=%08X\n", reg(4));
            break;
        case 0x8004F138u:
            fprintf(stderr, "[PW] startTx s3=%08X (tx=%02X)\n", reg(7), (reg(7) & 0xFFu) ^ 0xFFu);
            break;
        case 0x8004F35Cu:
            fprintf(stderr, "[PW] send-func a0=%08X byte=%02X count=%u\n", reg(4), reg(5),
                    (rd32(0x80067490u, phase), 0));
            break;
        case 0x8004F3BCu: {
            uint32_t s0 = reg(4);
            uint32_t count = 0;
            rd32(0x80067490u, scratch);
            if (s0 != 0) {
                uint32_t word = 0;
                rd32(s0, word);  // struct base: read 0x44 count bytes after
                memory_.read32((s0 & ~3u) + 0x44, count, nullptr);
            }
            fprintf(stderr, "[PW] send-wait-rx s0=%08X count=%02X phase=%u\n", s0, count, scratch);
            break;
        }
        case 0x8004F3E4u: {
            uint32_t s0 = reg(4);
            uint32_t count = 0;
            rd32(0x80067490u, scratch);
            if (s0 != 0) memory_.read32((s0 & ~3u) + 0x44, count, nullptr);
            uint32_t joystat = 0;
            memory_.read32(0x1F801044u, joystat, nullptr);
            fprintf(stderr, "[PW] send-rx-read count=%02X phase=%u stat=%04X\n", count, scratch,
                    joystat & 0xFFFFu);
            break;
        }
        case 0x8004F430u:
            fprintf(stderr, "[PW] send-irq-check-passed (I_STAT bit7 set)\n");
            break;
        case 0x8004F438u: {
            waitWinArm();
            if (const char* v = std::getenv("PSXREBORN_ISTAT_WIN");
                v != nullptr && gIstatWinArmed == 0) {
                gIstatWinArmed = std::atoi(v);
                if (gIstatWinArmed <= 0 || gIstatWinArmed > 100000) gIstatWinArmed = 2000;
            }
            uint32_t stat = 0;
            uint32_t istat = 0;
            uint32_t imask = 0;
            memory_.read32(0x1F801044u, stat, nullptr);
            memory_.read32(0x1F801070u, istat, nullptr);
            memory_.read32(0x1F801074u, imask, nullptr);
            fprintf(stderr, "[PW] send-acklevel-wait-enter stat=%04X istat=%03X imask=%03X t0=%08X\n",
                    stat & 0xFFFFu, istat & 0x7FFu, imask & 0x7FFu, reg(8));
            static int hits = 0;
            ++hits;
            static bool ramDumpedWait = false;
            if (hits == 3 && !ramDumpedWait) {
                ramDumpedWait = true;
                FILE* dump = std::fopen("/tmp/opencode/live_wait.bin", "wb");
                if (dump != nullptr) {
                    for (uint32_t base = 0x8004E000u; base < 0x8004FFFFu; base += 4) {
                        uint32_t word = 0;
                        memory_.read32(base, word, nullptr);
                        std::fwrite(&word, 1, 4, dump);
                    }
                    std::fclose(dump);
                    fprintf(stderr, "[PW] dumped live RAM 0x8004E000-0x8004FFFF (hit3)\n");
                }
                // Dump the send-context struct (padman "s0" register) so the
                // tx state machine layout can be decoded offline.
                const uint32_t s0 = reg(19);
                fprintf(stderr, "[PW] wait3 s0=%08X a0=%08X a1=%08X s1=%08X\n",
                        s0, reg(4), reg(5), reg(17));
                if (s0 != 0) {
                    FILE* s0dump = std::fopen("/tmp/opencode/live_s0.bin", "wb");
                    if (s0dump != nullptr) {
                        const uint32_t base = s0 & ~3u;
                        for (uint32_t a = 0; a < 0x50; a += 1) {
                            uint8_t byte = 0;
                            memory_.read8(base + a, byte, nullptr);
                            std::fwrite(&byte, 1, 1, s0dump);
                        }
                        for (uint32_t a = 0; a < 0x200; a += 4) {
                            uint32_t word = 0;
                            memory_.read32(base + a, word, nullptr);
                            std::fwrite(&word, 1, 4, s0dump);
                        }
                        std::fclose(s0dump);
                    }
                }
            }
            break;
        }
        case 0x8004F5B0u: {
            uint32_t count = 0;
            uint32_t word = 0;
            uint32_t s0 = reg(4);
            uint32_t buff = 0;
            if (s0 != 0) rd32(s0 + 0x3C, buff);
            if (buff) memory_.read32(buff, word, nullptr);
            memory_.read32((s0 & ~3u) + 0x44, count, nullptr);
            fprintf(stderr, "[PW] send-func-exit rx=%02X countBefore+1=%02X buf0=%02X\n",
                    reg(2) & 0xFFu, count, word & 0xFFu);
            break;
        }
        case 0x80050234u:
            fprintf(stderr, "[PW] phase2-id-rx v0=%08X\n", reg(2));
            break;
        case 0x80050304u:
            fprintf(stderr, "[PW] phase3-id-rx v1=%08X\n", reg(3));
            break;
        case 0x8004F4F0u: {
            // Reached after the wait loop succeeded; inspect the fast-path
            // gate [s0+0xE8] and the phase field to trace which branch the
            // per-byte ACK-wait takes.
            uint32_t s0 = reg(19);
            uint32_t stage = 0;
            uint32_t phase = 0;
            if (s0 != 0) memory_.read32((s0 & ~3u) + 0xE8, stage, nullptr);
            memory_.read32(0x80067490u, phase, nullptr);
            uint32_t tmr2Count = 0;
            uint32_t tmr2Mode = 0;
            memory_.read32(0x1F801120u, tmr2Count, nullptr);
            memory_.read32(0x1F801124u, tmr2Mode, nullptr);
            fprintf(stderr,
                    "[PW] send-fast-path stage=%02X phase=%u tmr2=%04X mode=%04X\n",
                    stage & 0xFFu, phase, tmr2Count & 0xFFFFu, tmr2Mode & 0x3FFu);
            break;
        }
        case 0x8004F4DCu: {
            // Inside the deadline wait loop: report the current TMR2 state so
            // the padman budget can be validated against our timer model.
            uint32_t tmr2Count = 0;
            uint32_t tmr2Mode = 0;
            uint32_t istat = 0;
            memory_.read32(0x1F801120u, tmr2Count, nullptr);
            memory_.read32(0x1F801124u, tmr2Mode, nullptr);
            memory_.read32(0x1F801070u, istat, nullptr);
            static int loopPrints = 0;
            if (loopPrints < 16) {
                ++loopPrints;
                fprintf(stderr,
                        "[PW] send-wait-loop count=%04X mode=%04X istat=%03X a0=%08X a1=%08X\n",
                        tmr2Count & 0xFFFFu, tmr2Mode & 0x3FFu, istat & 0x7FFu, reg(4), reg(5));
            }
            break;
        }
        case 0x8004F4C0u:
            fprintf(stderr, "[PW] send-wait-deadline-hit v0=-2 a0=%08X a1=%08X\n", reg(4),
                    reg(5));
            break;
        case 0x8004F160u:
            fprintf(stderr, "[PW] startTx-len-set\n");
            break;
        case 0x8004F1B8u:
            fprintf(stderr, "[PW] startTx-stat-wait stat=%04X\n",
                    ([&] { uint32_t v = 0; memory_.read32(0x1F801044u, v, nullptr); return v; }()));
            break;
        case 0x80050008u:
            break;
        default:
            break;
    }
}

}  // namespace psxreborn
