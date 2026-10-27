#include "psxreborn/mips.hpp"

namespace psxreborn {

namespace {

Instruction makeInstruction(Opcode opcode, uint32_t raw) {
    Instruction instruction;
    instruction.opcode = opcode;
    instruction.raw = raw;
    instruction.rs = static_cast<uint8_t>((raw >> 21) & 0x1Fu);
    instruction.rt = static_cast<uint8_t>((raw >> 16) & 0x1Fu);
    instruction.rd = static_cast<uint8_t>((raw >> 11) & 0x1Fu);
    instruction.shamt = static_cast<uint8_t>((raw >> 6) & 0x1Fu);
    instruction.imm = static_cast<int16_t>(raw & 0xFFFFu);
    instruction.target = raw & 0x03FFFFFFu;
    return instruction;
}

Instruction decodeSpecial(uint32_t raw) {
    const uint8_t funct = static_cast<uint8_t>(raw & 0x3Fu);
    switch (funct) {
        case 0x00:
            return makeInstruction(raw == 0 ? Opcode::Nop : Opcode::Sll, raw);
        case 0x02:
            return makeInstruction(Opcode::Srl, raw);
        case 0x03:
            return makeInstruction(Opcode::Sra, raw);
        case 0x04:
            return makeInstruction(Opcode::Sllv, raw);
        case 0x06:
            return makeInstruction(Opcode::Srlv, raw);
        case 0x07:
            return makeInstruction(Opcode::Srav, raw);
        case 0x08:
            return makeInstruction(Opcode::Jr, raw);
        case 0x09:
            return makeInstruction(Opcode::Jalr, raw);
        case 0x0C:
            return makeInstruction(Opcode::Syscall, raw);
        case 0x0D:
            return makeInstruction(Opcode::Break, raw);
        case 0x10:
            return makeInstruction(Opcode::Mfhi, raw);
        case 0x11:
            return makeInstruction(Opcode::Mthi, raw);
        case 0x12:
            return makeInstruction(Opcode::Mflo, raw);
        case 0x13:
            return makeInstruction(Opcode::Mtlo, raw);
        case 0x18:
            return makeInstruction(Opcode::Mult, raw);
        case 0x19:
            return makeInstruction(Opcode::Multu, raw);
        case 0x1A:
            return makeInstruction(Opcode::Div, raw);
        case 0x1B:
            return makeInstruction(Opcode::Divu, raw);
        case 0x20:
            return makeInstruction(Opcode::Add, raw);
        case 0x21:
            return makeInstruction(Opcode::Addu, raw);
        case 0x22:
            return makeInstruction(Opcode::Sub, raw);
        case 0x23:
            return makeInstruction(Opcode::Subu, raw);
        case 0x24:
            return makeInstruction(Opcode::And, raw);
        case 0x25:
            return makeInstruction(Opcode::Or, raw);
        case 0x26:
            return makeInstruction(Opcode::Xor, raw);
        case 0x27:
            return makeInstruction(Opcode::Nor, raw);
        case 0x2A:
            return makeInstruction(Opcode::Slt, raw);
        case 0x2B:
            return makeInstruction(Opcode::Sltu, raw);
        default:
            return makeInstruction(Opcode::Invalid, raw);
    }
}

Instruction decodeRegimm(uint32_t raw) {
    const uint8_t rt = static_cast<uint8_t>((raw >> 16) & 0x1Fu);
    switch (rt) {
        case 0x00:
            return makeInstruction(Opcode::Bltz, raw);
        case 0x01:
            return makeInstruction(Opcode::Bgez, raw);
        case 0x10:
            return makeInstruction(Opcode::Bltzal, raw);
        case 0x11:
            return makeInstruction(Opcode::Bgezal, raw);
        default:
            return makeInstruction(Opcode::Invalid, raw);
    }
}

}  // namespace

Instruction decodeInstruction(uint32_t raw) {
    const uint8_t opcode = static_cast<uint8_t>((raw >> 26) & 0x3Fu);
    switch (opcode) {
        case 0x00:
            return decodeSpecial(raw);
        case 0x01:
            return decodeRegimm(raw);
        case 0x02:
            return makeInstruction(Opcode::J, raw);
        case 0x03:
            return makeInstruction(Opcode::Jal, raw);
        case 0x04:
            return makeInstruction(Opcode::Beq, raw);
        case 0x05:
            return makeInstruction(Opcode::Bne, raw);
        case 0x06:
            return makeInstruction(Opcode::Blez, raw);
        case 0x07:
            return makeInstruction(Opcode::Bgtz, raw);
        case 0x08:
            return makeInstruction(Opcode::Addi, raw);
        case 0x09:
            return makeInstruction(Opcode::Addiu, raw);
        case 0x0A:
            return makeInstruction(Opcode::Slti, raw);
        case 0x0B:
            return makeInstruction(Opcode::Sltiu, raw);
        case 0x0C:
            return makeInstruction(Opcode::Andi, raw);
        case 0x0D:
            return makeInstruction(Opcode::Ori, raw);
        case 0x0E:
            return makeInstruction(Opcode::Xori, raw);
        case 0x0F:
            return makeInstruction(Opcode::Lui, raw);
        case 0x10: {
            const uint8_t rs = static_cast<uint8_t>((raw >> 21) & 0x1Fu);
            if (rs == 0x10 && (raw & 0x3Fu) == 0x10u && ((raw >> 6) & 0x1Fu) == 0) {
                return makeInstruction(Opcode::Rfe, raw);
            }
            switch (rs) {
                case 0x00:
                    return makeInstruction(Opcode::Mfc0, raw);
                case 0x02:
                    return makeInstruction(Opcode::Cfc0, raw);
                case 0x04:
                    return makeInstruction(Opcode::Mtc0, raw);
                case 0x06:
                    return makeInstruction(Opcode::Ctc0, raw);
                default:
                    return makeInstruction(Opcode::Invalid, raw);
            }
        }
        case 0x11:
            return makeInstruction(Opcode::Cop1, raw);
        case 0x12: {
            if ((raw & (1u << 25)) != 0) {
                return makeInstruction(Opcode::Cop2, raw);
            }
            switch ((raw >> 21) & 0x1Fu) {
                case 0x00:
                    return makeInstruction(Opcode::Mfc2, raw);
                case 0x02:
                    return makeInstruction(Opcode::Cfc2, raw);
                case 0x04:
                    return makeInstruction(Opcode::Mtc2, raw);
                case 0x06:
                    return makeInstruction(Opcode::Ctc2, raw);
                default:
                    return makeInstruction(Opcode::Invalid, raw);
            }
        }
        case 0x20:
            return makeInstruction(Opcode::Lb, raw);
        case 0x21:
            return makeInstruction(Opcode::Lh, raw);
        case 0x22:
            return makeInstruction(Opcode::Lwl, raw);
        case 0x23:
            return makeInstruction(Opcode::Lw, raw);
        case 0x24:
            return makeInstruction(Opcode::Lbu, raw);
        case 0x25:
            return makeInstruction(Opcode::Lhu, raw);
        case 0x26:
            return makeInstruction(Opcode::Lwr, raw);
        case 0x28:
            return makeInstruction(Opcode::Sb, raw);
        case 0x29:
            return makeInstruction(Opcode::Sh, raw);
        case 0x2A:
            return makeInstruction(Opcode::Swl, raw);
        case 0x2B:
            return makeInstruction(Opcode::Sw, raw);
        case 0x2E:
            return makeInstruction(Opcode::Swr, raw);
        case 0x31:
            return makeInstruction(Opcode::Lwc1, raw);
        case 0x32:
            return makeInstruction(Opcode::Lwc2, raw);
        case 0x39:
            return makeInstruction(Opcode::Swc1, raw);
        case 0x3A:
            return makeInstruction(Opcode::Swc2, raw);
        default:
            return makeInstruction(Opcode::Invalid, raw);
    }
}

bool Instruction::isBranch() const {
    switch (opcode) {
        case Opcode::Beq:
        case Opcode::Bne:
        case Opcode::Bgez:
        case Opcode::Bgezal:
        case Opcode::Bgtz:
        case Opcode::Blez:
        case Opcode::Bltz:
        case Opcode::Bltzal:
            return true;
        default:
            return false;
    }
}

bool Instruction::isJump() const {
    switch (opcode) {
        case Opcode::J:
        case Opcode::Jal:
        case Opcode::Jalr:
        case Opcode::Jr:
            return true;
        default:
            return false;
    }
}

std::string_view mnemonic(Opcode opcode) {
    switch (opcode) {
        case Opcode::Invalid:
            return "invalid";
        case Opcode::Nop:
            return "nop";
        case Opcode::Add:
            return "add";
        case Opcode::Addi:
            return "addi";
        case Opcode::Addiu:
            return "addiu";
        case Opcode::Addu:
            return "addu";
        case Opcode::And:
            return "and";
        case Opcode::Andi:
            return "andi";
        case Opcode::Beq:
            return "beq";
        case Opcode::Bne:
            return "bne";
        case Opcode::Bgez:
            return "bgez";
        case Opcode::Bgezal:
            return "bgezal";
        case Opcode::Bgtz:
            return "bgtz";
        case Opcode::Blez:
            return "blez";
        case Opcode::Bltz:
            return "bltz";
        case Opcode::Bltzal:
            return "bltzal";
        case Opcode::Break:
            return "break";
        case Opcode::Cfc0:
            return "cfc0";
        case Opcode::Cfc2:
            return "cfc2";
        case Opcode::Cop1:
            return "cop1";
        case Opcode::Cop2:
            return "cop2";
        case Opcode::Ctc0:
            return "ctc0";
        case Opcode::Ctc2:
            return "ctc2";
        case Opcode::Div:
            return "div";
        case Opcode::Divu:
            return "divu";
        case Opcode::J:
            return "j";
        case Opcode::Jal:
            return "jal";
        case Opcode::Jalr:
            return "jalr";
        case Opcode::Jr:
            return "jr";
        case Opcode::Lb:
            return "lb";
        case Opcode::Lbu:
            return "lbu";
        case Opcode::Lh:
            return "lh";
        case Opcode::Lhu:
            return "lhu";
        case Opcode::Lui:
            return "lui";
        case Opcode::Lw:
            return "lw";
        case Opcode::Lwl:
            return "lwl";
        case Opcode::Lwr:
            return "lwr";
        case Opcode::Lwc1:
            return "lwc1";
        case Opcode::Lwc2:
            return "lwc2";
        case Opcode::Mfc0:
            return "mfc0";
        case Opcode::Mfc2:
            return "mfc2";
        case Opcode::Mfhi:
            return "mfhi";
        case Opcode::Mflo:
            return "mflo";
        case Opcode::Mthi:
            return "mthi";
        case Opcode::Mtc0:
            return "mtc0";
        case Opcode::Mtc2:
            return "mtc2";
        case Opcode::Mtlo:
            return "mtlo";
        case Opcode::Mult:
            return "mult";
        case Opcode::Multu:
            return "multu";
        case Opcode::Nor:
            return "nor";
        case Opcode::Or:
            return "or";
        case Opcode::Ori:
            return "ori";
        case Opcode::Rfe:
            return "rfe";
        case Opcode::Sb:
            return "sb";
        case Opcode::Sh:
            return "sh";
        case Opcode::Sll:
            return "sll";
        case Opcode::Sllv:
            return "sllv";
        case Opcode::Slt:
            return "slt";
        case Opcode::Slti:
            return "slti";
        case Opcode::Sltiu:
            return "sltiu";
        case Opcode::Sltu:
            return "sltu";
        case Opcode::Sra:
            return "sra";
        case Opcode::Srav:
            return "srav";
        case Opcode::Srl:
            return "srl";
        case Opcode::Srlv:
            return "srlv";
        case Opcode::Sub:
            return "sub";
        case Opcode::Subu:
            return "subu";
        case Opcode::Sw:
            return "sw";
        case Opcode::Swc1:
            return "swc1";
        case Opcode::Swc2:
            return "swc2";
        case Opcode::Swl:
            return "swl";
        case Opcode::Swr:
            return "swr";
        case Opcode::Syscall:
            return "syscall";
        case Opcode::Xor:
            return "xor";
        case Opcode::Xori:
            return "xori";
    }
    return "invalid";
}

}  // namespace psxreborn
