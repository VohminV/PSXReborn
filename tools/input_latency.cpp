// Standalone input-latency harness for the PadSource -> SIO0 path.
//
// Measures:
//   1) cost of one full digital-pad transaction (select + 4 data bytes)
//   2) host state update -> first guest-visible button byte
// and prints percentiles plus the old frame-quantized baseline for
// comparison. Run with no arguments; exits 0 on success.
//
// Build is wired into CMake as target `psxreborn_input_latency` when tests
// are enabled.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "psxreborn/hardware.hpp"
#include "psxreborn/memory.hpp"
#include "psxreborn/pad.hpp"

using namespace psxreborn;
using Clock = std::chrono::steady_clock;

namespace {

class FixedSource final : public PadSource {
public:
    PadSnapshot next{};
    PadSnapshot sample() override { return next; }
};

uint64_t nsSince(Clock::time_point start) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

void setControl(System& system, uint16_t value) {
    MemoryFault fault = MemoryFault::None;
    system.write16(0x1F80104Au, value, &fault);
}

uint8_t transfer(System& system, uint8_t value) {
    MemoryFault fault = MemoryFault::None;
    system.write8(0x1F801040u, value, &fault);
    uint8_t response = 0xFF;
    system.read8(0x1F801040u, response, &fault);
    return response;
}

uint64_t percentile(std::vector<uint64_t> values, double p) {
    std::sort(values.begin(), values.end());
    const std::size_t idx =
        static_cast<std::size_t>(p * static_cast<double>(values.size() - 1));
    return values[idx];
}

uint64_t average(const std::vector<uint64_t>& values) {
    uint64_t sum = 0;
    for (uint64_t value : values) {
        sum += value;
    }
    return values.empty() ? 0 : sum / values.size();
}

void runDigitalTransaction(System& system) {
    setControl(system, 0x0001u);
    setControl(system, 0x0003u);
    transfer(system, 0x01);
    transfer(system, 0x42);
    transfer(system, 0x00);
    transfer(system, 0x00);
    transfer(system, 0x00);
}

}  // namespace

int main() {
    Memory memory;
    System system(memory);
    system.reset();
    FixedSource source;
    system.sio0().setPadSource(&source);
    system.sio0().setPadAnalogMode(false);
    setControl(system, 0x0003u);

    for (int i = 0; i < 100; ++i) {
        runDigitalTransaction(system);
    }

    constexpr int kIters = 20000;
    std::vector<uint64_t> txNs;
    txNs.reserve(kIters);
    for (int i = 0; i < kIters; ++i) {
        setControl(system, 0x0001u);
        setControl(system, 0x0003u);
        const auto start = Clock::now();
        transfer(system, 0x01);
        transfer(system, 0x42);
        transfer(system, 0x00);
        transfer(system, 0x00);
        transfer(system, 0x00);
        txNs.push_back(nsSince(start));
    }

    constexpr int kLatIters = 20000;
    std::vector<uint64_t> latNs;
    latNs.reserve(kLatIters);
    for (int i = 0; i < kLatIters; ++i) {
        runDigitalTransaction(system);
        setControl(system, 0x0001u);
        const auto start = Clock::now();
        source.next.buttons =
            static_cast<uint16_t>(0xFFFFu & (i & 1 ? ~kPadCross : ~kPadStart));
        setControl(system, 0x0003u);
        transfer(system, 0x01);
        const uint8_t id = transfer(system, 0x42);
        transfer(system, 0x00);
        transfer(system, 0x00);
        latNs.push_back(nsSince(start));
        if (id != 0x41) {
            std::fprintf(stderr, "unexpected id %02X\n", id);
            return 1;
        }
    }

    std::printf("full_digital_transaction_ns avg=%llu p50=%llu p95=%llu p99=%llu\n",
                (unsigned long long)average(txNs), (unsigned long long)percentile(txNs, 0.50),
                (unsigned long long)percentile(txNs, 0.95),
                (unsigned long long)percentile(txNs, 0.99));
    std::printf("host_update_to_guest_byte_ns avg=%llu p50=%llu p95=%llu p99=%llu\n",
                (unsigned long long)average(latNs), (unsigned long long)percentile(latNs, 0.50),
                (unsigned long long)percentile(latNs, 0.95),
                (unsigned long long)percentile(latNs, 0.99));
    std::printf("frame_quantized_baseline_ns (60fps)=%llu (144fps)=%llu\n",
                (unsigned long long)(1000000000ull / 60ull),
                (unsigned long long)(1000000000ull / 144ull));
    return 0;
}
