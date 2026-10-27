#include "psxreborn/hardware.hpp"

#include "psxreborn/cpu.hpp"
#include "psxreborn/memory.hpp"
#include "psxreborn/mips.hpp"

namespace psxreborn {

namespace {

constexpr uint32_t kTimerBase = 0x1F801100u;
constexpr uint32_t kTimerStride = 0x10u;
constexpr uint32_t kCyclesPerFrame = 33868800u / 60u;

}  // namespace

void Interrupts::reset() {
    status_ = 0;
    mask_ = 0;
}

bool Interrupts::read32(uint32_t address, uint32_t& value) const {
    if (address == kStatAddress) {
        value = status_;
        return true;
    }
    if (address == kMaskAddress) {
        value = mask_;
        return true;
    }
    return false;
}

bool Interrupts::write32(uint32_t address, uint32_t value) {
    if (address == kStatAddress) {
        status_ &= value & 0x7FFu;
        return true;
    }
    if (address == kMaskAddress) {
        mask_ = value & 0x7FFu;
        return true;
    }
    return false;
}

Timers::Timers(Interrupts& interrupts) : interrupts_(interrupts) {}

void Timers::reset() {
    counters_ = {};
}

bool Timers::counterIndex(uint32_t address, unsigned& index, uint32_t& offset) {
    if (address < kTimerBase || address >= kTimerBase + 3u * kTimerStride) {
        return false;
    }
    const uint32_t relative = address - kTimerBase;
    index = relative / kTimerStride;
    offset = relative % kTimerStride;
    return offset == 0 || offset == 4 || offset == 8;
}

void Timers::triggerIrq(unsigned index) {
    Counter& counter = counters_[index];
    if (counter.irqActive) {
        return;
    }
    if ((counter.mode & 0x40u) != 0 && counter.oneShotDone) {
        return;
    }
    counter.irqActive = true;
    counter.oneShotDone = true;
    interrupts_.request(Interrupts::kTimer0 << index);
}

void Timers::stepCounter(unsigned index) {
    Counter& counter = counters_[index];
    if (index == 2) {
        if ((counter.mode & 1u) != 0) {
            const unsigned sync = (counter.mode >> 1) & 3u;
            if (sync == 0 || sync == 3) {
                return;
            }
        }
        if ((counter.mode & 0x200u) != 0) {
            counter.prescale = (counter.prescale + 1u) & 7u;
            if (counter.prescale != 0) {
                return;
            }
        }
    }

    ++counter.count;
    if (counter.count == counter.target && (counter.mode & 8u) != 0) {
        counter.equal = true;
        if ((counter.mode & 0x10u) != 0) {
            triggerIrq(index);
        }
        counter.count = 0;
        return;
    }
    if (counter.count == 0) {
        counter.overflow = true;
        if ((counter.mode & 0x20u) != 0) {
            triggerIrq(index);
        }
        return;
    }
    if (counter.count == counter.target) {
        counter.equal = true;
        if ((counter.mode & 0x10u) != 0) {
            triggerIrq(index);
        }
    }
}

void Timers::tick(uint32_t cycles) {
    for (uint32_t cycle = 0; cycle < cycles; ++cycle) {
        for (unsigned index = 0; index < counters_.size(); ++index) {
            if (index == 1 && (counters_[index].mode & 0x0100u) != 0) {
                continue;
            }
            if (index == 0 && (counters_[index].mode & 0x0100u) != 0) {
                continue;
            }
            stepCounter(index);
        }
    }
}

void Timers::tickHblank() {
    if ((counters_[1].mode & 0x0100u) == 0) {
        return;
    }
    stepCounter(1);
}

bool Timers::read32(uint32_t address, uint32_t& value) const {
    unsigned index = 0;
    uint32_t offset = 0;
    if (!counterIndex(address, index, offset)) {
        return false;
    }
    const Counter& counter = counters_[index];
    if (offset == 0) {
        value = counter.count;
        return true;
    }
    if (offset == 4) {
        uint16_t mode = counter.mode & 0x3FFu;
        if (!counter.irqActive) {
            mode |= 0x400u;
        }
        if (counter.equal) {
            mode |= 0x800u;
        }
        if (counter.overflow) {
            mode |= 0x1000u;
        }
        value = mode;
        return true;
    }
    value = counter.target;
    return true;
}

bool Timers::write32(uint32_t address, uint32_t value) {
    unsigned index = 0;
    uint32_t offset = 0;
    if (!counterIndex(address, index, offset)) {
        return false;
    }
    Counter& counter = counters_[index];
    if (offset == 0) {
        counter.count = static_cast<uint16_t>(value);
        return true;
    }
    if (offset == 4) {
        counter.mode = static_cast<uint16_t>(value & 0x3FFu);
        counter.count = 0;
        counter.irqActive = false;
        counter.equal = false;
        counter.overflow = false;
        counter.oneShotDone = false;
        counter.prescale = 0;
        return true;
    }
    counter.target = static_cast<uint16_t>(value);
    return true;
}

uint16_t Timers::count(unsigned index) const {
    return index < counters_.size() ? counters_[index].count : 0;
}

uint16_t Timers::mode(unsigned index) const {
    return index < counters_.size() ? counters_[index].mode : 0;
}

uint16_t Timers::target(unsigned index) const {
    return index < counters_.size() ? counters_[index].target : 0;
}

System::System(Memory& memory)
    : memory_(memory),
      timers_(interrupts_),
      gpu_(),
      spu_(),
      dma_(memory_, interrupts_, gpu_, &spu_),
      cdrom_(interrupts_) {
    dma_.setCdrom(&cdrom_);
    memory_.setIoDevice(this);
}

System::~System() {
    memory_.setIoDevice(nullptr);
}

void System::reset() {
    interrupts_.reset();
    timers_.reset();
    gpu_.reset();
    spu_.reset();
    dma_.reset();
    cdrom_.reset();
    vblankCycles_ = 0;
    hblankCycles_ = 0;
    misc_.fill(0);
    const auto store = [this](uint32_t address, uint32_t value) {
        uint32_t offset = 0;
        if (!miscIndex(address, offset)) {
            return;
        }
        for (unsigned byte = 0; byte < 4; ++byte) {
            misc_[offset + byte] = static_cast<uint8_t>((value >> (byte * 8u)) & 0xFFu);
        }
    };
    store(0x1F801000u, 0x1F000000u);
    store(0x1F801004u, 0x1F802000u);
    store(0x1F801008u, 0x0013243Fu);
    store(0x1F80100Cu, 0x00003022u);
    store(0x1F801010u, 0x0013243Fu);
    store(0x1F801014u, 0x200931E1u);
    store(0x1F801018u, 0x00020843u);
    store(0x1F80101Cu, 0x00070777u);
    store(0x1F801020u, 0x00031125u);
    store(0x1F801060u, 0x00000B88u);
}

void System::bind(Cpu& cpu) {
    cpu.setInterruptSource([this]() { return irqPending(); });
    cpu.setCycleCallback([this](uint32_t cycles) { tick(cycles); });
}

void System::tick(uint32_t cycles) {
    timers_.tick(cycles);
    gpu_.tick(cycles);
    cdrom_.tick(cycles);
    vblankCycles_ += cycles;
    hblankCycles_ += cycles;
    constexpr uint32_t cyclesPerScanline = kCyclesPerFrame / 263u;
    while (hblankCycles_ >= cyclesPerScanline) {
        hblankCycles_ -= cyclesPerScanline;
        timers_.tickHblank();
    }
    while (vblankCycles_ >= kCyclesPerFrame) {
        vblankCycles_ -= kCyclesPerFrame;
        interrupts_.request(Interrupts::kVBlank);
        if (vblankHandler_) {
            vblankHandler_();
        }
    }
}

bool System::irqPending() const {
    return interrupts_.pending();
}

void System::setFault(MemoryFault* fault, MemoryFault value) {
    if (fault != nullptr) {
        *fault = value;
    }
}

uint32_t System::extract(uint32_t word, uint32_t address, unsigned size) {
    const unsigned shift = (address & 3u) * 8u;
    const uint32_t mask = size == 4 ? 0xFFFFFFFFu : ((1u << (size * 8u)) - 1u);
    return (word >> shift) & mask;
}

bool System::miscIndex(uint32_t address, uint32_t& offset) {
    if (address < kMiscBase || address >= kMiscBase + kMiscSize) {
        return false;
    }
    offset = address - kMiscBase;
    return true;
}

bool System::read8(uint32_t address, uint8_t& value, MemoryFault* fault) {
    bool handled = false;
    if (cdrom_.read8(address, value, handled)) {
        setFault(fault, MemoryFault::None);
        return true;
    }
    uint32_t word = 0;
    if (!read32(address & ~3u, word, fault)) {
        return false;
    }
    value = static_cast<uint8_t>(extract(word, address, 1));
    return true;
}

bool System::read16(uint32_t address, uint16_t& value, MemoryFault* fault) {
    if (spu_.read16(address, value)) {
        setFault(fault, MemoryFault::None);
        return true;
    }
    uint32_t word = 0;
    if (!read32(address & ~3u, word, fault)) {
        return false;
    }
    value = static_cast<uint16_t>(extract(word, address, 2));
    return true;
}

bool System::read32(uint32_t address, uint32_t& value, MemoryFault* fault) {
    if (interrupts_.read32(address, value) || timers_.read32(address, value) ||
        dma_.read32(address, value) || gpu_.read32(address, value, fault) ||
        spu_.read32(address, value)) {
        setFault(fault, MemoryFault::None);
        return true;
    }
    uint32_t offset = 0;
    if (miscIndex(address, offset)) {
        value = static_cast<uint32_t>(misc_[offset]) |
                (static_cast<uint32_t>(misc_[offset + 1]) << 8) |
                (static_cast<uint32_t>(misc_[offset + 2]) << 16) |
                (static_cast<uint32_t>(misc_[offset + 3]) << 24);
        setFault(fault, MemoryFault::None);
        return true;
    }
    setFault(fault, MemoryFault::Unsupported);
    return false;
}

bool System::write8(uint32_t address, uint8_t value, MemoryFault* fault) {
    bool handled = false;
    if (cdrom_.write8(address, value, handled)) {
        setFault(fault, MemoryFault::None);
        return true;
    }
    uint32_t word = 0;
    if (!read32(address & ~3u, word, fault)) {
        return false;
    }
    const unsigned shift = (address & 3u) * 8u;
    word = (word & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
    return write32(address & ~3u, word, fault);
}

bool System::write16(uint32_t address, uint16_t value, MemoryFault* fault) {
    if (spu_.write16(address, value)) {
        setFault(fault, MemoryFault::None);
        return true;
    }
    uint32_t word = 0;
    if (!read32(address & ~3u, word, fault)) {
        return false;
    }
    const unsigned shift = (address & 3u) * 8u;
    word = (word & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(value) << shift);
    return write32(address & ~3u, word, fault);
}

bool System::write32(uint32_t address, uint32_t value, MemoryFault* fault) {
    if (interrupts_.write32(address, value) || timers_.write32(address, value) ||
        dma_.write32(address, value) || gpu_.write32(address, value, fault) ||
        spu_.write32(address, value)) {
        setFault(fault, MemoryFault::None);
        return true;
    }
    uint32_t offset = 0;
    if (miscIndex(address, offset)) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            misc_[offset + byte] = static_cast<uint8_t>((value >> (byte * 8u)) & 0xFFu);
        }
        setFault(fault, MemoryFault::None);
        return true;
    }
    setFault(fault, MemoryFault::Unsupported);
    return false;
}

}  // namespace psxreborn
