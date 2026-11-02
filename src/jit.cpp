#include "psxreborn/jit.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "psxreborn/memory.hpp"
#include "psxreborn/memory_watch.hpp"

namespace psxreborn {

namespace {

constexpr int kRax = 0;
constexpr int kRcx = 1;
constexpr int kRdx = 2;
constexpr int kRbx = 3;
constexpr int kRsp = 4;
constexpr int kRbp = 5;
constexpr int kRsi = 6;
constexpr int kRdi = 7;
constexpr int kR8 = 8;
constexpr int kR9 = 9;
constexpr int kR10 = 10;
constexpr int kR11 = 11;
constexpr int kR12 = 12;
constexpr int kR13 = 13;
constexpr int kR14 = 14;
constexpr int kR15 = 15;

constexpr uint32_t kCf = 0;
constexpr uint32_t kIoValue = offsetof(JitContext, ioValue);
constexpr uint32_t kIoMask = offsetof(JitContext, ioMask);
constexpr uint32_t kIoAux = offsetof(JitContext, ioAux);
constexpr uint32_t kSteps = offsetof(JitContext, steps);
constexpr uint32_t kNextPc = offsetof(JitContext, nextPc);
constexpr uint32_t kPendingActive = offsetof(JitContext, pendingActive);
constexpr uint32_t kPendingReg = offsetof(JitContext, pendingReg);
constexpr uint32_t kPendingValue = offsetof(JitContext, pendingValue);
constexpr uint32_t kPendingMask = offsetof(JitContext, pendingMask);
constexpr uint32_t kBranchTaken = offsetof(JitContext, branchTaken);
constexpr uint32_t kBranchTarget = offsetof(JitContext, branchTarget);
constexpr uint32_t kFlags = offsetof(JitContext, flags);
constexpr uint32_t kHi = offsetof(JitContext, hi);
constexpr uint32_t kLo = offsetof(JitContext, lo);
constexpr uint32_t kGte = offsetof(JitContext, gte);

enum Condition {
    kO = 0,
    kNo = 1,
    kB = 2,
    kAe = 3,
    kE = 4,
    kNe = 5,
    kBe = 6,
    kA = 7,
    kS = 8,
    kNs = 9,
    kP = 10,
    kNp = 11,
    kL = 12,
    kGe = 13,
    kLe = 14,
    kG = 15,
};

class Emitter {
public:
    std::vector<uint8_t> code;

    size_t position() const { return code.size(); }

    void byte(uint8_t value) { code.push_back(value); }

    void dword(uint32_t value) {
        for (unsigned index = 0; index < 4; ++index) {
            code.push_back(static_cast<uint8_t>((value >> (index * 8)) & 0xFFu));
        }
    }

    void qword(uint64_t value) {
        for (unsigned index = 0; index < 8; ++index) {
            code.push_back(static_cast<uint8_t>((value >> (index * 8)) & 0xFFu));
        }
    }

    void rex(bool w, bool r, bool x, bool b) {
        const uint8_t value = static_cast<uint8_t>(0x40 | (w ? 8 : 0) | (r ? 4 : 0) |
                                                   (x ? 2 : 0) | (b ? 1 : 0));
        if (value != 0x40) {
            byte(value);
        }
    }

    void modrm(unsigned mod, unsigned reg, unsigned rm) {
        byte(static_cast<uint8_t>(((mod & 3u) << 6) | ((reg & 7u) << 3) | (rm & 7u)));
    }

    void sib(unsigned scale, unsigned index, unsigned base) {
        byte(static_cast<uint8_t>(((scale & 3u) << 6) | ((index & 7u) << 3) | (base & 7u)));
    }

    void memBase(int base, int32_t displacement) {
        rex(false, false, false, base >= 8);
        if (base == kRbp) {
            modrm(2, 0, 5);
            dword(static_cast<uint32_t>(displacement));
        } else if (base == kR12) {
            modrm(2, 0, 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, 0, static_cast<unsigned>(base & 7));
            dword(static_cast<uint32_t>(displacement));
        }
    }

    void memBaseIndex(int base, int index, int scale, int32_t displacement) {
        const bool needsDisp32 = displacement < -128 || displacement > 127;
        rex(false, false, index >= 8, base >= 8);
        modrm(needsDisp32 ? 2 : 1, 0, 4);
        unsigned scaleBits = 0;
        if (scale == 2) {
            scaleBits = 1;
        } else if (scale == 4) {
            scaleBits = 2;
        } else if (scale == 8) {
            scaleBits = 3;
        }
        sib(scaleBits, static_cast<unsigned>(index & 7), static_cast<unsigned>(base & 7));
        if (needsDisp32) {
            dword(static_cast<uint32_t>(displacement));
        } else {
            byte(static_cast<uint8_t>(static_cast<int8_t>(displacement)));
        }
    }

    void movRegImm(int reg, uint32_t value) {
        rex(false, false, false, reg >= 8);
        byte(static_cast<uint8_t>(0xB8 + (reg & 7)));
        dword(value);
    }

    void movReg64Imm(int reg, uint64_t value) {
        rex(true, false, false, reg >= 8);
        byte(static_cast<uint8_t>(0xB8 + (reg & 7)));
        qword(value);
    }

    void movRegReg(int destination, int source) {
        rex(false, source >= 8, false, destination >= 8);
        byte(0x89);
        modrm(3, static_cast<unsigned>(source & 7), static_cast<unsigned>(destination & 7));
    }

    void movReg64Reg(int destination, int source) {
        rex(true, source >= 8, false, destination >= 8);
        byte(0x89);
        modrm(3, static_cast<unsigned>(source & 7), static_cast<unsigned>(destination & 7));
    }

    void movRegMem(int reg, int base, int32_t displacement) {
        rex(false, reg >= 8, false, base >= 8);
        byte(0x8B);
        if (base == kRbp) {
            modrm(2, static_cast<unsigned>(reg & 7), 5);
            dword(static_cast<uint32_t>(displacement));
        } else if (base == kR12) {
            modrm(2, static_cast<unsigned>(reg & 7), 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, static_cast<unsigned>(reg & 7), static_cast<unsigned>(base & 7));
            dword(static_cast<uint32_t>(displacement));
        }
    }

    void movRegMemIndex(int reg, int base, int index, int scale, int32_t displacement) {
        const bool needsDisp32 = displacement < -128 || displacement > 127;
        rex(false, reg >= 8, index >= 8, base >= 8);
        byte(0x8B);
        modrm(needsDisp32 ? 2 : 1, static_cast<unsigned>(reg & 7), 4);
        unsigned scaleBits = 0;
        if (scale == 2) {
            scaleBits = 1;
        } else if (scale == 4) {
            scaleBits = 2;
        } else if (scale == 8) {
            scaleBits = 3;
        }
        sib(scaleBits, static_cast<unsigned>(index & 7), static_cast<unsigned>(base & 7));
        if (needsDisp32) {
            dword(static_cast<uint32_t>(displacement));
        } else {
            byte(static_cast<uint8_t>(static_cast<int8_t>(displacement)));
        }
    }

    void movMemReg(int base, int32_t displacement, int reg) {
        rex(false, reg >= 8, false, base >= 8);
        byte(0x89);
        if (base == kRbp) {
            modrm(2, static_cast<unsigned>(reg & 7), 5);
            dword(static_cast<uint32_t>(displacement));
        } else if (base == kR12) {
            modrm(2, static_cast<unsigned>(reg & 7), 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, static_cast<unsigned>(reg & 7), static_cast<unsigned>(base & 7));
            dword(static_cast<uint32_t>(displacement));
        }
    }

    void movMemRegIndex(int base, int index, int scale, int32_t displacement, int reg) {
        const bool needsDisp32 = displacement < -128 || displacement > 127;
        rex(false, reg >= 8, index >= 8, base >= 8);
        byte(0x89);
        modrm(needsDisp32 ? 2 : 1, static_cast<unsigned>(reg & 7), 4);
        unsigned scaleBits = 0;
        if (scale == 2) {
            scaleBits = 1;
        } else if (scale == 4) {
            scaleBits = 2;
        } else if (scale == 8) {
            scaleBits = 3;
        }
        sib(scaleBits, static_cast<unsigned>(index & 7), static_cast<unsigned>(base & 7));
        if (needsDisp32) {
            dword(static_cast<uint32_t>(displacement));
        } else {
            byte(static_cast<uint8_t>(static_cast<int8_t>(displacement)));
        }
    }

    void movMemImm32(int base, int32_t displacement, uint32_t value) {
        rex(false, false, false, base >= 8);
        byte(0xC7);
        if (base == kRbp) {
            modrm(2, 0, 5);
            dword(static_cast<uint32_t>(displacement));
        } else if (base == kR12) {
            modrm(2, 0, 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, 0, static_cast<unsigned>(base & 7));
            dword(static_cast<uint32_t>(displacement));
        }
        dword(value);
    }

    void movMemImm8(int base, int32_t displacement, uint8_t value) {
        rex(false, false, false, base >= 8);
        byte(0xC6);
        if (base == kRbp) {
            modrm(2, 0, 5);
            dword(static_cast<uint32_t>(displacement));
        } else if (base == kR12) {
            modrm(2, 0, 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, 0, static_cast<unsigned>(base & 7));
            dword(static_cast<uint32_t>(displacement));
        }
        byte(value);
    }

    void movReg64Mem(int reg, int base, int32_t displacement) {
        rex(true, reg >= 8, false, base >= 8);
        byte(0x8B);
        if (base == kRbp) {
            modrm(2, static_cast<unsigned>(reg & 7), 5);
            dword(static_cast<uint32_t>(displacement));
        } else if (base == kR12) {
            modrm(2, static_cast<unsigned>(reg & 7), 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, static_cast<unsigned>(reg & 7), static_cast<unsigned>(base & 7));
            dword(static_cast<uint32_t>(displacement));
        }
    }

    void movMem64Reg(int base, int32_t displacement, int reg) {
        rex(true, reg >= 8, false, base >= 8);
        byte(0x89);
        if (base == kRbp) {
            modrm(2, static_cast<unsigned>(reg & 7), 5);
            dword(static_cast<uint32_t>(displacement));
        } else if (base == kR12) {
            modrm(2, static_cast<unsigned>(reg & 7), 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, static_cast<unsigned>(reg & 7), static_cast<unsigned>(base & 7));
            dword(static_cast<uint32_t>(displacement));
        }
    }

    void opRegReg(uint8_t opcode, int destination, int source, bool wide = false) {
        rex(wide, source >= 8, false, destination >= 8);
        byte(opcode);
        modrm(3, static_cast<unsigned>(source & 7), static_cast<unsigned>(destination & 7));
    }

    void opRegImm(uint8_t operation, int reg, uint32_t immediate) {
        rex(false, false, false, reg >= 8);
        byte(0x81);
        modrm(3, operation, static_cast<unsigned>(reg & 7));
        dword(immediate);
    }

    void addRegImm(int reg, uint32_t value) { opRegImm(0, reg, value); }
    void subRegImm(int reg, uint32_t value) { opRegImm(5, reg, value); }
    void cmpRegImm(int reg, uint32_t value) { opRegImm(7, reg, value); }
    void andRegImm(int reg, uint32_t value) { opRegImm(4, reg, value); }
    void orRegImm(int reg, uint32_t value) { opRegImm(1, reg, value); }
    void xorRegImm(int reg, uint32_t value) { opRegImm(6, reg, value); }

    void addQwordMemImm8(int base, int32_t displacement, uint8_t value) {
        rex(true, false, false, base >= 8);
        byte(0x83);
        if (base == kR12) {
            modrm(2, 0, 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, 0, 5);
            dword(static_cast<uint32_t>(displacement));
        }
        byte(value);
    }

    void testRegReg(int a, int b) { opRegReg(0x85, a, b); }
    void addRegReg(int destination, int source) { opRegReg(0x01, destination, source); }
    void subRegReg(int destination, int source) { opRegReg(0x29, destination, source); }
    void andRegReg(int destination, int source) { opRegReg(0x21, destination, source); }
    void orRegReg(int destination, int source) { opRegReg(0x09, destination, source); }
    void xorRegReg(int destination, int source) { opRegReg(0x31, destination, source); }
    void cmpRegReg(int a, int b) { opRegReg(0x39, a, b); }

    void shiftImm(int reg, uint8_t operation, uint8_t amount) {
        rex(false, false, false, reg >= 8);
        byte(0xC1);
        modrm(3, operation, static_cast<unsigned>(reg & 7));
        byte(amount);
    }

    void shiftCl(int reg, uint8_t operation) {
        rex(false, false, false, reg >= 8);
        byte(0xD3);
        modrm(3, operation, static_cast<unsigned>(reg & 7));
    }

    void imulRegReg(int destination, int source) {
        rex(false, destination >= 8, false, source >= 8);
        byte(0x0F);
        byte(0xAF);
        modrm(3, static_cast<unsigned>(destination & 7), static_cast<unsigned>(source & 7));
    }

    void movsxdReg(int destination, int source) {
        rex(true, destination >= 8, false, source >= 8);
        byte(0x63);
        modrm(3, static_cast<unsigned>(destination & 7), static_cast<unsigned>(source & 7));
    }

    void movzxByte(int destination, int source) {
        rex(false, destination >= 8, false, source >= 8);
        byte(0x0F);
        byte(0xB6);
        modrm(3, static_cast<unsigned>(destination & 7), static_cast<unsigned>(source & 7));
    }

    void movsxByte(int destination, int source) {
        rex(false, destination >= 8, false, source >= 8);
        byte(0x0F);
        byte(0xBE);
        modrm(3, static_cast<unsigned>(destination & 7), static_cast<unsigned>(source & 7));
    }

    void movsxWord(int destination, int source) {
        rex(false, destination >= 8, false, source >= 8);
        byte(0x0F);
        byte(0xBF);
        modrm(3, static_cast<unsigned>(destination & 7), static_cast<unsigned>(source & 7));
    }

    void movzxWordMem(int destination, int base, int32_t displacement) {
        rex(false, destination >= 8, false, base >= 8);
        byte(0x0F);
        byte(0xB7);
        if (base == kR12) {
            modrm(2, static_cast<unsigned>(destination & 7), 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, static_cast<unsigned>(destination & 7), 5);
            dword(static_cast<uint32_t>(displacement));
        }
    }

    void movsxByteMem(int destination, int base, int32_t displacement) {
        rex(false, destination >= 8, false, base >= 8);
        byte(0x0F);
        byte(0xBE);
        if (base == kR12) {
            modrm(2, static_cast<unsigned>(destination & 7), 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, static_cast<unsigned>(destination & 7), 5);
            dword(static_cast<uint32_t>(displacement));
        }
    }

    void movsxWordMem(int destination, int base, int32_t displacement) {
        rex(false, destination >= 8, false, base >= 8);
        byte(0x0F);
        byte(0xBF);
        if (base == kR12) {
            modrm(2, static_cast<unsigned>(destination & 7), 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, static_cast<unsigned>(destination & 7), 5);
            dword(static_cast<uint32_t>(displacement));
        }
    }

    void movzxByteMem(int destination, int base, int32_t displacement) {
        rex(false, destination >= 8, false, base >= 8);
        byte(0x0F);
        byte(0xB6);
        if (base == kR12) {
            modrm(2, static_cast<unsigned>(destination & 7), 4);
            sib(0, 4, 4);
            dword(static_cast<uint32_t>(displacement));
        } else {
            modrm(2, static_cast<unsigned>(destination & 7), 5);
            dword(static_cast<uint32_t>(displacement));
        }
    }

    void setcc(uint8_t condition, int destination) {
        rex(false, false, false, destination >= 8);
        byte(0x0F);
        byte(static_cast<uint8_t>(0x90 + (condition & 0xFu)));
        modrm(3, 0, static_cast<unsigned>(destination & 7));
    }

    size_t jcc(uint8_t condition) {
        byte(0x0F);
        byte(static_cast<uint8_t>(0x80 + (condition & 0xFu)));
        const size_t offset = code.size();
        dword(0);
        return offset;
    }

    size_t jmp() {
        byte(0xE9);
        const size_t offset = code.size();
        dword(0);
        return offset;
    }

    void jmpTo(size_t target) {
        const int64_t relative = static_cast<int64_t>(target) - static_cast<int64_t>(code.size() + 5);
        byte(0xE9);
        dword(static_cast<uint32_t>(static_cast<int32_t>(relative)));
    }

    void patch(size_t offset, size_t target) {
        const int64_t relative = static_cast<int64_t>(target) - static_cast<int64_t>(offset + 4);
        const uint32_t value = static_cast<uint32_t>(static_cast<int32_t>(relative));
        for (unsigned index = 0; index < 4; ++index) {
            code[offset + index] = static_cast<uint8_t>((value >> (index * 8)) & 0xFFu);
        }
    }

    void callPointer(void* function) {
        movReg64Imm(kRax, reinterpret_cast<uint64_t>(function));
        rex(false, false, false, false);
        byte(0xFF);
        modrm(3, 2, static_cast<unsigned>(kRax));
    }

    void pushReg(int reg) {
        rex(false, false, false, reg >= 8);
        byte(static_cast<uint8_t>(0x50 + (reg & 7)));
    }

    void popReg(int reg) {
        rex(false, false, false, reg >= 8);
        byte(static_cast<uint8_t>(0x58 + (reg & 7)));
    }

    void xorReg(int reg, int other) { xorRegReg(reg, other); }
};

bool supported(const Instruction& instruction) {
    switch (instruction.opcode) {
        case Opcode::Nop:
        case Opcode::Addiu:
        case Opcode::Addu:
        case Opcode::Subu:
        case Opcode::And:
        case Opcode::Andi:
        case Opcode::Or:
        case Opcode::Ori:
        case Opcode::Xor:
        case Opcode::Xori:
        case Opcode::Nor:
        case Opcode::Sll:
        case Opcode::Srl:
        case Opcode::Sra:
        case Opcode::Sllv:
        case Opcode::Srlv:
        case Opcode::Srav:
        case Opcode::Slt:
        case Opcode::Sltu:
        case Opcode::Slti:
        case Opcode::Sltiu:
        case Opcode::Lui:
        case Opcode::Mfhi:
        case Opcode::Mflo:
        case Opcode::Mthi:
        case Opcode::Mtlo:
        case Opcode::Mult:
        case Opcode::Multu:
        case Opcode::Div:
        case Opcode::Divu:
        case Opcode::Beq:
        case Opcode::Bne:
        case Opcode::Blez:
        case Opcode::Bgtz:
        case Opcode::Bltz:
        case Opcode::Bgez:
        case Opcode::Bltzal:
        case Opcode::Bgezal:
        case Opcode::J:
        case Opcode::Jal:
        case Opcode::Jr:
        case Opcode::Jalr:
        case Opcode::Lb:
        case Opcode::Lbu:
        case Opcode::Lh:
        case Opcode::Lhu:
        case Opcode::Lw:
        case Opcode::Lwl:
        case Opcode::Lwr:
        case Opcode::Sb:
        case Opcode::Sh:
        case Opcode::Sw:
        case Opcode::Swl:
        case Opcode::Swr:
            return true;
        default:
            return false;
    }
}

struct DecodedInstruction {
    uint32_t pc = 0;
    Instruction instruction;
    bool delaySlot = false;
};

bool decodeBlock(Memory& memory, uint32_t start, std::vector<DecodedInstruction>& block) {
    uint32_t pc = start;
    for (unsigned count = 0; count < 64; ++count) {
        uint32_t raw = 0;
        MemoryFault fault = MemoryFault::None;
        if (!memory.read32(pc, raw, &fault)) {
            return false;
        }
        const Instruction instruction = decodeInstruction(raw);
        const uint32_t low = pc & 0xFFFFFFu;
        if (!instruction.valid() || !supported(instruction) || pc == 0xA0u || pc == 0xB0u ||
            pc == 0xC0u || (low >= 0xA000u && low < 0xA100u) ||
            (low >= 0xB000u && low < 0xB100u) || (low >= 0xC000u && low < 0xC100u)) {
            return false;
        }
        DecodedInstruction decoded{pc, instruction, false};
        if (!block.empty() && block.back().instruction.isControlTransfer()) {
            if (instruction.isControlTransfer()) {
                return false;
            }
            decoded.delaySlot = true;
        }
        block.push_back(decoded);
        pc += 4;
        if (!decoded.delaySlot && instruction.isControlTransfer()) {
            continue;
        }
        if (decoded.delaySlot) {
            return true;
        }
    }
    return true;
}

}  // namespace

extern "C" {

int jitRead(JitContext* context, uint32_t address, uint32_t size, uint32_t pc,
            uint32_t instruction, uint32_t flags) {
    MemoryFault fault = MemoryFault::None;
    uint32_t value = 0;
    bool ok = false;
    if (size == 1) {
        uint8_t byteValue = 0;
        ok = context->memory->read8(address, byteValue, &fault);
        value = byteValue;
    } else if (size == 2) {
        uint16_t halfValue = 0;
        ok = context->memory->read16(address, halfValue, &fault);
        value = halfValue;
    } else {
        ok = context->memory->read32(address, value, &fault);
    }
    if (!ok) {
        const CpuStepResult result = context->cpu->jitMemoryFault(
            fault, pc, instruction, address, false, (flags & 1u) != 0,
            context->branchTaken != 0, context->branchTarget);
        return result.reason != CpuStopReason::None ? 2 : 1;
    }
    if (psxreborn::memoryWatchEnabled()) {
        psxreborn::memoryWatchLoad(address, &value, pc, context->regs[31]);
    }
    context->ioValue = value;
    context->ioMask = 0xFFFFFFFFu;
    return 0;
}

int jitReadPartial(JitContext* context, uint32_t address, uint32_t pc, uint32_t instruction,
                   uint32_t flags) {
    const uint32_t aligned = address & ~3u;
    const uint32_t shift = address & 3u;
    uint32_t value = 0;
    MemoryFault fault = MemoryFault::None;
    if (!context->memory->read32(aligned, value, &fault)) {
        const CpuStepResult result = context->cpu->jitMemoryFault(
            fault, pc, instruction, aligned, false, (flags & 1u) != 0,
            context->branchTaken != 0, context->branchTarget);
        return result.reason != CpuStopReason::None ? 2 : 1;
    }
    if (psxreborn::memoryWatchEnabled()) {
        psxreborn::memoryWatchLoad(address, &value, pc, context->regs[31]);
    }
    static constexpr uint32_t leftMasks[4] = {0x00FFFFFFu, 0x0000FFFFu, 0x000000FFu, 0u};
    static constexpr uint32_t leftByteMasks[4] = {0x000000FFu, 0x0000FFFFu, 0x00FFFFFFu,
                                                  0xFFFFFFFFu};
    static constexpr uint32_t leftShifts[4] = {24, 16, 8, 0};
    static constexpr uint32_t rightMasks[4] = {0u, 0xFF000000u, 0xFFFF0000u, 0xFFFFFF00u};
    static constexpr uint32_t rightByteMasks[4] = {0xFFFFFFFFu, 0xFFFFFF00u, 0xFFFF0000u,
                                                   0xFF000000u};
    static constexpr uint32_t rightShifts[4] = {0, 8, 16, 24};
    if (context->ioAux == 0) {
        context->ioValue = (value & leftByteMasks[shift]) << leftShifts[shift];
        context->ioMask = leftMasks[shift];
    } else {
        context->ioValue = (value & rightByteMasks[shift]) >> rightShifts[shift];
        context->ioMask = rightMasks[shift];
    }
    return 0;
}

int jitWrite(JitContext* context, uint32_t address, uint32_t size, uint32_t pc,
             uint32_t instruction, uint32_t flags) {
    MemoryFault fault = MemoryFault::None;
    bool ok = false;
    const uint32_t value = context->ioValue;
    psxreborn::memoryWatchStore(address, value, pc);
    if (size == 1) {
        ok = context->memory->write8(address, static_cast<uint8_t>(value), &fault);
    } else if (size == 2) {
        ok = context->memory->write16(address, static_cast<uint16_t>(value), &fault);
    } else {
        ok = context->memory->write32(address, value, &fault);
    }
    if (!ok) {
        const CpuStepResult result = context->cpu->jitMemoryFault(
            fault, pc, instruction, address, true, (flags & 1u) != 0,
            context->branchTaken != 0, context->branchTarget);
        return result.reason != CpuStopReason::None ? 2 : 1;
    }
    return 0;
}

int jitWritePartial(JitContext* context, uint32_t address, uint32_t pc, uint32_t instruction,
                    uint32_t flags) {
    const uint32_t aligned = address & ~3u;
    const uint32_t shift = address & 3u;
    uint32_t memory = 0;
    MemoryFault fault = MemoryFault::None;
    if (!context->memory->read32(aligned, memory, &fault)) {
        const CpuStepResult result = context->cpu->jitMemoryFault(
            fault, pc, instruction, aligned, true, (flags & 1u) != 0,
            context->branchTaken != 0, context->branchTarget);
        return result.reason != CpuStopReason::None ? 2 : 1;
    }
    static constexpr uint32_t leftMasks[4] = {0xFFFFFF00u, 0xFFFF0000u, 0xFF000000u, 0u};
    static constexpr uint32_t leftShifts[4] = {24, 16, 8, 0};
    static constexpr uint32_t rightMasks[4] = {0u, 0x000000FFu, 0x0000FFFFu, 0x00FFFFFFu};
    static constexpr uint32_t rightShifts[4] = {0, 8, 16, 24};
    uint32_t value = 0;
    if (context->ioAux == 0) {
        value = (memory & leftMasks[shift]) | (context->ioValue >> leftShifts[shift]);
    } else {
        value = (memory & rightMasks[shift]) | (context->ioValue << rightShifts[shift]);
    }
    psxreborn::memoryWatchStore(address, value, pc);
    if (!context->memory->write32(aligned, value, &fault)) {
        const CpuStepResult result = context->cpu->jitMemoryFault(
            fault, pc, instruction, aligned, true, (flags & 1u) != 0,
            context->branchTaken != 0, context->branchTarget);
        return result.reason != CpuStopReason::None ? 2 : 1;
    }
    return 0;
}

void jitMultiply(JitContext* context, uint32_t a, uint32_t b, uint32_t isSigned) {
    if (isSigned != 0) {
        const int64_t result = static_cast<int64_t>(static_cast<int32_t>(a)) *
                               static_cast<int64_t>(static_cast<int32_t>(b));
        *context->lo = static_cast<uint32_t>(result);
        *context->hi = static_cast<uint32_t>(static_cast<uint64_t>(result) >> 32u);
    } else {
        const uint64_t result = static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
        *context->lo = static_cast<uint32_t>(result);
        *context->hi = static_cast<uint32_t>(result >> 32u);
    }
}

void jitDivide(JitContext* context, uint32_t a, uint32_t b, uint32_t isSigned) {
    if (isSigned != 0) {
        const int32_t dividend = static_cast<int32_t>(a);
        const int32_t divisor = static_cast<int32_t>(b);
        if (divisor == 0) {
            *context->hi = a;
            *context->lo = (a & 0x80000000u) != 0 ? 1u : 0xFFFFFFFFu;
        } else if (a == 0x80000000u && b == 0xFFFFFFFFu) {
            *context->lo = 0x80000000u;
            *context->hi = 0;
        } else {
            *context->lo = static_cast<uint32_t>(dividend / divisor);
            *context->hi = static_cast<uint32_t>(dividend % divisor);
        }
    } else {
        if (b == 0) {
            *context->hi = a;
            *context->lo = 0xFFFFFFFFu;
        } else {
            *context->lo = a / b;
            *context->hi = a % b;
        }
    }
}

}  // extern "C"

uint8_t* Jit::allocate(size_t size) {
    const size_t aligned = (size + 15u) & ~size_t{15u};
    if (arenas_.empty() || arenaOffset_ + aligned > arenas_.back().size) {
        size_t arenaSize = std::max<size_t>(aligned, 1024u * 1024u);
#if defined(_WIN32)
        uint8_t* data = static_cast<uint8_t*>(VirtualAlloc(
            nullptr, arenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#else
        void* mapping = mmap(nullptr, arenaSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        uint8_t* data = mapping == MAP_FAILED ? nullptr : static_cast<uint8_t*>(mapping);
#endif
        if (data == nullptr) {
            return nullptr;
        }
        arenas_.push_back({data, arenaSize});
        arenaOffset_ = 0;
        arenaSize_ = arenaSize;
    }
    uint8_t* result = arenas_.back().data + arenaOffset_;
    arenaOffset_ += aligned;
    return result;
}

Jit::Jit(Cpu& cpu, Memory& memory) : cpu_(cpu), memory_(memory) {
    memory_.setWriteObserver([this](uint32_t address, size_t size) {
        invalidateRange(address, size);
    });
}

Jit::~Jit() {
    memory_.setWriteObserver({});
    for (Arena& arena : arenas_) {
#if defined(_WIN32)
        VirtualFree(arena.data, 0, MEM_RELEASE);
#else
        munmap(arena.data, arena.size);
#endif
    }
}

void Jit::invalidateAll() {
    blocks_.clear();
    codePages_.fill(0);
}

void Jit::markCodePages(uint32_t start, uint32_t end) {
    const uint32_t physicalStart = start & 0x1FFFFFFFu;
    const uint32_t physicalEnd = end & 0x1FFFFFFFu;
    for (uint32_t address = physicalStart & ~0xFFFu; address < physicalEnd; address += 0x1000u) {
        const uint32_t page = address >> 12u;
        if (page < 512) {
            codePages_[page] = 1;
        }
    }
}

void Jit::invalidateRange(uint32_t address, size_t size) {
    const uint32_t physical = address & 0x1FFFFFFFu;
    if (physical >= Memory::kRamSize) {
        return;
    }
    const uint32_t firstPage = physical >> 12u;
    const uint32_t lastPage = (physical + static_cast<uint32_t>(size) - 1u) >> 12u;
    bool any = false;
    for (uint32_t page = firstPage; page <= lastPage && page < 512; ++page) {
        any = any || codePages_[page] != 0;
    }
    if (!any) {
        return;
    }
    for (auto iterator = blocks_.begin(); iterator != blocks_.end();) {
        const Block& block = iterator->second;
        const uint32_t blockStart = block.start & 0x1FFFFFFFu;
        const uint32_t blockEnd = block.end & 0x1FFFFFFFu;
        if (blockStart < physical + size && physical < blockEnd) {
            iterator = blocks_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    codePages_.fill(0);
    for (const auto& entry : blocks_) {
        markCodePages(entry.second.start, entry.second.end);
    }
}

void Jit::compile(uint32_t pc) {
    const uint32_t physical = pc & 0x1FFFFFFFu;
    if (physical >= Memory::kRamSize || !memory_.isExecutable(pc)) {
        return;
    }
    std::vector<DecodedInstruction> instructions;
    if (!decodeBlock(memory_, pc, instructions) || instructions.empty()) {
        return;
    }

    Emitter emitter;
    emitter.pushReg(kRbp);
    emitter.pushReg(kRbx);
    emitter.pushReg(kR12);
    emitter.pushReg(kR13);
    emitter.pushReg(kR14);
    emitter.pushReg(kR15);
    emitter.pushReg(kR10);
    emitter.movReg64Reg(kR12, kRdi);
    emitter.movReg64Mem(kRbp, kR12, static_cast<int32_t>(offsetof(JitContext, regs)));
    emitter.movRegMem(kR13, kR12, static_cast<int32_t>(kPendingActive));
    emitter.movRegMem(kR14, kR12, static_cast<int32_t>(kPendingReg));
    emitter.movRegMem(kR15, kR12, static_cast<int32_t>(kPendingValue));
    emitter.movRegMem(kRbx, kR12, static_cast<int32_t>(kPendingMask));

    auto loadReg = [&](int destination, uint8_t index) {
        if (index == 0) {
            emitter.xorReg(destination, destination);
        } else {
            emitter.movRegMem(destination, kRbp, static_cast<int32_t>(index) * 4);
        }
    };
    auto storeReg = [&](uint8_t index, int source) {
        if (index == 0) {
            return;
        }
        emitter.testRegReg(kR13, kR13);
        const size_t noPending = emitter.jcc(kE);
        emitter.cmpRegImm(kR14, index);
        const size_t notSame = emitter.jcc(kNe);
        emitter.xorReg(kR13, kR13);
        emitter.patch(notSame, emitter.position());
        emitter.patch(noPending, emitter.position());
        emitter.movMemReg(kRbp, static_cast<int32_t>(index) * 4, source);
    };
    auto applyPending = [&]() {
        emitter.testRegReg(kR13, kR13);
        const size_t skip = emitter.jcc(kE);
        emitter.movRegMemIndex(kRax, kRbp, kR14, 4, 0);
        emitter.andRegReg(kRax, kRbx);
        emitter.orRegReg(kRax, kR15);
        emitter.movMemRegIndex(kRbp, kR14, 4, 0, kRax);
        emitter.xorReg(kR13, kR13);
        emitter.patch(skip, emitter.position());
    };
    auto scheduleLoad = [&](uint8_t index, bool maskFromContext, bool cancelSame) {
        if (index == 0) {
            applyPending();
            return;
        }
        emitter.testRegReg(kR13, kR13);
        const size_t noPending = emitter.jcc(kE);
        size_t afterCancel = 0;
        if (cancelSame) {
            emitter.cmpRegImm(kR14, index);
            const size_t notSame = emitter.jcc(kNe);
            emitter.xorReg(kR13, kR13);
            const size_t canceled = emitter.jmp();
            emitter.patch(notSame, emitter.position());
            afterCancel = canceled;
        }
        applyPending();
        if (cancelSame) {
            emitter.patch(afterCancel, emitter.position());
        }
        emitter.patch(noPending, emitter.position());
        emitter.movRegImm(kR13, 1);
        emitter.movRegImm(kR14, index);
        emitter.movRegMem(kR15, kR12, static_cast<int32_t>(kIoValue));
        if (maskFromContext) {
            emitter.movRegMem(kRbx, kR12, static_cast<int32_t>(kIoMask));
        } else {
            emitter.movRegImm(kRbx, 0u);
        }
    };
    auto endInstruction = [&]() { emitter.addQwordMemImm8(kR12, kSteps, 1); };
    auto callHelper = [&](void* function) { emitter.callPointer(function); };
    auto faultCheck = [&]() {
        emitter.testRegReg(kRax, kRax);
        const size_t ok = emitter.jcc(kE);
        const size_t notException = emitter.jcc(kNe);
        emitter.addQwordMemImm8(kR12, kSteps, 1);
        const size_t bail = emitter.position();
        emitter.movMemImm32(kR12, kFlags, 1);
        const size_t jump = emitter.jmp();
        emitter.patch(notException, bail);
        emitter.patch(ok, emitter.position());
        return jump;
    };
    std::vector<size_t> bailJumps;

    auto emitRead = [&](const Instruction& instruction, uint32_t instructionPc, bool delay,
                        uint8_t destination, bool partial, bool left, int extension,
                        uint32_t accessSize) {
        loadReg(kRax, instruction.rs);
        emitter.addRegImm(kRax, static_cast<uint32_t>(static_cast<int32_t>(instruction.imm)));
        emitter.movRegReg(kRsi, kRax);
        emitter.movReg64Reg(kRdi, kR12);
        emitter.movRegImm(kRcx, instructionPc);
        emitter.movRegImm(kR8, instruction.raw);
        emitter.movRegImm(kR9, delay ? 1u : 0u);
        if (partial) {
            emitter.movRegReg(kRdx, kRcx);
            emitter.movMemImm32(kR12, kIoAux, left ? 0u : 1u);
            emitter.movRegReg(kRcx, kR8);
            emitter.movRegReg(kR8, kR9);
            callHelper(reinterpret_cast<void*>(&jitReadPartial));
        } else {
            emitter.movRegImm(kRdx, accessSize);
            callHelper(reinterpret_cast<void*>(&jitRead));
        }
        bailJumps.push_back(faultCheck());
        if (destination != 0) {
            if (extension == 2) {
                emitter.movzxByteMem(kRax, kR12, static_cast<int32_t>(kIoValue));
            } else if (extension == 4) {
                emitter.movzxWordMem(kRax, kR12, static_cast<int32_t>(kIoValue));
            } else if (extension == 5) {
                emitter.movsxByteMem(kRax, kR12, static_cast<int32_t>(kIoValue));
            } else if (extension == 6) {
                emitter.movsxWordMem(kRax, kR12, static_cast<int32_t>(kIoValue));
            } else {
                emitter.movRegMem(kRax, kR12, static_cast<int32_t>(kIoValue));
            }
            emitter.movMemReg(kR12, static_cast<int32_t>(kIoValue), kRax);
        }
        scheduleLoad(destination, partial, !partial);
        endInstruction();
    };

    for (const DecodedInstruction& decoded : instructions) {
        const Instruction& instruction = decoded.instruction;
        const uint32_t instructionPc = decoded.pc;
        switch (instruction.opcode) {
            case Opcode::Nop:
                applyPending();
                endInstruction();
                break;
            case Opcode::Lui:
                emitter.movRegImm(kRax, static_cast<uint32_t>(static_cast<uint16_t>(instruction.imm)) << 16u);
                storeReg(instruction.rt, kRax);
                applyPending();
                endInstruction();
                break;
            case Opcode::Addiu:
                loadReg(kRax, instruction.rs);
                emitter.addRegImm(kRax, static_cast<uint32_t>(static_cast<int32_t>(instruction.imm)));
                storeReg(instruction.rt, kRax);
                applyPending();
                endInstruction();
                break;
            case Opcode::Addu:
            case Opcode::Subu:
            case Opcode::And:
            case Opcode::Or:
            case Opcode::Xor: {
                loadReg(kRax, instruction.rs);
                loadReg(kRcx, instruction.rt);
                if (instruction.opcode == Opcode::Addu) {
                    emitter.addRegReg(kRax, kRcx);
                } else if (instruction.opcode == Opcode::Subu) {
                    emitter.subRegReg(kRax, kRcx);
                } else if (instruction.opcode == Opcode::And) {
                    emitter.andRegReg(kRax, kRcx);
                } else if (instruction.opcode == Opcode::Or) {
                    emitter.orRegReg(kRax, kRcx);
                } else {
                    emitter.xorRegReg(kRax, kRcx);
                }
                storeReg(instruction.rd, kRax);
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Nor:
                loadReg(kRax, instruction.rs);
                loadReg(kRcx, instruction.rt);
                emitter.orRegReg(kRax, kRcx);
                emitter.xorRegImm(kRax, 0xFFFFFFFFu);
                storeReg(instruction.rd, kRax);
                applyPending();
                endInstruction();
                break;
            case Opcode::Andi:
            case Opcode::Ori:
            case Opcode::Xori: {
                loadReg(kRax, instruction.rs);
                const uint32_t immediate = static_cast<uint32_t>(static_cast<uint16_t>(instruction.imm));
                if (instruction.opcode == Opcode::Andi) {
                    emitter.andRegImm(kRax, immediate);
                } else if (instruction.opcode == Opcode::Ori) {
                    emitter.orRegImm(kRax, immediate);
                } else {
                    emitter.xorRegImm(kRax, immediate);
                }
                storeReg(instruction.rt, kRax);
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Sll:
            case Opcode::Srl:
            case Opcode::Sra: {
                loadReg(kRax, instruction.rt);
                const uint8_t operation = instruction.opcode == Opcode::Sll ? 4
                                          : instruction.opcode == Opcode::Srl ? 5
                                                                              : 7;
                emitter.shiftImm(kRax, operation, instruction.shamt);
                storeReg(instruction.rd, kRax);
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Sllv:
            case Opcode::Srlv:
            case Opcode::Srav: {
                loadReg(kRax, instruction.rt);
                loadReg(kRcx, instruction.rs);
                emitter.andRegImm(kRcx, 0x1Fu);
                const uint8_t operation = instruction.opcode == Opcode::Sllv ? 4
                                          : instruction.opcode == Opcode::Srlv ? 5
                                                                               : 7;
                emitter.shiftCl(kRax, operation);
                storeReg(instruction.rd, kRax);
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Slt:
            case Opcode::Sltu: {
                loadReg(kRax, instruction.rs);
                loadReg(kRcx, instruction.rt);
                emitter.cmpRegReg(kRax, kRcx);
                emitter.setcc(instruction.opcode == Opcode::Slt ? kL : kB, kRax);
                emitter.movzxByte(kRax, kRax);
                storeReg(instruction.rd, kRax);
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Slti:
            case Opcode::Sltiu: {
                loadReg(kRax, instruction.rs);
                emitter.cmpRegImm(kRax, static_cast<uint32_t>(static_cast<int32_t>(instruction.imm)));
                emitter.setcc(instruction.opcode == Opcode::Slti ? kL : kB, kRax);
                emitter.movzxByte(kRax, kRax);
                storeReg(instruction.rt, kRax);
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Mfhi:
            case Opcode::Mflo: {
                emitter.movReg64Mem(kRax, kR12, static_cast<int32_t>(
                                                    instruction.opcode == Opcode::Mfhi ? kHi : kLo));
                emitter.movRegMem(kRax, kRax, 0);
                storeReg(instruction.rd, kRax);
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Mthi:
            case Opcode::Mtlo: {
                loadReg(kRax, instruction.rs);
                emitter.movReg64Mem(kRcx, kR12, static_cast<int32_t>(
                                                    instruction.opcode == Opcode::Mthi ? kHi : kLo));
                emitter.movMemReg(kRcx, 0, kRax);
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Mult:
            case Opcode::Multu:
            case Opcode::Div:
            case Opcode::Divu: {
                loadReg(kRax, instruction.rs);
                loadReg(kRcx, instruction.rt);
                emitter.movRegReg(kRsi, kRax);
                emitter.movRegReg(kRdx, kRcx);
                emitter.movReg64Reg(kRdi, kR12);
                emitter.movRegImm(kRcx, (instruction.opcode == Opcode::Mult ||
                                         instruction.opcode == Opcode::Div)
                                            ? 1u
                                            : 0u);
                callHelper(reinterpret_cast<void*>(instruction.opcode == Opcode::Mult ||
                                                           instruction.opcode == Opcode::Multu
                                                       ? &jitMultiply
                                                       : &jitDivide));
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Lb:
                emitRead(instruction, instructionPc, decoded.delaySlot, instruction.rt, false, false, 5, 1);
                break;
            case Opcode::Lbu:
                emitRead(instruction, instructionPc, decoded.delaySlot, instruction.rt, false, false, 2, 1);
                break;
            case Opcode::Lh:
                emitRead(instruction, instructionPc, decoded.delaySlot, instruction.rt, false, false, 6, 2);
                break;
            case Opcode::Lhu:
                emitRead(instruction, instructionPc, decoded.delaySlot, instruction.rt, false, false, 4, 2);
                break;
            case Opcode::Lw:
                emitRead(instruction, instructionPc, decoded.delaySlot, instruction.rt, false, false, 0, 4);
                break;
            case Opcode::Lwl:
                emitRead(instruction, instructionPc, decoded.delaySlot, instruction.rt, true, true, 0, 4);
                break;
            case Opcode::Lwr:
                emitRead(instruction, instructionPc, decoded.delaySlot, instruction.rt, true, false, 0, 4);
                break;
            case Opcode::Sb:
            case Opcode::Sh:
            case Opcode::Sw: {
                loadReg(kRax, instruction.rs);
                emitter.addRegImm(kRax, static_cast<uint32_t>(static_cast<int32_t>(instruction.imm)));
                emitter.movRegReg(kRsi, kRax);
                loadReg(kRax, instruction.rt);
                emitter.movMemReg(kR12, static_cast<int32_t>(kIoValue), kRax);
                emitter.movReg64Reg(kRdi, kR12);
                emitter.movRegImm(kRdx, instruction.opcode == Opcode::Sb ? 1u
                                             : instruction.opcode == Opcode::Sh ? 2u
                                                                                : 4u);
                emitter.movRegImm(kRcx, instructionPc);
                emitter.movRegImm(kR8, instruction.raw);
                emitter.movRegImm(kR9, decoded.delaySlot ? 1u : 0u);
                callHelper(reinterpret_cast<void*>(&jitWrite));
                bailJumps.push_back(faultCheck());
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Swl:
            case Opcode::Swr: {
                loadReg(kRax, instruction.rs);
                emitter.addRegImm(kRax, static_cast<uint32_t>(static_cast<int32_t>(instruction.imm)));
                emitter.movRegReg(kRsi, kRax);
                loadReg(kRax, instruction.rt);
                emitter.movMemReg(kR12, static_cast<int32_t>(kIoValue), kRax);
                emitter.movMemImm32(kR12, kIoAux, instruction.opcode == Opcode::Swl ? 0u : 1u);
                emitter.movReg64Reg(kRdi, kR12);
                emitter.movRegImm(kRdx, instructionPc);
                emitter.movRegImm(kRcx, instruction.raw);
                emitter.movRegImm(kR8, decoded.delaySlot ? 1u : 0u);
                callHelper(reinterpret_cast<void*>(&jitWritePartial));
                bailJumps.push_back(faultCheck());
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Beq:
            case Opcode::Bne:
            case Opcode::Blez:
            case Opcode::Bgtz:
            case Opcode::Bltz:
            case Opcode::Bgez:
            case Opcode::Bltzal:
            case Opcode::Bgezal: {
                const uint32_t target = instruction.branchTarget(instructionPc);
                if (instruction.opcode == Opcode::Bltzal ||
                    instruction.opcode == Opcode::Bgezal) {
                    emitter.movRegImm(kRax, instructionPc + 8u);
                    storeReg(31, kRax);
                }
                if (instruction.opcode == Opcode::Beq || instruction.opcode == Opcode::Bne) {
                    loadReg(kRax, instruction.rs);
                    loadReg(kRcx, instruction.rt);
                    emitter.cmpRegReg(kRax, kRcx);
                } else if (instruction.opcode == Opcode::Blez ||
                           instruction.opcode == Opcode::Bgtz) {
                    loadReg(kRax, instruction.rs);
                    emitter.cmpRegImm(kRax, 0);
                } else {
                    loadReg(kRax, instruction.rs);
                    emitter.cmpRegImm(kRax, 0);
                }
                uint8_t takenCondition = kE;
                if (instruction.opcode == Opcode::Bne) {
                    takenCondition = kNe;
                } else if (instruction.opcode == Opcode::Blez) {
                    takenCondition = kLe;
                } else if (instruction.opcode == Opcode::Bgtz) {
                    takenCondition = kG;
                } else if (instruction.opcode == Opcode::Bltz ||
                           instruction.opcode == Opcode::Bltzal) {
                    takenCondition = kL;
                } else if (instruction.opcode == Opcode::Bgez ||
                           instruction.opcode == Opcode::Bgezal) {
                    takenCondition = kGe;
                }
                const size_t notTaken = emitter.jcc(static_cast<uint8_t>(takenCondition ^ 1u));
                emitter.movMemImm32(kR12, kBranchTaken, 1);
                emitter.movMemImm32(kR12, kBranchTarget, target);
                emitter.movMemImm32(kR12, kNextPc, target);
                const size_t done = emitter.jmp();
                emitter.patch(notTaken, emitter.position());
                emitter.movMemImm32(kR12, kBranchTaken, 0);
                emitter.movMemImm32(kR12, kNextPc, instructionPc + 8u);
                emitter.patch(done, emitter.position());
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::J:
            case Opcode::Jal: {
                const uint32_t target = instruction.jumpTarget(instructionPc);
                if (instruction.opcode == Opcode::Jal) {
                    emitter.movRegImm(kRax, instructionPc + 8u);
                    storeReg(31, kRax);
                }
                emitter.movMemImm32(kR12, kBranchTaken, 1);
                emitter.movMemImm32(kR12, kBranchTarget, target);
                emitter.movMemImm32(kR12, kNextPc, target);
                applyPending();
                endInstruction();
                break;
            }
            case Opcode::Jr:
            case Opcode::Jalr: {
                loadReg(kRax, instruction.rs);
                emitter.movRegReg(kRdx, kRax);
                if (instruction.opcode == Opcode::Jalr && instruction.rd != 0) {
                    emitter.movRegImm(kRax, instructionPc + 8u);
                    storeReg(instruction.rd, kRax);
                }
                emitter.movMemReg(kR12, static_cast<int32_t>(kBranchTarget), kRdx);
                emitter.movMemReg(kR12, static_cast<int32_t>(kNextPc), kRdx);
                emitter.movMemImm32(kR12, kBranchTaken, 1);
                applyPending();
                endInstruction();
                break;
            }
            default:
                break;
        }
    }

    const bool endsWithBranch = !instructions.empty() &&
                                (instructions.back().delaySlot ||
                                 instructions.back().instruction.isControlTransfer());
    if (!endsWithBranch) {
        emitter.movMemImm32(kR12, kNextPc, instructions.back().pc + 4u);
    }
    const size_t epilogue = emitter.position();
    emitter.movMemReg(kR12, static_cast<int32_t>(kPendingActive), kR13);
    emitter.movMemReg(kR12, static_cast<int32_t>(kPendingReg), kR14);
    emitter.movMemReg(kR12, static_cast<int32_t>(kPendingValue), kR15);
    emitter.movMemReg(kR12, static_cast<int32_t>(kPendingMask), kRbx);
    emitter.popReg(kR10);
    emitter.popReg(kR15);
    emitter.popReg(kR14);
    emitter.popReg(kR13);
    emitter.popReg(kR12);
    emitter.popReg(kRbx);
    emitter.popReg(kRbp);
    emitter.byte(0xC3);
    for (size_t jump : bailJumps) {
        emitter.patch(jump, epilogue);
    }

    uint8_t* code = allocate(emitter.code.size());
    if (code == nullptr) {
        return;
    }
    std::memcpy(code, emitter.code.data(), emitter.code.size());
#if defined(__GNUC__) && !defined(_WIN32)
    __builtin___clear_cache(reinterpret_cast<char*>(code),
                            reinterpret_cast<char*>(code + emitter.code.size()));
#endif
    Block block;
    block.start = pc;
    block.end = instructions.back().pc + 4u;
    block.instructionCount = static_cast<uint32_t>(instructions.size());
    block.code = reinterpret_cast<CodeFn>(code);
    blocks_[pc] = block;
    markCodePages(block.start, block.end);
}

Jit::Block* Jit::blockFor(uint32_t pc) {
    const auto iterator = blocks_.find(pc);
    if (iterator != blocks_.end()) {
        return &iterator->second;
    }
    compile(pc);
    const auto compiled = blocks_.find(pc);
    return compiled == blocks_.end() ? nullptr : &compiled->second;
}

void Jit::syncContextFromCpu() {
    const CpuJitState state = cpu_.jitState();
    context_.regs = state.regs;
    context_.hi = state.hi;
    context_.lo = state.lo;
    context_.cpu = &cpu_;
    context_.memory = &memory_;
    context_.gte = state.gte;
    context_.pendingActive = *state.pendingActive ? 1u : 0u;
    context_.pendingReg = *state.pendingReg;
    context_.pendingValue = *state.pendingValue;
    context_.pendingMask = *state.pendingMask;
    context_.steps = 0;
    context_.flags = 0;
    context_.branchTaken = 0;
    context_.branchTarget = 0;
    context_.nextPc = cpu_.pc();
}

void Jit::syncCpuFromContext() {
    CpuJitState state = cpu_.jitState();
    if (state.regs != nullptr && context_.regs != nullptr) {
        std::memcpy(state.regs, context_.regs, 32 * sizeof(uint32_t));
    }
    if (state.pc != nullptr) {
        *state.pc = context_.nextPc;
    }
    if (state.hi != nullptr && context_.hi != nullptr) {
        *state.hi = *context_.hi;
    }
    if (state.lo != nullptr && context_.lo != nullptr) {
        *state.lo = *context_.lo;
    }
    if (state.pendingActive != nullptr) {
        *state.pendingActive = context_.pendingActive != 0;
    }
    if (state.pendingReg != nullptr) {
        *state.pendingReg = context_.pendingReg;
    }
    if (state.pendingValue != nullptr) {
        *state.pendingValue = context_.pendingValue;
    }
    if (state.pendingMask != nullptr) {
        *state.pendingMask = context_.pendingMask;
    }
}

CpuRunResult Jit::run(uint64_t maxSteps) {
    uint64_t steps = 0;
    while (steps < maxSteps) {
        if (cpu_.halted()) {
            return {cpu_.haltReason(), steps, cpu_.pc(), 0, 0};
        }
        if (cpu_.pollInterrupt()) {
            ++steps;
            cpu_.jitTick(kCyclesPerInstruction);
            continue;
        }
        Block* block = blockFor(cpu_.pc());
        if (block == nullptr || block->instructionCount > maxSteps - steps) {
            if (std::getenv("PSXREBORN_JIT_DEBUG") != nullptr) {
                std::fprintf(stderr, "[JIT] fallback steps=%llu pc=%08x\n",
                             static_cast<unsigned long long>(steps), cpu_.pc());
            }
            const CpuStepResult result = cpu_.step();
            if (result.reason != CpuStopReason::None) {
                return {result.reason, steps, result.pc, result.instruction, result.address};
            }
            ++steps;
            cpu_.jitTick(kCyclesPerInstruction);
            continue;
        }
        const CodeFn code = block->code;
        const uint32_t blockPc = block->start;
        syncContextFromCpu();
        if (std::getenv("PSXREBORN_JIT_DEBUG") != nullptr) {
            std::fprintf(stderr, "[JIT] pc=%08x block=%08x count=%u steps=%llu\n",
                         cpu_.pc(), block->start, block->instructionCount,
                         static_cast<unsigned long long>(steps));
        }
        code(&context_);
        if (std::getenv("PSXREBORN_JIT_DEBUG") != nullptr) {
            std::fprintf(stderr, "[JIT] done steps=%llu next=%08x flags=%u pc=%08x halted=%d\n",
                         static_cast<unsigned long long>(context_.steps), context_.nextPc,
                         context_.flags, cpu_.pc(), cpu_.halted() ? 1 : 0);
        }
        ++executedBlocks_;
        if (context_.flags != 0) {
            steps += context_.steps;
            cpu_.setPendingLoad(false, 0, 0, 0);
            if (cpu_.halted()) {
                return {cpu_.haltReason(), steps, cpu_.pc(), 0, 0};
            }
            if (context_.steps != 0) {
                cpu_.jitTick(static_cast<uint32_t>(context_.steps * kCyclesPerInstruction));
            }
            if (context_.steps == 0 && cpu_.pc() == blockPc) {
                return {CpuStopReason::UnsupportedInstruction, steps, cpu_.pc(), 0, 0};
            }
            continue;
        }
        syncCpuFromContext();
        steps += context_.steps;
        if (context_.steps != 0) {
            cpu_.jitTick(static_cast<uint32_t>(context_.steps * kCyclesPerInstruction));
        }
    }
    return {CpuStopReason::StepLimit, steps, cpu_.pc(), 0, 0};
}

}  // namespace psxreborn
