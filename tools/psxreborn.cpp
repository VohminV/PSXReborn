#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "psxreborn/bios.hpp"
#include "psxreborn/cdrom.hpp"
#include "psxreborn/cpu.hpp"
#include "psxreborn/hardware.hpp"
#include "psxreborn/memory.hpp"
#include "psxreborn/psx_exe.hpp"

using namespace psxreborn;

namespace {

constexpr uint64_t kDefaultMaxSteps = 1000000;

std::string hex32(uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

void printUsage(std::ostream& out) {
    out << "usage:\n"
        << "  psxreborn inspect <file.exe>\n"
        << "  psxreborn run <file.exe> [--max-steps <count>]\n"
        << "  psxreborn boot <disc.cue|disc.iso> [--max-steps <count>]\n";
}

bool readFile(const std::string& path, std::vector<uint8_t>& data, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open file";
        return false;
    }
    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (!file.eof() && file.fail()) {
        error = "cannot read file";
        return false;
    }
    return true;
}

void printHeader(const std::string& path, const PsxExeHeader& header) {
    std::cout << "file: " << path << "\n";
    std::cout << "entry point: " << hex32(header.entryPoint) << "\n";
    std::cout << "global pointer: " << hex32(header.globalPointer) << "\n";
    std::cout << "load address: " << hex32(header.loadAddress) << "\n";
    std::cout << "load size: " << header.loadSize << " bytes\n";
    std::cout << "data address: " << hex32(header.dataAddress) << "\n";
    std::cout << "data size: " << header.dataSize << " bytes\n";
    std::cout << "bss address: " << hex32(header.bssAddress) << "\n";
    std::cout << "bss size: " << header.bssSize << " bytes\n";
    std::cout << "stack base: " << hex32(header.stackBase) << "\n";
    std::cout << "stack offset: " << hex32(header.stackOffset) << "\n";
    std::cout << "region marker: " << header.regionMarker << "\n";
}

int inspectFile(const std::string& path) {
    std::vector<uint8_t> fileData;
    std::string error;
    if (!readFile(path, fileData, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    const PsxExeResult parsed = parsePsxExe(fileData);
    if (!parsed) {
        std::cerr << "error: " << parsed.message << " (" << toString(parsed.error) << ")\n";
        return 1;
    }

    const PsxExeImage& image = *parsed.image;
    printHeader(path, image.header);

    Memory memory;
    const PsxExeLoadResult loaded = loadPsxExe(image, memory);
    if (!loaded.success) {
        std::cerr << "error: " << loaded.message << "\n";
        return 1;
    }
    std::cout << "load check: ok\n";
    return 0;
}

std::optional<uint64_t> parseStepCount(const std::string& value) {
    try {
        size_t consumed = 0;
        const uint64_t result = std::stoull(value, &consumed, 0);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

int runFile(const std::string& path, uint64_t maxSteps) {
    std::vector<uint8_t> fileData;
    std::string error;
    if (!readFile(path, fileData, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    const PsxExeResult parsed = parsePsxExe(fileData);
    if (!parsed) {
        std::cerr << "error: " << parsed.message << " (" << toString(parsed.error) << ")\n";
        return 1;
    }

    Memory memory;
    const PsxExeImage& image = *parsed.image;
    const PsxExeLoadResult loaded = loadPsxExe(image, memory);
    if (!loaded.success) {
        std::cerr << "error: " << loaded.message << "\n";
        return 1;
    }

    Cpu cpu(memory);
    cpu.resetFromExe(image.header.entryPoint, image.header.globalPointer,
                     image.header.stackBase, image.header.stackOffset);
    const CpuRunResult result = cpu.run(maxSteps);

    std::cout << "stop: " << toString(result.reason) << "\n";
    std::cout << "steps: " << result.steps << "\n";
    std::cout << "pc: " << hex32(result.pc) << "\n";
    if (result.reason == CpuStopReason::UnsupportedInstruction) {
        std::cout << "instruction: " << hex32(result.instruction) << "\n";
    }
    if (result.reason == CpuStopReason::AddressError || result.reason == CpuStopReason::BusError ||
        result.reason == CpuStopReason::UnsupportedMemory) {
        std::cout << "address: " << hex32(result.address) << "\n";
    }

    for (uint8_t index = 1; index < 32; ++index) {
        const uint32_t value = cpu.reg(index);
        if (value != 0) {
            std::cout << "r" << static_cast<unsigned>(index) << ": " << hex32(value) << "\n";
        }
    }
    std::cout << "hi: " << hex32(cpu.hi()) << "\n";
    std::cout << "lo: " << hex32(cpu.lo()) << "\n";

    switch (result.reason) {
        case CpuStopReason::Breakpoint:
        case CpuStopReason::Syscall:
            return 0;
        case CpuStopReason::StepLimit:
            return 3;
        default:
            return 2;
    }
}


std::optional<uint32_t> parseHex32(const std::string& value) {
    try {
        size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed, 16);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(parsed);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

int bootImage(const std::string& path, uint64_t maxSteps) {
    std::string error;
    std::optional<CdImage> disc = CdImage::open(path, error);
    if (!disc) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    std::string stackLine;
    const std::optional<std::string> boot = disc->bootExecutable(stackLine);
    if (!boot) {
        std::cerr << "error: disc has no SYSTEM.CNF boot entry\n";
        return 1;
    }

    std::vector<uint8_t> executable;
    if (!disc->readFile(*boot, executable)) {
        std::cerr << "error: cannot read boot executable " << *boot << "\n";
        return 1;
    }

    const PsxExeResult parsed = parsePsxExe(executable);
    if (!parsed) {
        std::cerr << "error: " << parsed.message << " (" << toString(parsed.error) << ")\n";
        return 1;
    }
    const PsxExeImage& image = *parsed.image;

    Memory memory;
    const PsxExeLoadResult loaded = loadPsxExe(image, memory);
    if (!loaded.success) {
        std::cerr << "error: " << loaded.message << "\n";
        return 1;
    }

    std::cout << "boot: " << *boot << "\n";
    std::cout << "entry: " << hex32(image.header.entryPoint) << "\n";

    Cpu cpu(memory);
    System system(memory);
    Bios bios(cpu, memory, system);
    system.reset();
    system.bind(cpu);
    bios.reset();
    bios.attachDisc(&*disc);
    system.cdrom().attachDisc(&*disc);
    bios.install();
    cpu.setExecutionMode(CpuExecutionMode::Hardware);

    uint32_t stackBase = image.header.stackBase;
    uint32_t stackOffset = image.header.stackOffset;
    if (!stackLine.empty()) {
        const std::optional<uint32_t> parsedStack = parseHex32(stackLine);
        if (parsedStack) {
            stackBase = *parsedStack;
            stackOffset = 0;
        }
    }
    cpu.resetFromExe(image.header.entryPoint, image.header.globalPointer, stackBase, stackOffset);

    const CpuRunResult result = cpu.run(maxSteps);
    if (const char* vramPath = std::getenv("PSXREBORN_VRAM")) {
        std::ofstream out(vramPath, std::ios::binary);
        if (out) {
            const auto& vram = system.gpu().vram();
            out << "P6\n" << Gpu::kVramWidth << " " << Gpu::kVramHeight << "\n255\n";
            for (uint16_t pixel : vram) {
                const uint8_t r = static_cast<uint8_t>((pixel & 0x1Fu) << 3);
                const uint8_t g = static_cast<uint8_t>(((pixel >> 5u) & 0x1Fu) << 3);
                const uint8_t b = static_cast<uint8_t>(((pixel >> 10u) & 0x1Fu) << 3);
                const char rgb[3] = {static_cast<char>(r), static_cast<char>(g),
                                     static_cast<char>(b)};
                out.write(rgb, 3);
            }
        }
    }

    std::cout << "stop: " << toString(result.reason) << "\n";
    std::cout << "steps: " << result.steps << "\n";
    std::cout << "pc: " << hex32(result.pc) << "\n";
    if (result.reason == CpuStopReason::UnsupportedInstruction) {
        std::cout << "instruction: " << hex32(result.instruction) << "\n";
    }
    if (result.reason == CpuStopReason::AddressError || result.reason == CpuStopReason::BusError ||
        result.reason == CpuStopReason::UnsupportedMemory) {
        std::cout << "address: " << hex32(result.address) << "\n";
    }
    return result.reason == CpuStopReason::StepLimit ? 3 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(std::cerr);
        return 2;
    }

    const std::string_view command = argv[1];
    if (command == "--help" || command == "-h" || command == "help") {
        printUsage(std::cout);
        return 0;
    }

    if (command == "inspect") {
        if (argc != 3) {
            printUsage(std::cerr);
            return 2;
        }
        return inspectFile(argv[2]);
    }

    if (command == "boot") {
        if (argc != 3 && argc != 5) {
            printUsage(std::cerr);
            return 2;
        }
        uint64_t maxSteps = kDefaultMaxSteps;
        if (argc == 5) {
            if (std::string_view(argv[3]) != "--max-steps") {
                printUsage(std::cerr);
                return 2;
            }
            const std::optional<uint64_t> parsed = parseStepCount(argv[4]);
            if (!parsed) {
                std::cerr << "error: invalid --max-steps value\n";
                return 2;
            }
            maxSteps = *parsed;
        }
        return bootImage(argv[2], maxSteps);
    }

    if (command == "run") {
        if (argc != 3 && argc != 5) {
            printUsage(std::cerr);
            return 2;
        }
        uint64_t maxSteps = kDefaultMaxSteps;
        if (argc == 5) {
            if (std::string_view(argv[3]) != "--max-steps") {
                printUsage(std::cerr);
                return 2;
            }
            const std::optional<uint64_t> parsed = parseStepCount(argv[4]);
            if (!parsed) {
                std::cerr << "error: invalid --max-steps value\n";
                return 2;
            }
            maxSteps = *parsed;
        }
        return runFile(argv[2], maxSteps);
    }

    printUsage(std::cerr);
    return 2;
}
