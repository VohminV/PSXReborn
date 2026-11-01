#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "psxreborn/cpu.hpp"
#include "psxreborn/mips.hpp"

namespace psxreborn {

struct JitContext {
    uint32_t* regs = nullptr;
    uint32_t* hi = nullptr;
    uint32_t* lo = nullptr;
    uint64_t steps = 0;
    uint32_t nextPc = 0;
    uint32_t pendingActive = 0;
    uint32_t pendingReg = 0;
    uint32_t pendingValue = 0;
    uint32_t pendingMask = 0;
    uint32_t branchTaken = 0;
    uint32_t branchTarget = 0;
    uint32_t ioValue = 0;
    uint32_t ioMask = 0;
    uint32_t ioAux = 0;
    uint32_t flags = 0;
    uint32_t stopReason = 0;
    Cpu* cpu = nullptr;
    Memory* memory = nullptr;
    Gte* gte = nullptr;
    void* owner = nullptr;
};

class Jit {
public:
    Jit(Cpu& cpu, Memory& memory);
    ~Jit();

    Jit(const Jit&) = delete;
    Jit& operator=(const Jit&) = delete;

    CpuRunResult run(uint64_t maxSteps);
    void invalidateAll();
    void invalidateRange(uint32_t address, size_t size);
    uint64_t compiledBlocks() const { return blocks_.size(); }
    uint64_t executedBlocks() const { return executedBlocks_; }

private:
    struct Block {
        uint32_t start = 0;
        uint32_t end = 0;
        uint32_t instructionCount = 0;
        void (*code)(JitContext*) = nullptr;
    };

    using CodeFn = void (*)(JitContext*);

    Block* blockFor(uint32_t pc);
    void compile(uint32_t pc);
    void resetContext();
    void syncContextFromCpu();
    void syncCpuFromContext();
    void markCodePages(uint32_t start, uint32_t end);
    void releaseCodePages(uint32_t start, uint32_t end);
    uint8_t* allocate(size_t size);

    Cpu& cpu_;
    Memory& memory_;
    JitContext context_{};
    std::unordered_map<uint32_t, Block> blocks_;
    std::array<uint8_t, 513> codePages_{};
    struct Arena {
        uint8_t* data = nullptr;
        size_t size = 0;
    };
    std::vector<Arena> arenas_;
    size_t arenaOffset_ = 0;
    size_t arenaSize_ = 0;
    uint64_t executedBlocks_ = 0;
};

}  // namespace psxreborn
