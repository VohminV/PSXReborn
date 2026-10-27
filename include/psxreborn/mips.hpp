#pragma once

#include <cstdint>
#include <string_view>

namespace psxreborn {

enum class Opcode : uint8_t {
    Invalid,
    Nop,
    Add,
    Addi,
    Addiu,
    Addu,
    And,
    Andi,
    Beq,
    Bne,
    Bgez,
    Bgezal,
    Bgtz,
    Blez,
    Bltz,
    Bltzal,
    Break,
    Cfc0,
    Cfc2,
    Cop1,
    Cop2,
    Ctc0,
    Ctc2,
    Div,
    Divu,
    J,
    Jal,
    Jalr,
    Jr,
    Lb,
    Lbu,
    Lh,
    Lhu,
    Lui,
    Lw,
    Lwc1,
    Lwc2,
    Lwl,
    Lwr,
    Mfc0,
    Mfc2,
    Mfhi,
    Mflo,
    Mthi,
    Mtc0,
    Mtc2,
    Mtlo,
    Mult,
    Multu,
    Nor,
    Or,
    Ori,
    Rfe,
    Sb,
    Sh,
    Sll,
    Sllv,
    Slt,
    Slti,
    Sltiu,
    Sltu,
    Sra,
    Srav,
    Srl,
    Srlv,
    Sub,
    Subu,
    Sw,
    Swc1,
    Swc2,
    Swl,
    Swr,
    Syscall,
    Xor,
    Xori,
};

struct Instruction {
    Opcode opcode = Opcode::Invalid;
    uint32_t raw = 0;
    uint8_t rs = 0;
    uint8_t rt = 0;
    uint8_t rd = 0;
    uint8_t shamt = 0;
    int16_t imm = 0;
    uint32_t target = 0;

    bool valid() const { return opcode != Opcode::Invalid; }
    bool isBranch() const;
    bool isJump() const;
    bool isControlTransfer() const { return isBranch() || isJump(); }

    uint32_t branchTarget(uint32_t pc) const {
        return pc + 4u + (static_cast<uint32_t>(static_cast<int32_t>(imm)) << 2);
    }

    uint32_t jumpTarget(uint32_t pc) const {
        return (pc & 0xF0000000u) | (target << 2);
    }
};

Instruction decodeInstruction(uint32_t raw);
std::string_view mnemonic(Opcode opcode);

}  // namespace psxreborn
