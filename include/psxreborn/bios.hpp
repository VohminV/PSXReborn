#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "psxreborn/memory.hpp"

namespace psxreborn {

class CdImage;
class Cpu;
class System;

class Bios {
public:
    Bios(Cpu& cpu, Memory& memory, System& system);
    ~Bios();

    void reset();
    void install();
    void attachDisc(const CdImage* disc) { disc_ = disc; }
    void setPadButtons(uint16_t buttons) { padButtons_ = buttons; }
    uint16_t padButtons() const { return padButtons_; }

    void handleVBlank();
    void handleCdromInterrupt(uint8_t interrupt);

private:
    struct Event {
        uint32_t classId = 0;
        uint32_t specId = 0;
        uint32_t mode = 0;
        uint32_t handler = 0;
        bool enabled = false;
        bool triggered = false;
        bool pending = false;
        bool active = true;
    };

    struct HeapBlock {
        uint32_t address = 0;
        uint32_t size = 0;
        bool free = true;
    };

    struct OpenFile {
        bool used = false;
        std::vector<uint8_t> data;
        size_t position = 0;
    };

    bool onInstruction(uint32_t pc);
    bool onSyscall();
    void dispatch(uint8_t vector, uint8_t function);

    void a0Call(uint8_t function);
    void b0Call(uint8_t function);
    void c0Call(uint8_t function);

    uint32_t read32(uint32_t address) const;
    uint16_t read16(uint32_t address) const;
    uint8_t read8(uint32_t address) const;
    void write32(uint32_t address, uint32_t value);
    void write8(uint32_t address, uint8_t value);
    std::string readString(uint32_t address, size_t limit = 4096) const;
    void writeString(uint32_t address, const std::string& value);

    uint32_t openFile(const std::string& path, uint32_t mode);
    uint32_t readFile(uint32_t fd, uint32_t address, uint32_t size);
    uint32_t writeFile(uint32_t fd, uint32_t address, uint32_t size);
    uint32_t seekFile(uint32_t fd, int32_t offset, uint32_t whence);
    uint32_t closeFile(uint32_t fd);

    uint32_t openEvent(uint32_t classId, uint32_t specId, uint32_t mode, uint32_t handler);
    uint32_t closeEvent(uint32_t id);
    uint32_t waitEvent(uint32_t id);
    uint32_t testEvent(uint32_t id);
    void deliverEvent(uint32_t id);
    void deliverEventClass(uint32_t classId, uint32_t specId);

    void initHeap(uint32_t address, uint32_t size);
    uint32_t heapAlloc(uint32_t size);
    void heapFree(uint32_t address);
    uint32_t heapRealloc(uint32_t address, uint32_t size);
    void coalesce();

    void padInit(uint32_t buf1, uint32_t siz1, uint32_t buf2, uint32_t siz2);
    void padStart();
    void padStop();
    void padUpdate();

    uint32_t fetchArgument(unsigned index, unsigned firstRegister) const;
    std::string formatString(uint32_t formatAddress, unsigned firstRegister) const;
    void print(const std::string& text);

    Cpu& cpu_;
    Memory& memory_;
    System& system_;
    const CdImage* disc_ = nullptr;

    std::vector<Event> events_;
    std::vector<HeapBlock> heap_;
    std::vector<OpenFile> files_;
    uint32_t heapStart_ = 0;
    uint32_t heapSize_ = 0;
    uint32_t tableBase_ = 0;
    bool installed_ = false;
    uint32_t randState_ = 1;

    uint32_t padBuf1_ = 0;
    uint32_t padBuf2_ = 0;
    bool padEnabled_ = false;
    uint16_t padButtons_ = 0xFFFFu;
};

}  // namespace psxreborn
