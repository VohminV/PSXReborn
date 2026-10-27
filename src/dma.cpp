#include "psxreborn/dma.hpp"

#include "psxreborn/cdrom.hpp"
#include "psxreborn/gpu.hpp"
#include "psxreborn/hardware.hpp"
#include "psxreborn/spu.hpp"

namespace psxreborn {

Dma::Dma(Memory& memory, Interrupts& interrupts, Gpu& gpu, Spu* spu)
    : memory_(memory), interrupts_(interrupts), gpu_(gpu), spu_(spu) {}

void Dma::reset() {
    channels_ = {};
    dpcr_ = 0x07654321u;
    dicr_ = 0;
}

bool Dma::channelRegister(uint32_t address, unsigned& index, uint32_t& offset) {
    if (address < kBaseAddress || address >= kBaseAddress + kChannelCount * 0x10u) {
        return false;
    }
    const uint32_t relative = address - kBaseAddress;
    index = relative / 0x10u;
    offset = relative % 0x10u;
    return offset == 0 || offset == 4 || offset == 8;
}

bool Dma::channelEnabled(unsigned index) const {
    return (dpcr_ & (1u << (index * 4u + 3u))) != 0;
}

uint32_t Dma::dicr() const {
    uint32_t value = dicr_ & 0x7FFFFFFFu;
    const bool master = (dicr_ & (1u << 15)) != 0 || ((dicr_ & (1u << 23)) != 0 && (dicr_ & 0x7F000000u) != 0);
    if (master) {
        value |= 1u << 31;
    }
    return value;
}

bool Dma::read32(uint32_t address, uint32_t& value) {
    unsigned index = 0;
    uint32_t offset = 0;
    if (channelRegister(address, index, offset)) {
        const Channel& channel = channels_[index];
        value = offset == 0 ? channel.madr : offset == 4 ? channel.bcr : channel.chcr;
        return true;
    }
    if (address == kDpcrAddress) {
        value = dpcr_;
        return true;
    }
    if (address == kDicrAddress) {
        value = dicr();
        return true;
    }
    return false;
}

bool Dma::write32(uint32_t address, uint32_t value) {
    unsigned index = 0;
    uint32_t offset = 0;
    if (channelRegister(address, index, offset)) {
        Channel& channel = channels_[index];
        if (offset == 0) {
            channel.madr = value & 0x00FFFFFFu;
        } else if (offset == 4) {
            channel.bcr = value;
        } else {
            channel.chcr = value;
            if ((value & (1u << 24)) != 0 && channelEnabled(index)) {
                channel.chcr &= ~(1u << 28);
                start(index);
            }
        }
        return true;
    }
    if (address == kDpcrAddress) {
        dpcr_ = value;
        for (unsigned channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
            if ((channels_[channelIndex].chcr & (1u << 24)) != 0 && channelEnabled(channelIndex)) {
                start(channelIndex);
            }
        }
        return true;
    }
    if (address == kDicrAddress) {
        const bool previous = (dicr() & (1u << 31)) != 0;
        dicr_ &= ~(((value >> 24) & 0x7Fu) << 24);
        if ((value & (1u << 15)) != 0) {
            dicr_ &= ~(1u << 15);
        }
        dicr_ = (value & 0x00FF7FFFu) | (((dicr_ >> 24) & 0x7Fu) << 24);
        updateMasterIrq(previous);
        return true;
    }
    return false;
}

void Dma::updateMasterIrq(bool previous) {
    const bool current = (dicr() & (1u << 31)) != 0;
    if (current && !previous) {
        interrupts_.request(Interrupts::kDma);
    }
}

void Dma::complete(unsigned index) {
    channels_[index].chcr &= ~(1u << 24);
    if ((dicr_ & (1u << 23)) != 0 && (dicr_ & (1u << (16u + index))) != 0) {
        const bool previous = (dicr() & (1u << 31)) != 0;
        dicr_ |= 1u << (24u + index);
        updateMasterIrq(previous);
    }
}

void Dma::fail(unsigned index) {
    channels_[index].chcr &= ~(1u << 24);
    const bool previous = (dicr() & (1u << 31)) != 0;
    dicr_ |= 1u << 15;
    updateMasterIrq(previous);
}

bool Dma::transferWord(unsigned index, uint32_t& address, bool toDevice, uint32_t& word) {
    const bool decrement = (channels_[index].chcr & 2u) != 0;
    if (!toDevice) {
        if (!memory_.read32(address, word)) {
            return false;
        }
    } else if (!memory_.write32(address, word)) {
        return false;
    }
    address += decrement ? 0xFFFFFFFCu : 4u;
    return true;
}

bool Dma::runOtc(unsigned index) {
    Channel& channel = channels_[index];
    uint32_t words = channel.bcr & 0xFFFFu;
    if (words == 0) {
        words = 0x10000u;
    }
    uint32_t address = channel.madr;
    for (uint32_t wordIndex = 0; wordIndex < words; ++wordIndex) {
        const uint32_t link = wordIndex + 1u == words ? 0x00FFFFFFu : (address - 4u) & 0x00FFFFFFu;
        if (!memory_.write32(address, link)) {
            fail(index);
            return false;
        }
        address -= 4u;
    }
    channel.madr = address;
    complete(index);
    return true;
}

bool Dma::runGpu(unsigned index, uint32_t address, uint32_t words, bool linked) {
    Channel& channel = channels_[index];
    const uint32_t direction = gpu_.dmaDirection();
    const bool toGpu = direction == 2;
    if (direction == 0 || direction == 3) {
        fail(index);
        return false;
    }
    const bool decrement = (channel.chcr & 2u) != 0;
    uint32_t current = address;
    for (uint32_t wordIndex = 0; wordIndex < words; ++wordIndex) {
        uint32_t word = 0;
        if (toGpu) {
            if (!memory_.read32(current, word) || !gpu_.writeGp0(word)) {
                fail(index);
                return false;
            }
        } else {
            if (!gpu_.dmaRead(word) || !memory_.write32(current, word)) {
                fail(index);
                return false;
            }
        }
        current += decrement ? 0xFFFFFFFCu : 4u;
    }
    if (!linked) {
        channel.madr = current & 0x00FFFFFFu;
        complete(index);
    }
    return true;
}

bool Dma::runLinkedList(unsigned index) {
    Channel& channel = channels_[index];
    uint32_t address = channel.madr;
    for (uint32_t node = 0; node < 0x100000u; ++node) {
        uint32_t header = 0;
        if (!memory_.read32(address, header)) {
            fail(index);
            return false;
        }
        const uint32_t next = header & 0x00FFFFFFu;
        const uint32_t count = header >> 24;
        if (count != 0) {
            if (index == 2) {
                if (!runGpu(index, address + 4u, count, true)) {
                    return false;
                }
            } else {
                fail(index);
                return false;
            }
        }
        if (next == 0x00FFFFFFu) {
            channel.madr = next;
            complete(index);
            return true;
        }
        address = next;
    }
    fail(index);
    return false;
}

bool Dma::runNormal(unsigned index) {
    Channel& channel = channels_[index];
    const uint32_t mode = (channel.chcr >> 9) & 3u;
    uint32_t words = 0;
    if (mode == 1) {
        const uint32_t blockSize = channel.bcr & 0xFFFFu;
        const uint32_t blockCount = channel.bcr >> 16;
        words = (blockSize == 0 ? 0x10000u : blockSize) * (blockCount == 0 ? 1u : blockCount);
    } else {
        words = channel.bcr & 0xFFFFu;
        if (words == 0) {
            words = 0x10000u;
        }
    }

    if (index == 2) {
        return runGpu(index, channel.madr, words, false);
    }
    if (index == 3 && cdrom_ != nullptr) {
        if ((channel.chcr & 1u) != 0) {
            fail(index);
            return false;
        }
        uint32_t current = channel.madr;
        const bool decrement = (channel.chcr & 2u) != 0;
        for (uint32_t wordIndex = 0; wordIndex < words; ++wordIndex) {
            uint32_t value = 0;
            if (!cdrom_->dmaRead(value) || !memory_.write32(current, value)) {
                fail(index);
                return false;
            }
            current += decrement ? 0xFFFFFFFCu : 4u;
        }
        channel.madr = current & 0x00FFFFFFu;
        complete(index);
        return true;
    }
    if (index == 4 && spu_ != nullptr) {
        uint32_t current = channel.madr;
        const bool toSpu = (channel.chcr & 1u) != 0;
        const bool decrement = (channel.chcr & 2u) != 0;
        for (uint32_t wordIndex = 0; wordIndex < words; ++wordIndex) {
            uint32_t value = 0;
            if (toSpu) {
                if (!memory_.read32(current, value) || !spu_->dmaWrite(value)) {
                    fail(index);
                    return false;
                }
            } else {
                if (!spu_->dmaRead(value) || !memory_.write32(current, value)) {
                    fail(index);
                    return false;
                }
            }
            current += decrement ? 0xFFFFFFFCu : 4u;
        }
        channel.madr = current & 0x00FFFFFFu;
        complete(index);
        return true;
    }
    fail(index);
    return false;
}

void Dma::start(unsigned index) {
    if (!channelEnabled(index)) {
        return;
    }
    if (index == 6) {
        runOtc(index);
        return;
    }
    const uint32_t mode = (channels_[index].chcr >> 9) & 3u;
    if (mode == 2) {
        runLinkedList(index);
    } else {
        runNormal(index);
    }
}

}  // namespace psxreborn
