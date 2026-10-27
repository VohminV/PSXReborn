#include "psxreborn/gte.hpp"

#include "test_framework.hpp"

using psxreborn::Gte;

namespace {

constexpr uint32_t kCop2 = 0x4A000000u;

uint32_t command(uint32_t value) {
    return kCop2 | value;
}

void setIdentity(Gte& gte) {
    gte.writeControl(0, 0x1000u);
    gte.writeControl(1, 0);
    gte.writeControl(2, 0x1000u);
    gte.writeControl(3, 0);
    gte.writeControl(4, 0x1000u);
}

}  // namespace

TEST_CASE(gte_mvmva_translation_with_identity_matrix) {
    Gte gte;
    setIdentity(gte);
    gte.writeControl(5, 0x10);
    gte.writeControl(6, 0x20);
    gte.writeControl(7, 0x30);
    gte.writeData(0, (2u << 16) | 1u);
    gte.writeData(1, 3);
    gte.execute(command(0x0480012u));
    CHECK_EQ(gte.readData(25), 0x11u);
    CHECK_EQ(gte.readData(26), 0x22u);
    CHECK_EQ(gte.readData(27), 0x33u);
    CHECK_EQ(gte.readData(9), 0x11u);
    CHECK_EQ(gte.readData(10), 0x22u);
    CHECK_EQ(gte.readData(11), 0x33u);
    CHECK_EQ(gte.flags() & 0x7F87E000u, 0u);
}

TEST_CASE(gte_nclip_and_avsz) {
    Gte gte;
    gte.writeData(12, (0u << 16) | 0u);
    gte.writeData(13, (0u << 16) | 10u);
    gte.writeData(14, (10u << 16) | 0u);
    gte.execute(command(0x1400006u));
    CHECK_EQ(gte.readData(24), 100u);

    gte.writeData(17, 100);
    gte.writeData(18, 200);
    gte.writeData(19, 300);
    gte.writeControl(29, 0x1000u);
    gte.execute(command(0x158002Du));
    CHECK_EQ(gte.readData(24), 0x258000u);
    CHECK_EQ(gte.readData(7), 0x258u);
}

TEST_CASE(gte_saturation_flags_and_register_conversion) {
    Gte gte;
    gte.writeData(9, 0x7FFF);
    gte.writeData(10, 0x8000);
    gte.writeData(11, 1);
    gte.execute(command(0x0A00428u | 0x80000u));
    CHECK_EQ(gte.readData(9), 0x7FFFu);
    CHECK_EQ(gte.readData(10), 0x7FFFu);
    CHECK((gte.flags() & (1u << 24)) != 0);
    CHECK((gte.flags() & 0x80000000u) != 0);

    gte.writeData(28, (31u << 10) | (0u << 5) | 31u);
    CHECK_EQ(gte.readData(9), 0x0F80u);
    CHECK_EQ(gte.readData(10), 0u);
    CHECK_EQ(gte.readData(11), 0x0F80u);
    CHECK_EQ(gte.readData(29), (31u << 10) | 31u);

    gte.writeData(30, 0xFFFFFFFFu);
    CHECK_EQ(gte.readData(31), 32u);
}

TEST_CASE(gte_rtps_sets_size_and_screen_fifo) {
    Gte gte;
    setIdentity(gte);
    gte.writeControl(26, 0x100);
    gte.writeControl(24, 0x10u << 16);
    gte.writeControl(25, 0x20u << 16);
    gte.writeControl(28, 0);
    gte.writeData(0, 0);
    gte.writeData(1, 1);
    gte.execute(command(0x0180001u | 0x80000u));
    CHECK_EQ(gte.readData(19), 1u);
    CHECK_EQ(gte.readData(14) & 0xFFFFu, 0x10u);
    CHECK_EQ((gte.readData(14) >> 16) & 0xFFFFu, 0x20u);
    CHECK((gte.flags() & 0x7F87E000u) != 0);
}
