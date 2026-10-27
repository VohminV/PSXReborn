#include "psxreborn/spu.hpp"

#include <algorithm>
#include <cstring>

namespace psxreborn {

namespace {

constexpr uint32_t kRegisterBytes = 0x200;
constexpr uint32_t kVoiceRegisterBytes = 0x10;

constexpr int32_t kFilterPositive[5] = {0, 60, 115, 98, 122};
constexpr int32_t kFilterNegative[5] = {0, 0, -52, -55, -60};

}  // namespace

Spu::Spu() : ram_(kRamSize, 0) {
    reset();
}

void Spu::reset() {
    std::fill(ram_.begin(), ram_.end(), uint8_t{0});
    voices_ = {};
    registers_ = {};
    transferAddress_ = 0;
    control_ = 0;
    keyOn_ = 0;
    keyOff_ = 0;
    endFlags_ = 0;
    irqAddress_ = 0;
    ramControl_ = 0;
    irqFlag_ = false;
}

bool Spu::voiceRegister(uint32_t offset, unsigned& voice, uint32_t& index) {
    if (offset >= kVoiceCount * kVoiceRegisterBytes) {
        return false;
    }
    voice = offset / kVoiceRegisterBytes;
    index = (offset % kVoiceRegisterBytes) / 2u;
    return true;
}

int16_t Spu::clamp16(int32_t value) {
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return static_cast<int16_t>(value);
}

uint16_t Spu::readVoiceRegister(unsigned voice, uint32_t index) const {
    const Voice& state = voices_[voice];
    switch (index) {
        case 0:
            return state.volumeLeft;
        case 1:
            return state.volumeRight;
        case 2:
            return state.pitch;
        case 3:
            return state.startAddress;
        case 4:
            return state.adsrLow;
        case 5:
            return state.adsrHigh;
        case 6:
            return state.envelope;
        case 7:
            return state.repeatAddress;
        default:
            return 0;
    }
}

void Spu::writeVoiceRegister(unsigned voice, uint32_t index, uint16_t value) {
    Voice& state = voices_[voice];
    switch (index) {
        case 0:
            state.volumeLeft = value;
            break;
        case 1:
            state.volumeRight = value;
            break;
        case 2:
            state.pitch = value;
            break;
        case 3:
            state.startAddress = value;
            break;
        case 4:
            state.adsrLow = value;
            break;
        case 5:
            state.adsrHigh = value;
            break;
        case 6:
            state.envelope = value;
            break;
        case 7:
            state.repeatAddress = value;
            break;
        default:
            break;
    }
}

void Spu::startVoice(unsigned voice) {
    Voice& state = voices_[voice];
    state.keyedOn = true;
    state.end = false;
    state.envelope = 0;
    state.currentAddress = state.startAddress;
    state.history1 = 0;
    state.history2 = 0;
    endFlags_ &= static_cast<uint16_t>(~(1u << (voice & 15u)));
}

void Spu::stopVoice(unsigned voice) {
    voices_[voice].keyedOn = false;
}

uint16_t Spu::status() const {
    uint16_t value = control_ & 0x3Fu;
    if ((control_ & 0x10u) != 0 || (control_ & 0x20u) != 0) {
        value |= 1u << 7;
    }
    if (irqFlag_) {
        value |= 1u << 6;
    }
    return value;
}

bool Spu::read16(uint32_t address, uint16_t& value) const {
    if (address < kBaseAddress || address >= kBaseAddress + kRegisterBytes || (address & 1u) != 0) {
        return false;
    }
    const uint32_t offset = address - kBaseAddress;
    unsigned voice = 0;
    uint32_t voiceIndex = 0;
    if (voiceRegister(offset, voice, voiceIndex)) {
        value = readVoiceRegister(voice, voiceIndex);
        return true;
    }
    const uint32_t index = offset / 2u;
    if (offset >= 0x188u && offset <= 0x18Eu) {
        value = 0;
        return true;
    }
    if (offset == 0x19Cu) {
        value = endFlags_ & 0xFFFFu;
        return true;
    }
    if (offset == 0x19Eu) {
        value = (endFlags_ >> 8) & 0xFFu;
        return true;
    }
    if (offset == 0x1AEu) {
        value = status();
        return true;
    }
    value = registers_[index];
    return true;
}

bool Spu::write16(uint32_t address, uint16_t value) {
    if (address < kBaseAddress || address >= kBaseAddress + kRegisterBytes || (address & 1u) != 0) {
        return false;
    }
    const uint32_t offset = address - kBaseAddress;
    unsigned voice = 0;
    uint32_t voiceIndex = 0;
    if (voiceRegister(offset, voice, voiceIndex)) {
        writeVoiceRegister(voice, voiceIndex, value);
        registers_[offset / 2u] = value;
        return true;
    }
    const uint32_t index = offset / 2u;
    registers_[index] = value;
    if (offset == 0x188u) {
        keyOn_ = value;
        for (unsigned indexVoice = 0; indexVoice < 16; ++indexVoice) {
            if ((value & (1u << indexVoice)) != 0) {
                startVoice(indexVoice);
            }
        }
    } else if (offset == 0x18Au) {
        for (unsigned indexVoice = 16; indexVoice < kVoiceCount; ++indexVoice) {
            if ((value & (1u << (indexVoice - 16u))) != 0) {
                startVoice(indexVoice);
            }
        }
    } else if (offset == 0x18Cu) {
        keyOff_ = value;
        for (unsigned indexVoice = 0; indexVoice < 16; ++indexVoice) {
            if ((value & (1u << indexVoice)) != 0) {
                stopVoice(indexVoice);
            }
        }
    } else if (offset == 0x18Eu) {
        for (unsigned indexVoice = 16; indexVoice < kVoiceCount; ++indexVoice) {
            if ((value & (1u << (indexVoice - 16u))) != 0) {
                stopVoice(indexVoice);
            }
        }
    } else if (offset == 0x19Cu) {
        endFlags_ = static_cast<uint16_t>(endFlags_ & 0xFF00u);
    } else if (offset == 0x19Eu) {
        endFlags_ = static_cast<uint16_t>((endFlags_ & 0x00FFu) | ((value & 0xFFu) << 8));
    } else if (offset == 0x1A4u) {
        irqAddress_ = value;
    } else if (offset == 0x1A6u) {
        transferAddress_ = static_cast<uint32_t>(value) * 8u;
    } else if (offset == 0x1A8u) {
        if (((control_ >> 4) & 3u) == 1u) {
            if (transferAddress_ + 1u < ram_.size()) {
                ram_[transferAddress_] = static_cast<uint8_t>(value & 0xFFu);
                ram_[transferAddress_ + 1u] = static_cast<uint8_t>(value >> 8);
            }
            transferAddress_ = (transferAddress_ + 2u) & (kRamSize - 1u);
        }
    } else if (offset == 0x1AAu) {
        control_ = value;
        if ((value & 0x40u) == 0) {
            irqFlag_ = false;
        }
    } else if (offset == 0x1ACu) {
        ramControl_ = value;
    }
    return true;
}

bool Spu::read32(uint32_t address, uint32_t& value) const {
    uint16_t low = 0;
    uint16_t high = 0;
    if (!read16(address, low) || !read16(address + 2u, high)) {
        return false;
    }
    value = static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 16);
    return true;
}

bool Spu::write32(uint32_t address, uint32_t value) {
    if (!write16(address, static_cast<uint16_t>(value & 0xFFFFu))) {
        return false;
    }
    return write16(address + 2u, static_cast<uint16_t>(value >> 16));
}

bool Spu::dmaWrite(uint32_t word) {
    const bool manualOrDmaWrite = ((control_ >> 4) & 3u) == 2u;
    if (!manualOrDmaWrite) {
        return false;
    }
    if (transferAddress_ + 3u >= ram_.size()) {
        return false;
    }
    ram_[transferAddress_] = static_cast<uint8_t>(word & 0xFFu);
    ram_[transferAddress_ + 1u] = static_cast<uint8_t>((word >> 8) & 0xFFu);
    ram_[transferAddress_ + 2u] = static_cast<uint8_t>((word >> 16) & 0xFFu);
    ram_[transferAddress_ + 3u] = static_cast<uint8_t>((word >> 24) & 0xFFu);
    transferAddress_ = (transferAddress_ + 4u) & (kRamSize - 1u);
    return true;
}

bool Spu::dmaRead(uint32_t& word) {
    const bool dmaReadMode = ((control_ >> 4) & 3u) == 3u;
    if (!dmaReadMode) {
        return false;
    }
    if (transferAddress_ + 3u >= ram_.size()) {
        return false;
    }
    word = static_cast<uint32_t>(ram_[transferAddress_]) |
           (static_cast<uint32_t>(ram_[transferAddress_ + 1u]) << 8) |
           (static_cast<uint32_t>(ram_[transferAddress_ + 2u]) << 16) |
           (static_cast<uint32_t>(ram_[transferAddress_ + 3u]) << 24);
    transferAddress_ = (transferAddress_ + 4u) & (kRamSize - 1u);
    return true;
}

uint8_t Spu::ram(uint32_t address) const {
    return address < ram_.size() ? ram_[address] : 0;
}

void Spu::setRam(uint32_t address, uint8_t value) {
    if (address < ram_.size()) {
        ram_[address] = value;
    }
}

uint16_t Spu::currentEnvelope(unsigned voice) const {
    return voice < kVoiceCount ? voices_[voice].envelope : 0;
}

bool Spu::voiceKeyedOn(unsigned voice) const {
    return voice < kVoiceCount && voices_[voice].keyedOn;
}

std::array<int16_t, 28> Spu::decodeBlock(const uint8_t* block, int16_t& history1, int16_t& history2) {
    std::array<int16_t, 28> output{};
    const uint32_t shift = block[0] & 0x0Fu;
    const uint32_t filter = (block[0] >> 4) & 0x07u;
    const uint32_t safeFilter = filter < 5u ? filter : 4u;
    const int32_t positive = kFilterPositive[safeFilter];
    const int32_t negative = kFilterNegative[safeFilter];
    for (uint32_t sampleIndex = 0; sampleIndex < 28; ++sampleIndex) {
        const uint8_t packed = block[2u + sampleIndex / 2u];
        int32_t nibble = (sampleIndex & 1u) == 0 ? (packed & 0x0Fu) : (packed >> 4);
        if ((nibble & 0x08u) != 0) {
            nibble -= 16;
        }
        const int32_t sample = (nibble << 12) >> shift;
        const int32_t predicted = (history1 * positive + history2 * negative + 32) >> 6;
        const int16_t decoded = clamp16(sample + predicted);
        output[sampleIndex] = decoded;
        history2 = history1;
        history1 = decoded;
    }
    return output;
}

void Spu::setFault(MemoryFault* fault, MemoryFault value) {
    if (fault != nullptr) {
        *fault = value;
    }
}

}  // namespace psxreborn
