#pragma once

#include <cstdint>

namespace psxreborn::inputtrace {

// Enabled by environment variable PSXREBORN_TRACE_INPUT.
//   unset  : tracing off, all calls are no-ops
//   "1"    : level 1 - state changes (publish/push), transaction latches on
//            button change, 1 Hz summary
//   "2"    : level 2 - additionally every poll, every transaction start and
//            every pad response byte
// All timestamps are steady_clock nanoseconds since epoch (CLOCK_MONOTONIC).
// The uinput injector in the input harness uses the same clock, so injection
// timestamps and trace timestamps are directly comparable.

bool enabled();
bool verbose();
int64_t nowNs();

void logLine(const char* fmt, ...);

// State was acquired from the host input backend and stored (Play side).
void notePublish(uint16_t buttons, const char* source);

// Host state was pushed into the guest-facing pad state (System/Sio0 path).
void notePush(uint16_t buttons);

// A pad transaction latched its response (command byte handled, slot selected).
// Level 1 logs only when the latched button value differs from the last logged
// one; level 2 logs every transaction.
void noteTx(uint8_t command, uint16_t buttons, unsigned slot);

// One pad response byte was emitted (level 2 only).
void noteTxByte(uint8_t command, unsigned index, uint8_t value);

// Backend polled SDL state without a value change (level 2 only).
void notePoll(uint16_t buttons);

struct Summary {
    uint64_t publishes = 0;
    uint64_t pushes = 0;
    uint64_t transactions = 0;
    uint64_t bytes = 0;
    uint64_t polls = 0;
    uint64_t avgTxIntervalUs = 0;
    uint64_t maxTxIntervalUs = 0;
};

Summary summary();

}  // namespace psxreborn::inputtrace
