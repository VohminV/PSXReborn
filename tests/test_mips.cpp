#include "psxreborn/mips.hpp"

#include "test_framework.hpp"
#include "test_helpers.hpp"

using psxreborn::Opcode;

TEST_CASE(mips_decodes_special_instructions) {
    const auto nop = psxreborn::decodeInstruction(0);
    CHECK_EQ(static_cast<int>(nop.opcode), static_cast<int>(Opcode::Nop));
    CHECK(std::string_view(psxreborn::mnemonic(nop.opcode)) == "nop");

    const auto sll = psxreborn::decodeInstruction(test::encodeR(0, 2, 3, 4, 0x00));
    CHECK_EQ(static_cast<int>(sll.opcode), static_cast<int>(Opcode::Sll));
    CHECK_EQ(sll.rt, 2);
    CHECK_EQ(sll.rd, 3);
    CHECK_EQ(sll.shamt, 4);

    const auto jr = psxreborn::decodeInstruction(test::encodeR(31, 0, 0, 0, 0x08));
    CHECK_EQ(static_cast<int>(jr.opcode), static_cast<int>(Opcode::Jr));
    CHECK_EQ(jr.rs, 31);
    CHECK(jr.isJump());
    CHECK(!jr.isBranch());

    const auto jalr = psxreborn::decodeInstruction(test::encodeR(8, 0, 31, 0, 0x09));
    CHECK_EQ(static_cast<int>(jalr.opcode), static_cast<int>(Opcode::Jalr));
    CHECK_EQ(jalr.rs, 8);
    CHECK_EQ(jalr.rd, 31);

    const auto add = psxreborn::decodeInstruction(test::encodeR(2, 3, 4, 0, 0x20));
    CHECK_EQ(static_cast<int>(add.opcode), static_cast<int>(Opcode::Add));
    CHECK_EQ(add.rs, 2);
    CHECK_EQ(add.rt, 3);
    CHECK_EQ(add.rd, 4);

    const auto sltiu = psxreborn::decodeInstruction(test::encodeI(0x0B, 4, 5, 0xFFFE));
    CHECK_EQ(static_cast<int>(sltiu.opcode), static_cast<int>(Opcode::Sltiu));
    CHECK_EQ(sltiu.rs, 4);
    CHECK_EQ(sltiu.rt, 5);
    CHECK_EQ(sltiu.imm, -2);
}

TEST_CASE(mips_decodes_branches_and_jumps) {
    const auto beq = psxreborn::decodeInstruction(test::encodeI(0x04, 1, 2, 0xFFFE));
    CHECK_EQ(static_cast<int>(beq.opcode), static_cast<int>(Opcode::Beq));
    CHECK(beq.isBranch());
    CHECK_EQ(beq.branchTarget(0x80010000u), 0x8000FFFCu);

    const auto bgezal = psxreborn::decodeInstruction(test::encodeI(0x01, 5, 0x11, 1));
    CHECK_EQ(static_cast<int>(bgezal.opcode), static_cast<int>(Opcode::Bgezal));
    CHECK_EQ(bgezal.rs, 5);
    CHECK_EQ(bgezal.branchTarget(0x80010000u), 0x80010008u);

    const auto j = psxreborn::decodeInstruction(test::encodeJ(0x02, 0x00040010));
    CHECK_EQ(static_cast<int>(j.opcode), static_cast<int>(Opcode::J));
    CHECK(j.isJump());
    CHECK_EQ(j.jumpTarget(0x8ABC0000u), 0x80100040u);

    const auto jal = psxreborn::decodeInstruction(test::encodeJ(0x03, 0x00040010));
    CHECK_EQ(static_cast<int>(jal.opcode), static_cast<int>(Opcode::Jal));
}

TEST_CASE(mips_decodes_memory_and_immediate_instructions) {
    const auto lui = psxreborn::decodeInstruction(test::encodeI(0x0F, 0, 2, 0x1234));
    CHECK_EQ(static_cast<int>(lui.opcode), static_cast<int>(Opcode::Lui));
    CHECK_EQ(lui.rt, 2);
    CHECK_EQ(lui.imm, 0x1234);

    const auto lw = psxreborn::decodeInstruction(test::encodeI(0x23, 29, 4, 0x0010));
    CHECK_EQ(static_cast<int>(lw.opcode), static_cast<int>(Opcode::Lw));
    CHECK_EQ(lw.rs, 29);
    CHECK_EQ(lw.rt, 4);
    CHECK_EQ(lw.imm, 0x10);

    const auto swl = psxreborn::decodeInstruction(test::encodeI(0x2A, 3, 4, 0xFFFC));
    CHECK_EQ(static_cast<int>(swl.opcode), static_cast<int>(Opcode::Swl));
    CHECK_EQ(swl.imm, -4);

    const auto lwr = psxreborn::decodeInstruction(test::encodeI(0x26, 3, 4, 0));
    CHECK_EQ(static_cast<int>(lwr.opcode), static_cast<int>(Opcode::Lwr));
}

TEST_CASE(mips_decodes_coprocessor_instructions) {
    const auto mfc0 = psxreborn::decodeInstruction(0x40000000u | test::encodeR(0, 2, 12, 0, 0));
    CHECK_EQ(static_cast<int>(mfc0.opcode), static_cast<int>(Opcode::Mfc0));
    CHECK_EQ(mfc0.rt, 2);
    CHECK_EQ(mfc0.rd, 12);

    const auto mtc0 = psxreborn::decodeInstruction(0x40000000u | test::encodeR(4, 2, 12, 0, 0));
    CHECK_EQ(static_cast<int>(mtc0.opcode), static_cast<int>(Opcode::Mtc0));

    const auto rfe = psxreborn::decodeInstruction(0x42000010u);
    CHECK_EQ(static_cast<int>(rfe.opcode), static_cast<int>(Opcode::Rfe));

    const auto mfc2 = psxreborn::decodeInstruction(0x48000000u | test::encodeR(0, 2, 25, 0, 0));
    CHECK_EQ(static_cast<int>(mfc2.opcode), static_cast<int>(Opcode::Mfc2));

    const auto cop2 = psxreborn::decodeInstruction(0x4A000000u | 0x0480012u);
    CHECK_EQ(static_cast<int>(cop2.opcode), static_cast<int>(Opcode::Cop2));

    const auto lwc2 = psxreborn::decodeInstruction(test::encodeI(0x32, 2, 3, 0x10));
    CHECK_EQ(static_cast<int>(lwc2.opcode), static_cast<int>(Opcode::Lwc2));
}

TEST_CASE(mips_rejects_unsupported_and_reserved_encodings) {
    const auto cop3 = psxreborn::decodeInstruction(test::encodeI(0x13, 0, 2, 0));
    CHECK(!cop3.valid());

    const auto reservedSpecial = psxreborn::decodeInstruction(test::encodeR(0, 0, 0, 0, 0x3F));
    CHECK(!reservedSpecial.valid());

    const auto invalidRegimm = psxreborn::decodeInstruction(test::encodeI(0x01, 0, 2, 0));
    CHECK(!invalidRegimm.valid());

    const auto invalidLoad = psxreborn::decodeInstruction(test::encodeI(0x27, 0, 2, 0));
    CHECK(!invalidLoad.valid());
}
