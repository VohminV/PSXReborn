#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "psxreborn/gte.hpp"
#include "psxreborn/memory.hpp"
#include "psxreborn/mips.hpp"

namespace psxreborn {

class Memory;

enum class CpuStopReason {
    None,
    StepLimit,
    Breakpoint,
    Syscall,
    UnsupportedInstruction,
    UnsupportedMemory,
    AddressError,
    BusError,
    ArithmeticOverflow,
    AlreadyHalted,
};

enum class CpuExecutionMode {
    Debug,
    Hardware,
};

enum class CpuException : uint8_t {
    Interrupt = 0,
    AddressLoad = 4,
    AddressStore = 5,
    BusInstruction = 6,
    BusData = 7,
    Syscall = 8,
    Break = 9,
    ReservedInstruction = 10,
    CoprocessorUnusable = 11,
    Overflow = 12,
};

struct CpuStepResult {
    CpuStopReason reason = CpuStopReason::None;
    uint32_t pc = 0;
    uint32_t instruction = 0;
    uint32_t address = 0;
};

struct CpuRunResult {
    CpuStopReason reason = CpuStopReason::None;
    uint64_t steps = 0;
    uint32_t pc = 0;
    uint32_t instruction = 0;
    uint32_t address = 0;
};

class Cpu {
public:
    explicit Cpu(Memory& memory);

    void reset();
    void resetFromExe(uint32_t entryPoint, uint32_t globalPointer, uint32_t stackBase, uint32_t stackOffset);

    void setExecutionMode(CpuExecutionMode mode) { executionMode_ = mode; }
    CpuExecutionMode executionMode() const { return executionMode_; }
    void setInterruptSource(std::function<bool()> source) { interruptSource_ = std::move(source); }
    void setCycleCallback(std::function<void(uint32_t)> callback) { cycleCallback_ = std::move(callback); }
    void setInstructionHook(std::function<bool(uint32_t)> hook) { instructionHook_ = std::move(hook); }
    void setSyscallHandler(std::function<bool()> handler) { syscallHandler_ = std::move(handler); }

    CpuStepResult step();
    CpuRunResult run(uint64_t maxSteps);

    uint32_t pc() const { return pc_; }
    uint32_t hi() const { return hi_; }
    uint32_t lo() const { return lo_; }
    uint32_t reg(uint8_t index) const;
    void setPc(uint32_t value) { pc_ = value; }
    void setReg(uint8_t index, uint32_t value);
    void setHiLo(uint32_t hi, uint32_t lo);

    uint32_t cop0(uint8_t index) const;
    void setCop0(uint8_t index, uint32_t value);
    uint32_t status() const { return cop0_[12]; }
    uint32_t cause() const { return cop0_[13]; }
    uint32_t epc() const { return cop0_[14]; }
    uint32_t badVaddr() const { return cop0_[8]; }
    bool inException() const { return inException_; }

    Gte& gte() { return gte_; }
    const Gte& gte() const { return gte_; }

    bool halted() const { return halted_; }
    CpuStopReason haltReason() const { return haltReason_; }
    bool inDelaySlot() const { return inDelaySlot_; }
    bool pendingLoadActive() const { return pendingLoad_.active; }
    bool interruptPending() const;

private:
    struct PendingLoad {
        bool active = false;
        uint8_t reg = 0;
        uint32_t value = 0;
        uint32_t mask = 0;
    };

    CpuStepResult execute(const Instruction& instruction, uint32_t pc, PendingLoad& carried);
    CpuStepResult stop(CpuStopReason reason, uint32_t pc, uint32_t instruction, uint32_t address);
    CpuStepResult memoryFault(MemoryFault fault, uint32_t pc, uint32_t instruction, uint32_t address, bool store);
    CpuStepResult coprocessorUnusable(uint32_t pc, uint32_t instruction, uint8_t coprocessor);
    void raiseException(CpuException exception, uint32_t badAddress = 0, uint8_t coprocessor = 0);
    void syncInterruptCause();
    void rfe();
    bool cop0Usable() const;
    static bool validCop0Register(uint8_t index);
    static CpuStopReason reasonForFault(MemoryFault fault);

    Memory& memory_;
    Gte gte_;
    std::array<uint32_t, 32> regs_{};
    uint32_t pc_ = 0;
    uint32_t hi_ = 0;
    uint32_t lo_ = 0;
    PendingLoad pendingLoad_{};
    bool inDelaySlot_ = false;
    bool branchTaken_ = false;
    uint32_t branchTarget_ = 0;
    bool halted_ = false;
    CpuStopReason haltReason_ = CpuStopReason::None;
    uint32_t haltPc_ = 0;
    CpuExecutionMode executionMode_ = CpuExecutionMode::Debug;
    std::function<bool()> interruptSource_;
    std::function<void(uint32_t)> cycleCallback_;
    std::function<bool(uint32_t)> instructionHook_;
    std::function<bool()> syscallHandler_;
    std::array<uint32_t, 16> cop0_{};
    bool exceptionRaised_ = false;
    bool inException_ = false;
};

const char* toString(CpuStopReason reason);

}  // namespace psxreborn
