#include <array>
#include <cstdint>

#include "psxreborn/controller_profile.hpp"
#include "psxreborn/cpu.hpp"
#include "psxreborn/hardware.hpp"
#include "psxreborn/memory.hpp"
#include "psxreborn/pad.hpp"

#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace psxreborn;

namespace {

constexpr uint16_t kTxEnable = 1u << 0;
constexpr uint16_t kSelect = 1u << 1;
constexpr uint16_t kAcknowledge = 1u << 4;
constexpr uint16_t kRxIrqEnable = 1u << 11;
constexpr uint16_t kAckIrqEnable = 1u << 12;

void setControl(System& system, uint16_t value) {
    MemoryFault fault = MemoryFault::None;
    CHECK(system.write16(0x1F80104Au, value, &fault));
    CHECK(fault == MemoryFault::None);
}

uint16_t status(System& system) {
    MemoryFault fault = MemoryFault::None;
    uint16_t value = 0;
    CHECK(system.read16(0x1F801044u, value, &fault));
    CHECK(fault == MemoryFault::None);
    return value;
}

void writeData(System& system, uint8_t value) {
    MemoryFault fault = MemoryFault::None;
    CHECK(system.write8(0x1F801040u, value, &fault));
    CHECK(fault == MemoryFault::None);
}

uint8_t readData(System& system) {
    MemoryFault fault = MemoryFault::None;
    uint8_t response = 0xFF;
    CHECK(system.read8(0x1F801040u, response, &fault));
    CHECK(fault == MemoryFault::None);
    return response;
}

uint8_t transfer(System& system, uint8_t value) {
    writeData(system, value);
    return readData(system);
}

}  // namespace

TEST_CASE(sio0_controller_protocol) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadCross));

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xBF));
}

TEST_CASE(sio0_cpu_polls_digital_pad_through_mapped_io) {
    Memory memory;
    System system(memory);
    Cpu cpu(memory);
    constexpr uint32_t kProgramBase = 0x80010000u;
    test::writeProgram(memory, kProgramBase, {
        test::encodeI(0x0F, 0, 8, 0x1F80),
        test::encodeI(0x0D, 8, 8, 0x104A),
        test::encodeI(0x09, 0, 9, 0x0003),
        test::encodeI(0x29, 8, 9, 0x0000),
        test::encodeI(0x09, 8, 8, 0xFFF6),
        test::encodeI(0x09, 0, 9, 0x0001),
        test::encodeI(0x28, 8, 9, 0x0000),
        test::encodeI(0x24, 8, 10, 0x0000),
        test::encodeI(0x09, 0, 9, 0x0042),
        test::encodeI(0x28, 8, 9, 0x0000),
        test::encodeI(0x24, 8, 11, 0x0000),
        test::encodeI(0x28, 8, 0, 0x0000),
        test::encodeI(0x24, 8, 12, 0x0000),
        test::encodeI(0x28, 8, 0, 0x0000),
        test::encodeI(0x24, 8, 13, 0x0000),
        test::encodeI(0x28, 8, 0, 0x0000),
        test::encodeI(0x24, 8, 14, 0x0000),
        0x0000000Du,
    });

    cpu.reset();
    system.reset();
    cpu.setPc(kProgramBase);
    system.bind(cpu);
    system.sio0().setPadAnalogMode(false);
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadCross));

    const CpuRunResult result = cpu.run(64);
    CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::Breakpoint));
    CHECK_EQ(cpu.reg(10), 0xFFu);
    CHECK_EQ(cpu.reg(11), 0x41u);
    CHECK_EQ(cpu.reg(12), 0x5Au);
    CHECK_EQ(cpu.reg(13), 0xFFu);
    CHECK_EQ(cpu.reg(14), 0xBFu);
}

TEST_CASE(sio0_digital_pad_protocol_vectors) {
    struct Vector {
        uint16_t buttons;
        uint8_t low;
        uint8_t high;
    };
    const Vector vectors[] = {
        {0xFFFFu, 0xFFu, 0xFFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadSelect), 0xFEu, 0xFFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadStart), 0xF7u, 0xFFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadUp), 0xEFu, 0xFFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadRight), 0xDFu, 0xFFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadDown), 0xBFu, 0xFFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadLeft), 0x7Fu, 0xFFu},
        {static_cast<uint16_t>(0xFFFFu & ~(kPadUp | kPadStart)), 0xE7u, 0xFFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadL2), 0xFFu, 0xFEu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadR2), 0xFFu, 0xFDu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadL1), 0xFFu, 0xFBu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadR1), 0xFFu, 0xF7u},
        {static_cast<uint16_t>(0xFFFFu & ~kPadTriangle), 0xFFu, 0xEFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadCircle), 0xFFu, 0xDFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadCross), 0xFFu, 0xBFu},
        {static_cast<uint16_t>(0xFFFFu & ~kPadSquare), 0xFFu, 0x7Fu},
        {static_cast<uint16_t>(0xFFFFu & ~(kPadCross | kPadL1)), 0xFFu, 0xBBu},
    };

    for (const Vector& vector : vectors) {
        Memory memory;
        System system(memory);
        system.reset();
        setControl(system, kTxEnable | kSelect);
        system.sio0().setPadAnalogMode(false);
        system.setPadButtons(vector.buttons);

        CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
        CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
        CHECK_EQ(transfer(system, 0x00), vector.low);
        CHECK_EQ(transfer(system, 0x00), vector.high);
    }
}

TEST_CASE(sio0_xinput_mask_reaches_guest_pad_protocol) {
    struct Vector {
        uint16_t xinput;
        uint8_t low;
        uint8_t high;
    };
    const Vector vectors[] = {
        {0, 0xFFu, 0xFFu},
        {kXInputStart, 0xF7u, 0xFFu},
        {kXInputDpadUp, 0xEFu, 0xFFu},
        {static_cast<uint16_t>(kXInputA | kXInputLeftShoulder), 0xFFu, 0xBBu},
        {static_cast<uint16_t>(kXInputDpadUp | kXInputStart), 0xE7u, 0xFFu},
    };

    for (const Vector& vector : vectors) {
        Memory memory;
        System system(memory);
        system.reset();
        setControl(system, kTxEnable | kSelect);
        system.sio0().setPadAnalogMode(false);

        PadState pad;
        pad.setXInputButtons(vector.xinput);
        system.setPadButtons(pad.buttons());

        CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
        CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
        CHECK_EQ(transfer(system, 0x00), vector.low);
        CHECK_EQ(transfer(system, 0x00), vector.high);
    }
}

TEST_CASE(sio0_automap_reproduces_manual_mapping_mask_bytes) {
    // The auto-mapped pipeline (GLFW logical button -> XInput mask ->
    // xinputToPadButtons -> active-low word) must produce the exact same
    // dual button-mask bytes the manual event mapping produced. Drives both
    // entry points through a real SIO0/DualShock 0x42 transaction and asserts
    // identical, active-low bytes for every user-visible button.
    struct Vector {
        uint16_t xinput;
        uint16_t pad;
        uint8_t low;
        uint8_t high;
    };
    const Vector vectors[] = {
        {kXInputStart, kPadStart, 0xF7u, 0xFFu},
        {kXInputBack, kPadSelect, 0xFEu, 0xFFu},
        {kXInputA, kPadCross, 0xFFu, 0xBFu},
        {kXInputB, kPadCircle, 0xFFu, 0xDFu},
        {kXInputX, kPadSquare, 0xFFu, 0x7Fu},
        {kXInputY, kPadTriangle, 0xFFu, 0xEFu},
        {kXInputDpadUp, kPadUp, 0xEFu, 0xFFu},
        {kXInputDpadDown, kPadDown, 0xBFu, 0xFFu},
        {kXInputDpadLeft, kPadLeft, 0x7Fu, 0xFFu},
        {kXInputDpadRight, kPadRight, 0xDFu, 0xFFu},
        {kXInputLeftShoulder, kPadL1, 0xFFu, 0xFBu},
        {kXInputRightShoulder, kPadR1, 0xFFu, 0xF7u},
        {kXInputLeftThumb, kPadL3, 0xFDu, 0xFFu},
        {kXInputRightThumb, kPadR3, 0xFBu, 0xFFu},
    };

    for (const Vector& vector : vectors) {
        Memory memory;
        System system(memory);
        system.reset();
        setControl(system, kTxEnable | kSelect);
        system.sio0().setPadAnalogMode(false);

        // Manual mapping entry point (per-button incremental set).
        PadState manual;
        manual.setControllerButton(vector.pad, true);
        system.setPadButtons(manual.buttons());
        CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
        CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
        CHECK_EQ(transfer(system, 0x00), vector.low);
        CHECK_EQ(transfer(system, 0x00), vector.high);

        // Auto mapping entry point (polled full-replace set): a new
        // transaction must sample the same button state and therefore return
        // the same bytes.
        setControl(system, kTxEnable);
        setControl(system, kTxEnable | kSelect);
        PadState automap;
        automap.setXInputButtons(vector.xinput);
        system.setPadButtons(automap.buttons());
        CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
        CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
        CHECK_EQ(transfer(system, 0x00), vector.low);
        CHECK_EQ(transfer(system, 0x00), vector.high);
    }
}

TEST_CASE(sio0_default_profile_maps_all_glfw_buttons_to_guest_bytes) {
    // Full Home-era chain: GLFW gamepad button id -> ControllerMapping ->
    // PadState -> SIO0 0x42 response. Every PS1 slot must reach the active-low
    // mask bytes, so a shifted GLFW id cannot silently turn D-pad DOWN into L3.
    const ControllerMapping mapping = ControllerMapping::defaults();
    for (std::size_t slot = 0; slot < kPs1ButtonCount; ++slot) {
        const HostControl control = mapping.host[slot];
        ControllerSnapshot snapshot;
        if (control == kHostAxisTriggerLeft) {
            snapshot.axes[4] = mapping.triggerThreshold;
        } else if (control == kHostAxisTriggerRight) {
            snapshot.axes[5] = mapping.triggerThreshold;
        } else {
            snapshot.setButtonPressed(control, true);
        }

        PadState pad;
        applyControllerSnapshot(pad, snapshot, mapping);
        CHECK_EQ(pad.pressed(), ps1ButtonBit(slot));

        Memory memory;
        System system(memory);
        system.reset();
        setControl(system, kTxEnable | kSelect);
        system.sio0().setPadAnalogMode(false);
        system.setPadButtons(pad.buttons());

        const uint16_t word = pad.buttons();
        CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
        CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(word & 0xFFu));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>((word >> 8u) & 0xFFu));
    }
}

TEST_CASE(sio0_automap_input_state_updates_across_transactions) {
    // The polled auto-mapped state must be re-sampled at each pad select:
    // press then release of the same button must be guest-visible in the two
    // mask bytes of consecutive transactions.
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);

    PadState pad;

    auto drive = [&](uint16_t xinputMask, uint8_t low, uint8_t high) {
        setControl(system, kTxEnable);
        setControl(system, kTxEnable | kSelect);
        pad.setXInputButtons(xinputMask);
        system.setPadButtons(pad.buttons());
        CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
        CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
        CHECK_EQ(transfer(system, 0x00), low);
        CHECK_EQ(transfer(system, 0x00), high);
    };

    drive(kXInputStart, 0xF7u, 0xFFu);
    drive(0, 0xFFu, 0xFFu);
    drive(kXInputA, 0xFFu, 0xBFu);
    drive(0, 0xFFu, 0xFFu);
    drive(static_cast<uint16_t>(kXInputBack | kXInputDpadDown), 0xBEu, 0xFFu);
}

TEST_CASE(sio0_restarts_poll_on_select_toggle) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadStart));

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xF7));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));

    // A new transaction requires deselecting via JOY_CTRL SELECT, not a
    // repeated 0x01 data byte (0x01 can occur inside command payloads).
    setControl(system, kTxEnable);
    setControl(system, kTxEnable | kSelect);

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xF7));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));
}

TEST_CASE(sio0_status_tracks_rx_and_ack) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);

    CHECK_EQ(status(system) & 0x0082u, 0u);
    MemoryFault fault = MemoryFault::None;
    CHECK(system.write8(0x1F801040u, 0x01, &fault));
    CHECK(fault == MemoryFault::None);
    CHECK((status(system) & 0x0002u) != 0);
    // /ACK asserts only after the whole byte transfer (default baud * 8
    // cycles), never with the write.
    CHECK_EQ(status(system) & 0x0080u, 0u);
    system.sio0().tick(1100u);
    CHECK((status(system) & 0x0080u) != 0);
    uint8_t response = 0;
    CHECK(system.read8(0x1F801040u, response, &fault));
    CHECK_EQ(response, static_cast<uint8_t>(0xFF));
    CHECK_EQ(status(system) & 0x0002u, 0u);
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
    system.sio0().tick(1100u);
    CHECK((status(system) & 0x0080u) != 0);
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));
    system.sio0().tick(1600u);
    CHECK_EQ(status(system) & 0x0080u, 0u);
}

TEST_CASE(sio0_rx_fifo_queues_unread_responses) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);

    writeData(system, 0x01);
    writeData(system, 0x42);
    CHECK((status(system) & 0x0002u) != 0);
    CHECK_EQ(readData(system), static_cast<uint8_t>(0xFF));
    CHECK((status(system) & 0x0002u) != 0);
    CHECK_EQ(readData(system), static_cast<uint8_t>(0x41));
    CHECK_EQ(status(system) & 0x0002u, 0u);
}

TEST_CASE(sio0_wide_data_reads_consume_fifo_entries) {
    // DR reads: the low byte removes 1 RX FIFO entry; the upper bytes are
    // non-destructive previews of the next entries
    // (docs/serialinterfacessio.md).
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadCross));

    writeData(system, 0x01);
    writeData(system, 0x42);
    writeData(system, 0x00);

    MemoryFault fault = MemoryFault::None;
    uint16_t halfword = 0;
    CHECK(system.read16(0x1F801040u, halfword, &fault));
    CHECK(fault == MemoryFault::None);
    CHECK_EQ(halfword, static_cast<uint16_t>(0x41FFu));
    // Only the low byte was consumed.
    CHECK_EQ(readData(system), static_cast<uint8_t>(0x41u));
    CHECK_EQ(readData(system), static_cast<uint8_t>(0x5Au));

    writeData(system, 0x00);
    writeData(system, 0x00);
    writeData(system, 0x00);
    writeData(system, 0x00);

    uint32_t word = 0;
    CHECK(system.read32(0x1F801040u, word, &fault));
    CHECK(fault == MemoryFault::None);
    CHECK_EQ(word, 0xFFFFBFFFu);
    // The previewed entries stay in the FIFO.
    CHECK_EQ(readData(system), static_cast<uint8_t>(0xBFu));
    CHECK_EQ(readData(system), static_cast<uint8_t>(0xFFu));
    CHECK_EQ(readData(system), static_cast<uint8_t>(0xFFu));
}

TEST_CASE(sio0_rx_irq_honours_fifo_threshold) {
    constexpr uint16_t kTwoByteThreshold = 1u << 8;
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect | kRxIrqEnable | kTwoByteThreshold);
    system.sio0().setPadAnalogMode(false);

    writeData(system, 0x01);
    CHECK(!system.sio0().irqRequested());
    CHECK_EQ(status(system) & 0x0200u, 0u);

    writeData(system, 0x42);
    CHECK(system.sio0().irqRequested());
    CHECK((status(system) & 0x0200u) != 0);

    CHECK_EQ(readData(system), static_cast<uint8_t>(0xFF));
    setControl(system, kTxEnable | kSelect | kRxIrqEnable | kTwoByteThreshold |
                            kAcknowledge);
    CHECK(!system.sio0().irqRequested());
    CHECK_EQ(status(system) & 0x0200u, 0u);

    CHECK_EQ(readData(system), static_cast<uint8_t>(0x41));
    CHECK_EQ(status(system) & 0x0002u, 0u);
}

TEST_CASE(sio0_empty_rx_returns_idle_byte) {
    // Reading DR with an empty RX FIFO returns the idle bus level (0xFF),
    // never a stale previously received byte (pcsx-redux sio.cc read8;
    // docs/serialinterfacessio.md DR notes).
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadCross));

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xBF));
    // The transfer helper consumes every response; the FIFO is empty now and
    // further reads return the idle bus level.
    CHECK_EQ(readData(system), static_cast<uint8_t>(0xFF));
}

TEST_CASE(sio0_deselect_ends_poll_session) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));

    setControl(system, kTxEnable);
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0xFF));

    setControl(system, kTxEnable | kSelect);
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
}

TEST_CASE(sio0_dualshock_config_mode_enables_analog_read) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(true);
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadStart));
    system.setPadSticks(10, 20, 30, 40);

    auto startNewPoll = [&]() {
        setControl(system, kTxEnable);
        setControl(system, kTxEnable | kSelect);
    };

    // Enter config mode: 0x43 then data byte 1. DualShock starts digital, so
    // enable analog first; then 0x43 outside config replies like an analog
    // read (0x73).
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x43), static_cast<uint8_t>(0x73));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xF7));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(30));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(40));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(10));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(20));
    CHECK(system.sio0().padConfigMode());

    // Set analog mode inside config: 0x44 then data byte 1.
    startNewPoll();
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x44), static_cast<uint8_t>(0xF3));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK(system.sio0().padAnalogMode());

    // Exit config mode: 0x43 then data byte 0.
    startNewPoll();
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x43), static_cast<uint8_t>(0xF3));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK(!system.sio0().padConfigMode());
    CHECK(system.sio0().padAnalogMode());

    // Analog read returns 0x73 with stick data.
    startNewPoll();
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x73));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xF7));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(30));   // right X
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(40));   // right Y
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(10));   // left X
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(20));   // left Y
}

TEST_CASE(sio0_dualshock_config_read_returns_f3) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(true);
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadCross));

    // Enter config mode. Analog on, so 0x43 replies like analog read.
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x43), static_cast<uint8_t>(0x73));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xBF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(128));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(128));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(128));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(128));
    CHECK(system.sio0().padConfigMode());

    // Read inside config mode returns 0xF3 ID with button data.
    setControl(system, kTxEnable);
    setControl(system, kTxEnable | kSelect);
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0xF3));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xBF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(128));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(128));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(128));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(128));
}

TEST_CASE(sio0_ack_irq_and_control_acknowledge) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect | kAckIrqEnable);

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK(!system.sio0().irqRequested());
    system.sio0().tick(1087);
    CHECK(!system.sio0().irqRequested());
    system.sio0().tick(1);
    CHECK(system.sio0().irqRequested());
    // The /ACK interrupt must fire while /ACK is still asserted: drivers poll
    // JOY_STAT.7 (ACK input level) after the interrupt and expect the level
    // low, then wait for the pulse to release.
    CHECK((system.sio0().status() & 0x0080u) != 0);
    CHECK((system.sio0().status() & 0x0200u) != 0);
    CHECK((system.interrupts().status() & Interrupts::kPad) != 0);

    // CR.4 cannot clear SR.9 while /ACK is still low (circa 100 cycles).
    setControl(system, kTxEnable | kSelect | kAckIrqEnable | kAcknowledge);
    CHECK(system.sio0().irqRequested());
    CHECK((system.sio0().status() & 0x0200u) != 0);

    // Later ticks release /ACK, and only then the acknowledge clears SR.9.
    system.sio0().tick(600);
    CHECK((system.sio0().status() & 0x0080u) == 0);
    CHECK(system.sio0().irqRequested());
    setControl(system, kTxEnable | kSelect | kAckIrqEnable | kAcknowledge);
    CHECK_EQ(system.sio0().status() & 0x0200u, 0u);
    CHECK(!system.sio0().irqRequested());
}

TEST_CASE(sio0_ack_irq_requires_enable) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    system.sio0().tick(2000);
    CHECK(!system.sio0().irqRequested());
    CHECK_EQ(system.sio0().status() & 0x0200u, 0u);
    CHECK_EQ(system.interrupts().status() & Interrupts::kPad, 0u);
    CHECK(!system.sio0().ackAsserted());
}

TEST_CASE(sio0_rx_irq_tracks_fifo_threshold) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect | kRxIrqEnable);

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK((system.sio0().status() & 0x0200u) != 0);
    CHECK((system.interrupts().status() & Interrupts::kPad) != 0);

    setControl(system, kAcknowledge);
    CHECK_EQ(system.sio0().status() & 0x0200u, 0u);
}

TEST_CASE(sio0_requires_tx_enable_and_select) {
    Memory memory;
    System system(memory);
    system.reset();
    system.setPadButtons(0xFFFFu);
    system.sio0().setPadAnalogMode(false);

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0xFF));

    setControl(system, kTxEnable);
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0xFF));

    setControl(system, kTxEnable | kSelect);
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
}

TEST_CASE(sio0_control_reset_clears_state) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect | kAckIrqEnable);
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));

    setControl(system, kAcknowledge | (1u << 6));
    CHECK_EQ(system.sio0().status() & 0x0302u, 0u);
    CHECK_EQ(system.sio0().selectedSlot(), 0u);
    CHECK(!system.sio0().ackAsserted());
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0xFF));
}

TEST_CASE(sio0_control_reset_preserves_pad_config_mode) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadCross));

    // Enter controller config mode: 01 43 00 01 00.
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x43), static_cast<uint8_t>(0x41));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xBF));
    CHECK(system.sio0().padConfigMode());

    // CR.6 resets only the SIO unit. The external controller stays in config
    // mode, so the following 0x45 (Get LED State) must still answer.
    setControl(system, 1u << 6);
    setControl(system, kTxEnable | kSelect);

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x45), static_cast<uint8_t>(0xF3));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x01));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x02));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x02));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x01));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK(system.sio0().padConfigMode());
}

TEST_CASE(sio0_full_pad_poll_preserves_ack_level_and_buttons) {
    Memory memory;
    System system(memory);
    system.reset();
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadCross));
    setControl(system, kTxEnable | kSelect | kAckIrqEnable);

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    // /ACK asserts only after the byte transfer completes (baud * 8 cycles).
    CHECK(!system.sio0().ackAsserted());
    system.sio0().tick(1100);
    CHECK(system.sio0().ackAsserted());
    system.sio0().tick(500);
    CHECK(!system.sio0().ackAsserted());
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
    system.sio0().tick(1300);
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    system.sio0().tick(1300);
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(~kPadCross & 0xFFu));
    system.sio0().tick(1300);
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>((~kPadCross >> 8) & 0xFFu));

    // The final data byte carries no /ACK: after acknowledging the pending
    // interrupt from the last ACKed byte, no further interrupt arrives.
    setControl(system, kAcknowledge);
    system.sio0().tick(2000);
    CHECK(!system.sio0().ackAsserted());
    CHECK(!system.sio0().irqRequested());
}

TEST_CASE(sio0_memory_card_read_and_write_protocol) {
    Memory memory;
    System system(memory);
    system.reset();
    MemoryCard card = MemoryCard::formatted();
    system.setMemoryCard(0, &card);
    setControl(system, kTxEnable | kSelect);

    CHECK_EQ(transfer(system, 0x81), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x52), static_cast<uint8_t>(0x08));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5D));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5C));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5D));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));

    std::array<uint8_t, 128> sector{};
    uint8_t checksum = 0;
    for (size_t index = 0; index < sector.size(); ++index) {
        const uint8_t value = transfer(system, 0x00);
        sector[index] = value;
        checksum ^= value;
    }
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(checksum));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x47));
    CHECK_EQ(sector[0], static_cast<uint8_t>('M'));
    CHECK_EQ(sector[1], static_cast<uint8_t>('C'));
    setControl(system, static_cast<uint16_t>(1u << 6));
    setControl(system, kTxEnable | kSelect);

    std::array<uint8_t, 128> payload{};
    for (size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<uint8_t>(index * 3u + 1u);
    }
    checksum = static_cast<uint8_t>(0x00u ^ 0x40u);
    for (uint8_t value : payload) {
        checksum ^= value;
    }

    CHECK_EQ(transfer(system, 0x81), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x57), static_cast<uint8_t>(0x08));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5D));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x00));
    CHECK_EQ(transfer(system, 0x40), static_cast<uint8_t>(0x40));
    for (uint8_t value : payload) {
        CHECK_EQ(transfer(system, value), value);
    }
    CHECK_EQ(transfer(system, checksum), static_cast<uint8_t>(checksum));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5C));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5D));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x47));
    setControl(system, static_cast<uint16_t>(1u << 6));

    std::array<uint8_t, 128> stored{};
    CHECK(card.readBytes(64u * 128u, stored.data(), stored.size()));
    CHECK(stored == payload);
    CHECK(card.isDirty());
}

namespace {

// PadSource whose sample() can be changed between calls; used to prove SIO0
// latches host input at pad select and does not re-sample mid-transaction.
class MutablePadSource final : public PadSource {
public:
    PadSnapshot next{};
    int samples = 0;

    PadSnapshot sample() override {
        ++samples;
        return next;
    }
};

}  // namespace

TEST_CASE(sio0_pad_snapshot_is_stable_for_whole_transaction) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);

    MutablePadSource source;
    source.next.buttons = static_cast<uint16_t>(0xFFFFu & ~kPadCross);
    system.sio0().setPadSource(&source);

    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(source.samples, 1);

    // Host changes the controller mid-poll: the in-flight transaction must
    // keep returning the bytes sampled at select time.
    source.next.buttons = static_cast<uint16_t>(0xFFFFu & ~kPadStart);
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xBF));
    CHECK_EQ(source.samples, 1);

    // The next transaction samples the new state.
    setControl(system, kTxEnable);
    setControl(system, kTxEnable | kSelect);
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(source.samples, 2);
    CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xF7));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xFF));

    system.sio0().setPadSource(nullptr);
}

TEST_CASE(sio0_pad_source_samples_at_select_not_per_byte) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);

    MutablePadSource source;
    source.next.buttons = 0xFFFFu;
    system.sio0().setPadSource(&source);

    writeData(system, 0x01);
    writeData(system, 0x42);
    writeData(system, 0x00);
    writeData(system, 0x00);
    writeData(system, 0x00);
    CHECK_EQ(source.samples, 1);

    system.sio0().setPadSource(nullptr);
}

TEST_CASE(sio0_dualshock_digital_mode_config_command_replies_0x41) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);
    system.setPadButtons(static_cast<uint16_t>(0xFFFFu & ~kPadCross));

    // Outside config with analog off, 0x43 behaves like a digital read
    // (ID 0x41, 4-byte payload) and still latches config mode on the data
    // byte - matching PCSX-Redux PadType::Analog.
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x43), static_cast<uint8_t>(0x41));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
    CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
    CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0xBF));
    CHECK(system.sio0().padConfigMode());
}

TEST_CASE(sio0_polling_is_independent_of_host_frame_cadence) {
    // SIO0 samples the PadSource only on pad select. Two back-to-back
    // transactions with no host-frame processing between them must both see
    // the latest source state - polling frequency is guest-driven, not
    // host-frame-driven.
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);

    MutablePadSource source;
    source.next.buttons = static_cast<uint16_t>(0xFFFFu & ~kPadCross);
    system.sio0().setPadSource(&source);

    for (int round = 0; round < 4; ++round) {
        source.next.buttons = static_cast<uint16_t>(0xFFFFu & ~(round & 1 ? kPadStart : kPadCross));
        setControl(system, kTxEnable);
        setControl(system, kTxEnable | kSelect);
        const uint8_t expectedLo = static_cast<uint8_t>(round & 1 ? 0xF7u : 0xFFu);
        const uint8_t expectedHi = static_cast<uint8_t>(round & 1 ? 0xFFu : 0xBFu);
        CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
        CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
        CHECK_EQ(transfer(system, 0x00), expectedLo);
        CHECK_EQ(transfer(system, 0x00), expectedHi);
    }
    CHECK_EQ(source.samples, 4);

    system.sio0().setPadSource(nullptr);
}

TEST_CASE(sio0_cross_press_release_roundtrip_through_protocol) {
    Memory memory;
    System system(memory);
    system.reset();
    setControl(system, kTxEnable | kSelect);
    system.sio0().setPadAnalogMode(false);

    MutablePadSource source;
    system.sio0().setPadSource(&source);

    auto pollButtons = [&](uint16_t buttons) {
        source.next.buttons = buttons;
        setControl(system, kTxEnable);
        setControl(system, kTxEnable | kSelect);
        CHECK_EQ(transfer(system, 0x01), static_cast<uint8_t>(0xFF));
        CHECK_EQ(transfer(system, 0x42), static_cast<uint8_t>(0x41));
        CHECK_EQ(transfer(system, 0x00), static_cast<uint8_t>(0x5A));
        const uint8_t lo = transfer(system, 0x00);
        const uint8_t hi = transfer(system, 0x00);
        return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8u));
    };

    // Press Cross.
    uint16_t observed = pollButtons(static_cast<uint16_t>(0xFFFFu & ~kPadCross));
    CHECK_EQ(static_cast<uint8_t>(observed >> 8), static_cast<uint8_t>(0xBFu));

    // Release Cross: both bytes return to 0xFF.
    observed = pollButtons(0xFFFFu);
    CHECK_EQ(static_cast<uint8_t>(observed), static_cast<uint8_t>(0xFFu));
    CHECK_EQ(static_cast<uint8_t>(observed >> 8), static_cast<uint8_t>(0xFFu));

    // Start + Select together.
    observed = pollButtons(static_cast<uint16_t>(0xFFFFu & ~(kPadStart | kPadSelect)));
    CHECK_EQ(static_cast<uint8_t>(observed), static_cast<uint8_t>(0xF6u));

    system.sio0().setPadSource(nullptr);
}
