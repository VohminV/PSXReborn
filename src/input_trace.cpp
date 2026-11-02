#include "psxreborn/input_trace.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace psxreborn::inputtrace {
namespace {

bool envEnabled() { return std::getenv("PSXREBORN_TRACE_INPUT") != nullptr; }

bool envVerbose() {
    const char* value = std::getenv("PSXREBORN_TRACE_INPUT");
    return value != nullptr && value[0] == '2';
}

bool envChain() {
    const char* value = std::getenv("PSXREBORN_TRACE_INPUT");
    if (value != nullptr && value[0] == '3') {
        return true;
    }
    const char* chain = std::getenv("PSXREBORN_TRACE_CHAIN");
    return chain != nullptr && chain[0] != '0';
}

uint64_t g_frame = 0;

uint64_t g_publishes = 0;
uint64_t g_pushes = 0;
uint64_t g_transactions = 0;
uint64_t g_bytes = 0;
uint64_t g_polls = 0;

uint16_t g_lastLoggedTxButtons = 0;
bool g_haveLoggedTx = false;

int64_t g_lastTxNs = 0;
uint64_t g_txIntervalSumNs = 0;
uint64_t g_txIntervalMaxNs = 0;
uint64_t g_txIntervals = 0;

}  // namespace

bool enabled() {
    static const bool value = envEnabled();
    return value;
}

bool verbose() {
    static const bool value = envEnabled() && envVerbose();
    return value;
}

bool chainEnabled() {
    static const bool value = envChain();
    return value;
}

void beginFrame(uint64_t frame) {
    g_frame = frame;
}

uint64_t frameNumber() {
    return g_frame;
}

int64_t nowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + static_cast<int64_t>(ts.tv_nsec);
}

void logLine(const char* fmt, ...) {
    if (!enabled()) {
        return;
    }
    std::fprintf(stderr, "[INPUT] frame=%llu t_ns=%lld ",
                 static_cast<unsigned long long>(g_frame),
                 static_cast<long long>(nowNs()));
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

void chainLine(const char* layer, const char* fmt, ...) {
    if (!chainEnabled()) {
        return;
    }
    std::fprintf(stderr, "[CHAIN] frame=%llu t_ns=%lld layer=%s ",
                 static_cast<unsigned long long>(g_frame),
                 static_cast<long long>(nowNs()), layer);
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

void notePublish(uint16_t buttons, const char* source) {
    if (!enabled()) {
        return;
    }
    ++g_publishes;
    if (verbose()) {
        logLine("ev=publish buttons=%04X src=%s", buttons, source);
        return;
    }
    static uint16_t last = 0;
    static bool have = false;
    if (!have || last != buttons) {
        have = true;
        last = buttons;
        logLine("ev=publish buttons=%04X src=%s", buttons, source);
    }
}

void notePush(uint16_t buttons) {
    if (!enabled()) {
        return;
    }
    ++g_pushes;
    static uint16_t last = 0;
    static bool have = false;
    if (!have || last != buttons) {
        have = true;
        last = buttons;
        logLine("ev=push buttons=%04X", buttons);
    }
}

void noteTx(uint8_t command, uint16_t buttons, unsigned slot) {
    if (!enabled()) {
        return;
    }
    ++g_transactions;
    const int64_t now = nowNs();
    if (g_lastTxNs != 0) {
        const uint64_t delta = static_cast<uint64_t>(now - g_lastTxNs);
        g_txIntervalSumNs += delta;
        ++g_txIntervals;
        if (delta > g_txIntervalMaxNs) {
            g_txIntervalMaxNs = delta;
        }
    }
    g_lastTxNs = now;
    if (!verbose()) {
        if (g_haveLoggedTx && g_lastLoggedTxButtons == buttons) {
            return;
        }
        g_haveLoggedTx = true;
        g_lastLoggedTxButtons = buttons;
    }
    logLine("ev=tx cmd=%02X buttons=%04X slot=%u", command, buttons, slot);
}

void noteTxByte(uint8_t command, unsigned index, uint8_t value) {
    if (!verbose()) {
        return;
    }
    ++g_bytes;
    logLine("ev=tx_byte cmd=%02X i=%u b=%02X", command, index, value);
}

void notePoll(uint16_t buttons) {
    if (!verbose()) {
        return;
    }
    ++g_polls;
    logLine("ev=poll buttons=%04X", buttons);
}

Summary summary() {
    Summary out;
    out.publishes = g_publishes;
    out.pushes = g_pushes;
    out.transactions = g_transactions;
    out.bytes = g_bytes;
    out.polls = g_polls;
    if (g_txIntervals != 0) {
        out.avgTxIntervalUs = (g_txIntervalSumNs / g_txIntervals) / 1000u;
        out.maxTxIntervalUs = g_txIntervalMaxNs / 1000u;
    }
    return out;
}

}  // namespace psxreborn::inputtrace
