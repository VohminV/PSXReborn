#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "psxreborn/cpu.hpp"
#include "psxreborn/memory.hpp"
#include "psxreborn/psx_exe.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

namespace {

int normalizedExitCode(int status) {
#ifdef _WIN32
    return status;
#else
    if (!WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
#endif
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    CHECK(file.good());
}

int runCommand(const std::string& command) {
    return normalizedExitCode(std::system(command.c_str()));
}

}  // namespace

TEST_CASE(end_to_end_exe_loads_and_runs) {
    const std::vector<uint8_t> payload = test::wordsToPayload({
        test::encodeI(0x0F, 0, 1, 0x8002),
        test::encodeI(0x09, 0, 2, 42),
        test::encodeI(0x2B, 1, 2, 0x100),
        test::encodeR(0, 0, 0, 0, 0x0D),
    });
    test::ExeBuildOptions options;
    options.loadAddress = 0x80010000;
    options.entryPoint = 0x80010000;
    const std::vector<uint8_t> file = test::makePsxExe(payload, options);

    const psxreborn::PsxExeResult parsed = psxreborn::parsePsxExe(file);
    CHECK(parsed);
    psxreborn::Memory memory;
    const psxreborn::PsxExeLoadResult loaded = psxreborn::loadPsxExe(*parsed.image, memory);
    CHECK(loaded.success);

    psxreborn::Cpu cpu(memory);
    cpu.resetFromExe(parsed.image->header.entryPoint, parsed.image->header.globalPointer,
                     parsed.image->header.stackBase, parsed.image->header.stackOffset);
    const psxreborn::CpuRunResult result = cpu.run(16);
    CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(psxreborn::CpuStopReason::Breakpoint));
    CHECK_EQ(result.steps, 3u);
    CHECK_EQ(cpu.reg(2), 42u);
    CHECK_EQ(test::readWord(memory, 0x80020100), 42u);
}

TEST_CASE(end_to_end_cli_inspect_and_run) {
    CHECK(!test::cliPath.empty());
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("psxreborn-e2e-" + std::to_string(unique));
    std::filesystem::create_directories(directory);

    const std::filesystem::path exePath = directory / "hello.exe";
    const std::filesystem::path outputPath = directory / "output.txt";
    const std::filesystem::path loopPath = directory / "loop.exe";
    const std::filesystem::path badPath = directory / "bad.exe";

    const std::vector<uint8_t> payload = test::wordsToPayload({
        test::encodeI(0x09, 0, 2, 42),
        test::encodeR(0, 0, 0, 0, 0x0D),
    });
    writeBinaryFile(exePath, test::makePsxExe(payload));

    const std::string cli = "\"" + test::cliPath + "\"";
    int exitCode = runCommand(cli + " inspect \"" + exePath.string() + "\" > \"" + outputPath.string() + "\" 2>&1");
    CHECK_EQ(exitCode, 0);
    std::string output = readTextFile(outputPath);
    CHECK(output.find("entry point: 0x80010000") != std::string::npos);
    CHECK(output.find("load size: 8 bytes") != std::string::npos);
    CHECK(output.find("load check: ok") != std::string::npos);

    exitCode = runCommand(cli + " run \"" + exePath.string() + "\" > \"" + outputPath.string() + "\" 2>&1");
    CHECK_EQ(exitCode, 0);
    output = readTextFile(outputPath);
    CHECK(output.find("stop: breakpoint") != std::string::npos);
    CHECK(output.find("steps: 1") != std::string::npos);
    CHECK(output.find("r2: 0x0000002a") != std::string::npos);

    const std::vector<uint8_t> loopPayload = test::wordsToPayload({
        test::encodeI(0x04, 0, 0, 0xFFFF),
        test::encodeI(0x00, 0, 0, 0),
    });
    writeBinaryFile(loopPath, test::makePsxExe(loopPayload));
    exitCode = runCommand(cli + " run \"" + loopPath.string() + "\" --max-steps 7 > \"" +
                          outputPath.string() + "\" 2>&1");
    CHECK_EQ(exitCode, 3);
    output = readTextFile(outputPath);
    CHECK(output.find("stop: step limit") != std::string::npos);
    CHECK(output.find("steps: 7") != std::string::npos);

    writeBinaryFile(badPath, std::vector<uint8_t>(0x100, 0));
    exitCode = runCommand(cli + " inspect \"" + badPath.string() + "\" > \"" + outputPath.string() + "\" 2>&1");
    CHECK_EQ(exitCode, 1);
    output = readTextFile(outputPath);
    CHECK(output.find("file too small") != std::string::npos);

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}
