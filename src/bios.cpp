#include "psxreborn/bios.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "psxreborn/cdrom.hpp"
#include "psxreborn/cpu.hpp"
#include "psxreborn/hardware.hpp"

namespace psxreborn {

namespace {

constexpr uint8_t kA0 = 4;
constexpr uint8_t kA1 = 5;
constexpr uint8_t kA2 = 6;
constexpr uint8_t kA3 = 7;
constexpr uint8_t kV0 = 2;
constexpr uint8_t kT1 = 9;
constexpr uint8_t kSp = 29;
constexpr uint8_t kRa = 31;

constexpr uint32_t kEventVBlank = 0xF0000009u;
constexpr uint32_t kEventVBlankSpec = 0x20u;
constexpr uint32_t kEventCdromA = 0xF4000001u;
constexpr uint32_t kEventCdromB = 0xF0000011u;
constexpr uint32_t kCyclesPerFrame = 33868800u / 60u;

}  // namespace

Bios::Bios(Cpu& cpu, Memory& memory, System& system)
    : cpu_(cpu), memory_(memory), system_(system) {}

Bios::~Bios() = default;

void Bios::padInit(uint32_t buf1, uint32_t siz1, uint32_t buf2, uint32_t siz2) {
    (void)siz1;
    (void)siz2;
    padBuf1_ = buf1;
    padBuf2_ = buf2;
    if (padBuf1_ != 0) {
        for (unsigned i = 0; i < 0x22u; ++i) {
            memory_.write8(padBuf1_ + i, 0);
        }
        memory_.write8(padBuf1_ + 1, 0x41);
    }
    if (padBuf2_ != 0) {
        for (unsigned i = 0; i < 0x22u; ++i) {
            memory_.write8(padBuf2_ + i, 0);
        }
        memory_.write8(padBuf2_ + 1, 0x41);
    }
}

void Bios::padStart() {
    padEnabled_ = true;
}

void Bios::padStop() {
    padEnabled_ = false;
}

void Bios::padUpdate() {
    if (padBuf1_ != 0) {
        memory_.write8(padBuf1_ + 0, 0);
        memory_.write8(padBuf1_ + 1, 0x41);
        memory_.write8(padBuf1_ + 2, static_cast<uint8_t>(padButtons_ & 0xFFu));
        memory_.write8(padBuf1_ + 3, static_cast<uint8_t>((padButtons_ >> 8) & 0xFFu));
    }
    if (padBuf2_ != 0) {
        memory_.write8(padBuf2_ + 0, 0);
        memory_.write8(padBuf2_ + 1, 0x41);
        memory_.write8(padBuf2_ + 2, static_cast<uint8_t>(padButtons_ & 0xFFu));
        memory_.write8(padBuf2_ + 3, static_cast<uint8_t>((padButtons_ >> 8) & 0xFFu));
    }
}

void Bios::reset() {
    events_.clear();
    heap_.clear();
    files_.assign(3, {});
    heapStart_ = 0;
    heapSize_ = 0;
    randState_ = 1;
    installed_ = false;
}

void Bios::install() {
    if (installed_) {
        return;
    }
    tableBase_ = 0x8006F000u;
    for (uint32_t index = 0; index < 0x100; ++index) {
        write32(tableBase_ + index * 4u, 0xC000u | index);
        write32(tableBase_ + 0x400u + index * 4u, 0xB000u | index);
    }
    cpu_.setInstructionHook([this](uint32_t pc) { return onInstruction(pc); });
    cpu_.setSyscallHandler([this]() { return onSyscall(); });
    system_.setVBlankHandler([this]() { handleVBlank(); });
    system_.cdrom().setInterruptCallback([this](uint8_t interrupt) {
        handleCdromInterrupt(interrupt);
    });
    installed_ = true;
}

void Bios::handleVBlank() {
    deliverEventClass(kEventVBlank, kEventVBlankSpec);
    deliverEventClass(0xF0000001u, 0x0001u);
    if (padEnabled_) {
        padUpdate();
    }
}

void Bios::handleCdromInterrupt(uint8_t interrupt) {
    uint32_t spec = 0;
    switch (interrupt) {
        case 1:
        case 4:
            spec = 0x0100u;
            break;
        case 2:
            spec = 0x8000u;
            break;
        case 3:
            spec = 0x0004u;
            break;
        case 5:
            spec = 0x2000u;
            break;
        default:
            return;
    }
    deliverEventClass(kEventCdromA, spec);
    deliverEventClass(kEventCdromB, spec);
}

bool Bios::onInstruction(uint32_t pc) {
    uint8_t vector = 0;
    uint8_t function = 0;
    const uint32_t low = pc & 0xFFFFFFu;
    if (pc == 0xA0u) {
        vector = 0xA0;
        function = static_cast<uint8_t>(cpu_.reg(kT1));
    } else if (pc == 0xB0u) {
        vector = 0xB0;
        function = static_cast<uint8_t>(cpu_.reg(kT1));
    } else if (pc == 0xC0u) {
        vector = 0xC0;
        function = static_cast<uint8_t>(cpu_.reg(kT1));
    } else if (low >= 0xA000u && low < 0xA100u) {
        vector = 0xA0;
        function = static_cast<uint8_t>(low);
    } else if (low >= 0xB000u && low < 0xB100u) {
        vector = 0xB0;
        function = static_cast<uint8_t>(low);
    } else if (low >= 0xC000u && low < 0xC100u) {
        vector = 0xC0;
        function = static_cast<uint8_t>(low);
    } else {
        return false;
    }

    dispatch(vector, function);
    cpu_.setPc(cpu_.reg(kRa));
    return true;
}

bool Bios::onSyscall() {
    const uint32_t function = cpu_.reg(kA0);
    switch (function) {
        case 1: {
            const uint32_t status = cpu_.status();
            cpu_.setReg(kV0, status & 0x404u);
            cpu_.setCop0(12, status & ~0x404u);
            return true;
        }
        case 2: {
            cpu_.setCop0(12, cpu_.status() | 0x404u);
            cpu_.setReg(kV0, 0);
            return true;
        }
        default:
            return false;
    }
}

void Bios::dispatch(uint8_t vector, uint8_t function) {
    static const bool trace = std::getenv("PSXREBORN_TRACE") != nullptr;
    if (trace) {
        std::cerr << "[BIOS] " << (vector == 0xA0 ? "A0:" : vector == 0xB0 ? "B0:" : "C0:")
                  << std::hex << static_cast<unsigned>(function) << std::dec << " ra=0x"
                  << std::hex << cpu_.reg(kRa) << " a0=0x" << cpu_.reg(kA0) << " a1=0x"
                  << cpu_.reg(kA1) << " a2=0x" << cpu_.reg(kA2) << std::dec << "\n";
    }
    switch (vector) {
        case 0xA0:
            a0Call(function);
            break;
        case 0xB0:
            b0Call(function);
            break;
        case 0xC0:
            c0Call(function);
            break;
        default:
            break;
    }
}

uint32_t Bios::read32(uint32_t address) const {
    uint32_t value = 0;
    memory_.read32(address, value);
    return value;
}

uint16_t Bios::read16(uint32_t address) const {
    uint16_t value = 0;
    memory_.read16(address, value);
    return value;
}

uint8_t Bios::read8(uint32_t address) const {
    uint8_t value = 0;
    memory_.read8(address, value);
    return value;
}

void Bios::write32(uint32_t address, uint32_t value) {
    memory_.write32(address, value);
}

void Bios::write8(uint32_t address, uint8_t value) {
    memory_.write8(address, value);
}

std::string Bios::readString(uint32_t address, size_t limit) const {
    std::string result;
    for (size_t index = 0; index < limit; ++index) {
        const uint8_t character = read8(address + static_cast<uint32_t>(index));
        if (character == 0) {
            break;
        }
        result.push_back(static_cast<char>(character));
    }
    return result;
}

void Bios::writeString(uint32_t address, const std::string& value) {
    for (size_t index = 0; index < value.size(); ++index) {
        write8(address + static_cast<uint32_t>(index), static_cast<uint8_t>(value[index]));
    }
    write8(address + static_cast<uint32_t>(value.size()), 0);
}

void Bios::print(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

uint32_t Bios::fetchArgument(unsigned index, unsigned firstRegister) const {
    const unsigned absolute = firstRegister + index;
    if (absolute <= kA3) {
        return cpu_.reg(static_cast<uint8_t>(absolute));
    }
    const uint32_t stack = cpu_.reg(kSp);
    return read32(stack + 0x10u + (absolute - kA3 - 1u) * 4u);
}

std::string Bios::formatString(uint32_t formatAddress, unsigned firstRegister) const {
    const std::string format = readString(formatAddress);
    std::string output;
    unsigned argument = 0;
    for (size_t index = 0; index < format.size(); ++index) {
        const char character = format[index];
        if (character != '%') {
            output.push_back(character);
            continue;
        }
        ++index;
        if (index >= format.size()) {
            break;
        }
        while (index < format.size() &&
               (format[index] == '-' || format[index] == '+' || format[index] == ' ' ||
                format[index] == '0' || format[index] == '#')) {
            ++index;
        }
        while (index < format.size() && format[index] >= '0' && format[index] <= '9') {
            ++index;
        }
        if (index < format.size() && format[index] == '.') {
            ++index;
            while (index < format.size() && format[index] >= '0' && format[index] <= '9') {
                ++index;
            }
        }
        while (index < format.size() && (format[index] == 'l' || format[index] == 'h')) {
            ++index;
        }
        if (index >= format.size()) {
            break;
        }
        char buffer[64];
        switch (format[index]) {
            case '%':
                output.push_back('%');
                break;
            case 'c':
                output.push_back(static_cast<char>(fetchArgument(argument++, firstRegister) & 0xFFu));
                break;
            case 's':
                output.append(readString(fetchArgument(argument++, firstRegister)));
                break;
            case 'd':
            case 'i':
                std::snprintf(buffer, sizeof(buffer), "%d",
                              static_cast<int32_t>(fetchArgument(argument++, firstRegister)));
                output.append(buffer);
                break;
            case 'u':
                std::snprintf(buffer, sizeof(buffer), "%u", fetchArgument(argument++, firstRegister));
                output.append(buffer);
                break;
            case 'x':
                std::snprintf(buffer, sizeof(buffer), "%x", fetchArgument(argument++, firstRegister));
                output.append(buffer);
                break;
            case 'X':
                std::snprintf(buffer, sizeof(buffer), "%X", fetchArgument(argument++, firstRegister));
                output.append(buffer);
                break;
            case 'p':
                std::snprintf(buffer, sizeof(buffer), "0x%08x",
                              fetchArgument(argument++, firstRegister));
                output.append(buffer);
                break;
            default:
                output.push_back('%');
                output.push_back(format[index]);
                break;
        }
    }
    return output;
}

uint32_t Bios::openFile(const std::string& path, uint32_t mode) {
    if ((mode & 3u) != 1u) {
        return 0xFFFFFFFFu;
    }
    std::string normalized = path;
    const size_t colon = normalized.find(':');
    if (colon != std::string::npos) {
        normalized = normalized.substr(colon + 1);
    }
    while (!normalized.empty() && (normalized.front() == '\\' || normalized.front() == '/')) {
        normalized.erase(normalized.begin());
    }
    if (disc_ == nullptr) {
        return 0xFFFFFFFFu;
    }
    std::vector<uint8_t> data;
    if (!disc_->readFile(normalized, data)) {
        std::cerr << "[BIOS] open failed: " << path << "\n";
        return 0xFFFFFFFFu;
    }
    for (size_t index = 3; index < files_.size(); ++index) {
        if (!files_[index].used) {
            files_[index].used = true;
            files_[index].data = std::move(data);
            files_[index].position = 0;
            return static_cast<uint32_t>(index);
        }
    }
    files_.push_back({});
    files_.back().used = true;
    files_.back().data = std::move(data);
    return static_cast<uint32_t>(files_.size() - 1);
}

uint32_t Bios::readFile(uint32_t fd, uint32_t address, uint32_t size) {
    if (fd >= files_.size() || !files_[fd].used) {
        return 0xFFFFFFFFu;
    }
    OpenFile& file = files_[fd];
    const size_t available = file.position < file.data.size() ? file.data.size() - file.position : 0;
    const size_t count = std::min<size_t>(size, available);
    if (count != 0) {
        memory_.writeBytes(address, file.data.data() + file.position, count);
        file.position += count;
    }
    return static_cast<uint32_t>(count);
}

uint32_t Bios::writeFile(uint32_t fd, uint32_t address, uint32_t size) {
    if (fd == 1 || fd == 2) {
        std::string text;
        text.resize(size);
        for (uint32_t index = 0; index < size; ++index) {
            text[index] = static_cast<char>(read8(address + index));
        }
        print(text);
        return size;
    }
    return 0xFFFFFFFFu;
}

uint32_t Bios::seekFile(uint32_t fd, int32_t offset, uint32_t whence) {
    if (fd >= files_.size() || !files_[fd].used) {
        return 0xFFFFFFFFu;
    }
    OpenFile& file = files_[fd];
    int64_t position = 0;
    if (whence == 0) {
        position = offset;
    } else if (whence == 1) {
        position = static_cast<int64_t>(file.position) + offset;
    } else if (whence == 2) {
        position = static_cast<int64_t>(file.data.size()) + offset;
    } else {
        return 0xFFFFFFFFu;
    }
    if (position < 0) {
        return 0xFFFFFFFFu;
    }
    file.position = static_cast<size_t>(position);
    return static_cast<uint32_t>(file.position);
}

uint32_t Bios::closeFile(uint32_t fd) {
    if (fd >= files_.size() || !files_[fd].used) {
        return 0xFFFFFFFFu;
    }
    files_[fd] = {};
    return 0;
}

uint32_t Bios::openEvent(uint32_t classId, uint32_t specId, uint32_t mode, uint32_t handler) {
    for (size_t index = 0; index < events_.size(); ++index) {
        if (!events_[index].active) {
            events_[index] = Event{classId, specId, mode, handler, false, false, false, true};
            return static_cast<uint32_t>(index);
        }
    }
    events_.push_back(Event{classId, specId, mode, handler, false, false, false, true});
    return static_cast<uint32_t>(events_.size() - 1);
}

uint32_t Bios::closeEvent(uint32_t id) {
    if (id >= events_.size() || !events_[id].active) {
        return 0;
    }
    events_[id].active = false;
    events_[id].enabled = false;
    events_[id].triggered = false;
    events_[id].pending = false;
    return 1;
}

uint32_t Bios::waitEvent(uint32_t id) {
    if (id >= events_.size() || !events_[id].active) {
        return 0;
    }
    for (unsigned attempt = 0; attempt < kCyclesPerFrame / 1024u; ++attempt) {
        Event& event = events_[id];
        if (event.triggered) {
            event.triggered = false;
            return 1;
        }
        system_.tick(1024);
    }
    Event& event = events_[id];
    if (event.triggered) {
        event.triggered = false;
        return 1;
    }
    return 0;
}

uint32_t Bios::testEvent(uint32_t id) {
    if (id >= events_.size() || !events_[id].active) {
        return 0;
    }
    if (events_[id].triggered) {
        events_[id].triggered = false;
        return 1;
    }
    return 0;
}

void Bios::deliverEvent(uint32_t id) {
    if (id >= events_.size() || !events_[id].active) {
        return;
    }
    events_[id].triggered = true;
}

void Bios::deliverEventClass(uint32_t classId, uint32_t specId) {
    for (Event& event : events_) {
        if (!event.active || event.classId != classId || event.specId != specId) {
            continue;
        }
        if (!event.enabled) {
            event.pending = true;
            continue;
        }
        event.triggered = true;
    }
}

void Bios::initHeap(uint32_t address, uint32_t size) {
    heapStart_ = address;
    heapSize_ = size;
    heap_.clear();
    if (size != 0) {
        heap_.push_back({address, size, true});
    }
}

uint32_t Bios::heapAlloc(uint32_t size) {
    if (size == 0) {
        return 0;
    }
    const uint32_t aligned = (size + 7u) & ~7u;
    for (size_t index = 0; index < heap_.size(); ++index) {
        HeapBlock& block = heap_[index];
        if (!block.free || block.size < aligned) {
            continue;
        }
        const uint32_t address = block.address;
        if (block.size >= aligned + 16u) {
            HeapBlock remainder{block.address + aligned, block.size - aligned, true};
            block.size = aligned;
            block.free = false;
            heap_.insert(heap_.begin() + static_cast<std::ptrdiff_t>(index) + 1, remainder);
        } else {
            block.free = false;
        }
        return address;
    }
    std::cerr << "[BIOS] malloc failed: size=" << size << "\n";
    return 0;
}

void Bios::heapFree(uint32_t address) {
    for (HeapBlock& block : heap_) {
        if (block.address == address) {
            block.free = true;
            coalesce();
            return;
        }
    }
}

void Bios::coalesce() {
    std::sort(heap_.begin(), heap_.end(),
              [](const HeapBlock& left, const HeapBlock& right) {
                  return left.address < right.address;
              });
    if (heap_.empty()) {
        return;
    }
    std::vector<HeapBlock> merged;
    merged.push_back(heap_.front());
    for (size_t index = 1; index < heap_.size(); ++index) {
        HeapBlock& previous = merged.back();
        const HeapBlock& current = heap_[index];
        if (previous.free && current.free &&
            previous.address + previous.size == current.address) {
            previous.size += current.size;
        } else {
            merged.push_back(current);
        }
    }
    heap_ = std::move(merged);
}

uint32_t Bios::heapRealloc(uint32_t address, uint32_t size) {
    if (address == 0) {
        return heapAlloc(size);
    }
    uint32_t oldSize = 0;
    for (const HeapBlock& block : heap_) {
        if (block.address == address) {
            oldSize = block.size;
            break;
        }
    }
    if (oldSize == 0) {
        return 0;
    }
    if (size <= oldSize) {
        return address;
    }
    const uint32_t newAddress = heapAlloc(size);
    if (newAddress == 0) {
        return 0;
    }
    std::vector<uint8_t> data(oldSize);
    memory_.readBytes(address, data.data(), oldSize);
    memory_.writeBytes(newAddress, data.data(), oldSize);
    heapFree(address);
    return newAddress;
}

void Bios::a0Call(uint8_t function) {
    switch (function) {
        case 0x00:
            cpu_.setReg(kV0, openFile(readString(cpu_.reg(kA0)), cpu_.reg(kA1)));
            return;
        case 0x01:
            cpu_.setReg(kV0, seekFile(cpu_.reg(kA0), static_cast<int32_t>(cpu_.reg(kA1)),
                                      cpu_.reg(kA2)));
            return;
        case 0x02:
            cpu_.setReg(kV0, readFile(cpu_.reg(kA0), cpu_.reg(kA1), cpu_.reg(kA2)));
            return;
        case 0x03:
            cpu_.setReg(kV0, writeFile(cpu_.reg(kA0), cpu_.reg(kA1), cpu_.reg(kA2)));
            return;
        case 0x04:
            cpu_.setReg(kV0, closeFile(cpu_.reg(kA0)));
            return;
        case 0x05:
            cpu_.setReg(kV0, 0);
            return;
        case 0x0A:
        case 0x28: {
            const uint32_t address = cpu_.reg(kA0);
            const uint32_t size = cpu_.reg(kA1);
            memory_.fill(address, size, 0);
            cpu_.setReg(kV0, address);
            return;
        }
        case 0x0B:
        case 0x29: {
            uint32_t a = cpu_.reg(kA0);
            uint32_t b = cpu_.reg(kA1);
            uint32_t size = cpu_.reg(kA2);
            int result = 0;
            for (uint32_t index = 0; index < size; ++index) {
                result = static_cast<int>(read8(a + index)) - static_cast<int>(read8(b + index));
                if (result != 0) {
                    break;
                }
            }
            cpu_.setReg(kV0, static_cast<uint32_t>(result));
            return;
        }
        case 0x13:
            cpu_.setReg(kV0, 0);
            return;
        case 0x14:
            std::cerr << "[BIOS] longjmp(0x" << std::hex << cpu_.reg(kA0) << std::dec
                      << ") unsupported\n";
            cpu_.setReg(kV0, 0);
            return;
        case 0x15: {
            const uint32_t destination = cpu_.reg(kA0);
            std::string text = readString(destination) + readString(cpu_.reg(kA1));
            writeString(destination, text);
            cpu_.setReg(kV0, destination);
            return;
        }
        case 0x16: {
            const uint32_t destination = cpu_.reg(kA0);
            std::string text = readString(destination);
            const std::string source = readString(cpu_.reg(kA1));
            const uint32_t count = cpu_.reg(kA2);
            text.append(source.substr(0, count));
            writeString(destination, text);
            cpu_.setReg(kV0, destination);
            return;
        }
        case 0x17: {
            const std::string left = readString(cpu_.reg(kA0));
            const std::string right = readString(cpu_.reg(kA1));
            cpu_.setReg(kV0, static_cast<uint32_t>(left.compare(right) < 0
                                                       ? -1
                                                       : (left == right ? 0 : 1)));
            return;
        }
        case 0x18: {
            const uint32_t count = cpu_.reg(kA2);
            const std::string left = readString(cpu_.reg(kA0)).substr(0, count);
            const std::string right = readString(cpu_.reg(kA1)).substr(0, count);
            cpu_.setReg(kV0, static_cast<uint32_t>(left.compare(right) < 0
                                                       ? -1
                                                       : (left == right ? 0 : 1)));
            return;
        }
        case 0x19: {
            const uint32_t destination = cpu_.reg(kA0);
            writeString(destination, readString(cpu_.reg(kA1)));
            cpu_.setReg(kV0, destination);
            return;
        }
        case 0x1A: {
            const uint32_t destination = cpu_.reg(kA0);
            writeString(destination, readString(cpu_.reg(kA1)).substr(0, cpu_.reg(kA2)));
            cpu_.setReg(kV0, destination);
            return;
        }
        case 0x1B:
            cpu_.setReg(kV0, static_cast<uint32_t>(readString(cpu_.reg(kA0)).size()));
            return;
        case 0x1E:
        case 0x1F: {
            const std::string text = readString(cpu_.reg(kA0));
            const char character = static_cast<char>(cpu_.reg(kA1) & 0xFFu);
            const size_t position = function == 0x1E ? text.find(character) : text.rfind(character);
            cpu_.setReg(kV0, position == std::string::npos
                                 ? 0
                                 : cpu_.reg(kA0) + static_cast<uint32_t>(position));
            return;
        }
        case 0x27: {
            const uint32_t destination = cpu_.reg(kA1);
            const uint32_t source = cpu_.reg(kA0);
            const uint32_t size = cpu_.reg(kA2);
            std::vector<uint8_t> data(size);
            memory_.readBytes(source, data.data(), size);
            memory_.writeBytes(destination, data.data(), size);
            cpu_.setReg(kV0, destination);
            return;
        }
        case 0x2A:
        case 0x2C: {
            const uint32_t destination = cpu_.reg(kA0);
            const uint32_t source = cpu_.reg(kA1);
            const uint32_t size = cpu_.reg(kA2);
            std::vector<uint8_t> data(size);
            memory_.readBytes(source, data.data(), size);
            memory_.writeBytes(destination, data.data(), size);
            cpu_.setReg(kV0, destination);
            return;
        }
        case 0x2B: {
            const uint32_t address = cpu_.reg(kA0);
            const uint8_t value = static_cast<uint8_t>(cpu_.reg(kA1));
            const uint32_t size = cpu_.reg(kA2);
            memory_.fill(address, size, value);
            cpu_.setReg(kV0, address);
            return;
        }
        case 0x2D: {
            uint32_t a = cpu_.reg(kA0);
            uint32_t b = cpu_.reg(kA1);
            uint32_t size = cpu_.reg(kA2);
            int result = 0;
            for (uint32_t index = 0; index < size; ++index) {
                result = static_cast<int>(read8(a + index)) - static_cast<int>(read8(b + index));
                if (result != 0) {
                    break;
                }
            }
            cpu_.setReg(kV0, static_cast<uint32_t>(result));
            return;
        }
        case 0x2E: {
            const uint32_t address = cpu_.reg(kA0);
            const uint8_t value = static_cast<uint8_t>(cpu_.reg(kA1));
            const uint32_t size = cpu_.reg(kA2);
            for (uint32_t index = 0; index < size; ++index) {
                if (read8(address + index) == value) {
                    cpu_.setReg(kV0, address + index);
                    return;
                }
            }
            cpu_.setReg(kV0, 0);
            return;
        }
        case 0x2F:
            randState_ = randState_ * 1103515245u + 12345u;
            cpu_.setReg(kV0, (randState_ >> 16u) & 0x7FFFu);
            return;
        case 0x30:
            randState_ = cpu_.reg(kA0);
            return;
        case 0x33:
            cpu_.setReg(kV0, heapAlloc(cpu_.reg(kA0)));
            return;
        case 0x34:
            heapFree(cpu_.reg(kA0));
            cpu_.setReg(kV0, 0);
            return;
        case 0x37: {
            const uint32_t total = cpu_.reg(kA0) * cpu_.reg(kA1);
            const uint32_t address = heapAlloc(total);
            if (address != 0) {
                memory_.fill(address, total, 0);
            }
            cpu_.setReg(kV0, address);
            return;
        }
        case 0x38:
            cpu_.setReg(kV0, heapRealloc(cpu_.reg(kA0), cpu_.reg(kA1)));
            return;
        case 0x39:
            initHeap(cpu_.reg(kA0), cpu_.reg(kA1));
            cpu_.setReg(kV0, 0);
            return;
        case 0x3B:
        case 0x3C:
            cpu_.setReg(kV0, 0xFFFFFFFFu);
            return;
        case 0x3E:
            print(readString(cpu_.reg(kA0)) + "\n");
            cpu_.setReg(kV0, 0);
            return;
        case 0x3F: {
            const std::string text = formatString(cpu_.reg(kA0), kA1);
            print(text);
            cpu_.setReg(kV0, static_cast<uint32_t>(text.size()));
            return;
        }
        case 0x44:
        case 0x40:
            cpu_.setReg(kV0, 0);
            return;
        case 0x46: {
            const uint32_t address = cpu_.reg(kA0);
            const uint32_t count = cpu_.reg(kA1);
            for (uint32_t index = 0; index < count; ++index) {
                system_.gpu().writeGp0(read32(address + index * 4u));
            }
            cpu_.setReg(kV0, 0);
            return;
        }
        case 0x48:
            system_.gpu().write32(Gpu::kGp1Address, cpu_.reg(kA0), nullptr);
            cpu_.setReg(kV0, cpu_.reg(kA0));
            return;
        case 0x49:
            system_.gpu().writeGp0(cpu_.reg(kA0));
            cpu_.setReg(kV0, 0);
            return;
        case 0x4A: {
            const uint32_t address = cpu_.reg(kA0);
            const uint32_t count = cpu_.reg(kA1);
            for (uint32_t index = 0; index < count; ++index) {
                system_.gpu().writeGp0(read32(address + index * 4u));
            }
            cpu_.setReg(kV0, 0);
            return;
        }
        case 0x4B: {
            uint32_t pointer = cpu_.reg(kA0);
            for (unsigned node = 0; node < 0x100000u && pointer != 0x00FFFFFFu; ++node) {
                const uint32_t header = read32(pointer);
                const uint32_t next = header & 0x00FFFFFFu;
                const uint32_t count = header >> 24u;
                for (uint32_t index = 0; index < count; ++index) {
                    system_.gpu().writeGp0(read32(pointer + 4u + index * 4u));
                }
                pointer = next;
            }
            cpu_.setReg(kV0, 0);
            return;
        }
        case 0x4D:
            cpu_.setReg(kV0, system_.gpu().status());
            return;
        case 0x4E:
            cpu_.setReg(kV0, 0);
            return;
        case 0x54:
        case 0x70:
            cpu_.setReg(kV0, 1);
            return;
        case 0x56:
        case 0x72:
            cpu_.setReg(kV0, 0);
            return;
        default:
            std::cerr << "[BIOS] unimplemented A0:" << std::hex << static_cast<unsigned>(function)
                      << std::dec << " a0=0x" << std::hex << cpu_.reg(kA0) << " a1=0x"
                      << cpu_.reg(kA1) << " a2=0x" << cpu_.reg(kA2) << std::dec << "\n";
            cpu_.setReg(kV0, 0);
            return;
    }
}

void Bios::b0Call(uint8_t function) {
    switch (function) {
        case 0x00:
            cpu_.setReg(kV0, heapAlloc(cpu_.reg(kA0)));
            return;
        case 0x01:
            heapFree(cpu_.reg(kA0));
            cpu_.setReg(kV0, 0);
            return;
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
            cpu_.setReg(kV0, 0);
            return;
        case 0x07:
            deliverEvent(cpu_.reg(kA0));
            return;
        case 0x08:
            cpu_.setReg(kV0, openEvent(cpu_.reg(kA0), cpu_.reg(kA1), cpu_.reg(kA2),
                                       cpu_.reg(kA3)));
            return;
        case 0x09:
            cpu_.setReg(kV0, closeEvent(cpu_.reg(kA0)));
            return;
        case 0x0A:
            cpu_.setReg(kV0, waitEvent(cpu_.reg(kA0)));
            return;
        case 0x0B:
            cpu_.setReg(kV0, testEvent(cpu_.reg(kA0)));
            return;
        case 0x0C: {
            const uint32_t id = cpu_.reg(kA0);
            if (id < events_.size() && events_[id].active) {
                events_[id].enabled = true;
                if (events_[id].pending) {
                    events_[id].pending = false;
                    events_[id].triggered = true;
                }
                cpu_.setReg(kV0, 1);
            } else {
                cpu_.setReg(kV0, 0);
            }
            return;
        }
        case 0x0D: {
            const uint32_t id = cpu_.reg(kA0);
            if (id < events_.size() && events_[id].active) {
                events_[id].enabled = false;
                cpu_.setReg(kV0, 1);
            } else {
                cpu_.setReg(kV0, 0);
            }
            return;
        }
        case 0x12:
            padInit(cpu_.reg(kA0), cpu_.reg(kA1), cpu_.reg(kA2), cpu_.reg(kA3));
            cpu_.setReg(kV0, 0);
            return;
        case 0x13:
            padStart();
            cpu_.setReg(kV0, 0);
            return;
        case 0x14:
            padStop();
            cpu_.setReg(kV0, 0);
            return;
        case 0x15:
            padInit(cpu_.reg(kA1), 0x22u, cpu_.reg(kA2), 0x22u);
            padStart();
            cpu_.setReg(kV0, 2);
            return;
        case 0x16: {
            uint32_t buttons = padButtons_;
            if (padBuf1_ != 0) {
                memory_.write8(padBuf1_ + 0, 0);
                memory_.write8(padBuf1_ + 1, 0x41);
                memory_.write8(padBuf1_ + 2, static_cast<uint8_t>(buttons & 0xFFu));
                memory_.write8(padBuf1_ + 3, static_cast<uint8_t>((buttons >> 8) & 0xFFu));
            }
            cpu_.setReg(kV0, (static_cast<uint32_t>(padButtons_ >> 8) << 0) |
                             (static_cast<uint32_t>(padButtons_ & 0xFFu) << 8));
            return;
        }
        case 0x18:
        case 0x19:
            cpu_.setReg(kV0, 0);
            return;
        case 0x20: {
            const uint32_t id = cpu_.reg(kA0);
            if (id < events_.size() && events_[id].active) {
                events_[id].triggered = false;
                events_[id].pending = false;
            }
            cpu_.setReg(kV0, 0);
            return;
        }
        case 0x32:
            cpu_.setReg(kV0, openFile(readString(cpu_.reg(kA0)), cpu_.reg(kA1)));
            return;
        case 0x33:
            cpu_.setReg(kV0, seekFile(cpu_.reg(kA0), static_cast<int32_t>(cpu_.reg(kA1)),
                                      cpu_.reg(kA2)));
            return;
        case 0x34:
            cpu_.setReg(kV0, readFile(cpu_.reg(kA0), cpu_.reg(kA1), cpu_.reg(kA2)));
            return;
        case 0x35:
            cpu_.setReg(kV0, writeFile(cpu_.reg(kA0), cpu_.reg(kA1), cpu_.reg(kA2)));
            return;
        case 0x36:
            cpu_.setReg(kV0, closeFile(cpu_.reg(kA0)));
            return;
        case 0x37:
            cpu_.setReg(kV0, 0);
            return;
        case 0x3A:
            cpu_.setReg(kV0, 0xFFFFFFFFu);
            return;
        case 0x3B:
            std::fputc(static_cast<int>(cpu_.reg(kA0) & 0xFFu), stdout);
            std::fflush(stdout);
            cpu_.setReg(kV0, cpu_.reg(kA0) & 0xFFu);
            return;
        case 0x3C:
            cpu_.setReg(kV0, 0xFFFFFFFFu);
            return;
        case 0x3D:
            std::fputc(static_cast<int>(cpu_.reg(kA0) & 0xFFu), stdout);
            std::fflush(stdout);
            cpu_.setReg(kV0, cpu_.reg(kA0) & 0xFFu);
            return;
        case 0x3E:
            cpu_.setReg(kV0, 0);
            return;
        case 0x3F:
            print(readString(cpu_.reg(kA0)) + "\n");
            cpu_.setReg(kV0, 0);
            return;
        case 0x42:
        case 0x43:
            cpu_.setReg(kV0, 0);
            return;
        case 0x54:
        case 0x55:
            cpu_.setReg(kV0, 0);
            return;
        case 0x56:
            cpu_.setReg(kV0, tableBase_);
            return;
        case 0x57:
            cpu_.setReg(kV0, tableBase_ + 0x400u);
            return;
        case 0x5B:
            cpu_.setReg(kV0, 0);
            return;
        default:
            std::cerr << "[BIOS] unimplemented B0:" << std::hex << static_cast<unsigned>(function)
                      << std::dec << " a0=0x" << std::hex << cpu_.reg(kA0) << " a1=0x"
                      << cpu_.reg(kA1) << " a2=0x" << cpu_.reg(kA2) << std::dec << "\n";
            cpu_.setReg(kV0, 0);
            return;
    }
}

void Bios::c0Call(uint8_t function) {
    switch (function) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x0A:
        case 0x0C:
        case 0x0D:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D:
            cpu_.setReg(kV0, 0);
            return;
        default:
            std::cerr << "[BIOS] unimplemented C0:" << std::hex << static_cast<unsigned>(function)
                      << std::dec << " a0=0x" << std::hex << cpu_.reg(kA0) << " a1=0x"
                      << cpu_.reg(kA1) << std::dec << "\n";
            cpu_.setReg(kV0, 0);
            return;
    }
}

}  // namespace psxreborn
