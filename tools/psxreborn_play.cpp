#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "psxreborn/bios.hpp"
#include "psxreborn/cdrom.hpp"
#include "psxreborn/cpu.hpp"
#include "psxreborn/hardware.hpp"
#include "psxreborn/memory.hpp"
#include "psxreborn/psx_exe.hpp"

using namespace psxreborn;

namespace {
constexpr uint16_t kBtnSelect = 1u << 0;
constexpr uint16_t kBtnL3 = 1u << 1;
constexpr uint16_t kBtnR3 = 1u << 2;
constexpr uint16_t kBtnStart = 1u << 3;
constexpr uint16_t kBtnUp = 1u << 4;
constexpr uint16_t kBtnRight = 1u << 5;
constexpr uint16_t kBtnDown = 1u << 6;
constexpr uint16_t kBtnLeft = 1u << 7;
constexpr uint16_t kBtnL2 = 1u << 8;
constexpr uint16_t kBtnR2 = 1u << 9;
constexpr uint16_t kBtnL1 = 1u << 10;
constexpr uint16_t kBtnR1 = 1u << 11;
constexpr uint16_t kBtnTriangle = 1u << 12;
constexpr uint16_t kBtnCircle = 1u << 13;
constexpr uint16_t kBtnCross = 1u << 14;
constexpr uint16_t kBtnSquare = 1u << 15;
constexpr uint64_t kCyclesPerFrame = 33868800u / 60u;

uint16_t g_pad = 0xFFFFu;

void setButton(uint16_t bit, bool pressed) {
    if (pressed) {
        g_pad &= static_cast<uint16_t>(~bit);
    } else {
        g_pad |= bit;
    }
}

uint32_t color555To888(uint16_t p) {
    uint8_t r = static_cast<uint8_t>((p & 0x1Fu) << 3);
    uint8_t g = static_cast<uint8_t>(((p >> 5) & 0x1Fu) << 3);
    uint8_t b = static_cast<uint8_t>(((p >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: psxreborn_play <disc.cue|disc.iso|file.exe>\n";
        return 2;
    }

    std::string path = argv[1];

    Memory memory;
    Cpu* cpuPtr = nullptr;
    System* systemPtr = nullptr;
    Bios* biosPtr = nullptr;
    std::unique_ptr<CdImage> disc;

    // Boot disc or exe
    if (path.size() > 4 && path.substr(path.size() - 4) == ".exe") {
        std::vector<uint8_t> fileData;
        std::ifstream f(path, std::ios::binary);
        fileData.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        const PsxExeResult parsed = parsePsxExe(fileData);
        if (!parsed) {
            std::cerr << "parse error\n";
            return 1;
        }
        const PsxExeImage& image = *parsed.image;
        const PsxExeLoadResult loaded = loadPsxExe(image, memory);
        if (!loaded.success) {
            std::cerr << "load error\n";
            return 1;
        }
        cpuPtr = new Cpu(memory);
        systemPtr = new System(memory);
        biosPtr = new Bios(*cpuPtr, memory, *systemPtr);
        systemPtr->reset();
        systemPtr->bind(*cpuPtr);
        biosPtr->reset();
        biosPtr->install();
        cpuPtr->setExecutionMode(CpuExecutionMode::Hardware);
        cpuPtr->resetFromExe(image.header.entryPoint, image.header.globalPointer,
                             image.header.stackBase, image.header.stackOffset);
    } else {
        std::string error;
        std::optional<CdImage> opened = CdImage::open(path, error);
        if (!opened) {
            std::cerr << "disc error: " << error << "\n";
            return 1;
        }
        disc = std::make_unique<CdImage>(std::move(*opened));
        std::string stackLine;
        const std::optional<std::string> boot = disc->bootExecutable(stackLine);
        if (!boot) {
            std::cerr << "no boot exe\n";
            return 1;
        }
        std::vector<uint8_t> executable;
        if (!disc->readFile(*boot, executable)) {
            std::cerr << "read exe error\n";
            return 1;
        }
        const PsxExeResult parsed = parsePsxExe(executable);
        if (!parsed) {
            std::cerr << "parse error\n";
            return 1;
        }
        const PsxExeImage& image = *parsed.image;
        const PsxExeLoadResult loaded = loadPsxExe(image, memory);
        if (!loaded.success) {
            std::cerr << "load error\n";
            return 1;
        }
        cpuPtr = new Cpu(memory);
        systemPtr = new System(memory);
        biosPtr = new Bios(*cpuPtr, memory, *systemPtr);
        systemPtr->reset();
        systemPtr->bind(*cpuPtr);
        biosPtr->reset();
        biosPtr->attachDisc(disc.get());
        systemPtr->cdrom().attachDisc(disc.get());
        biosPtr->install();
        cpuPtr->setExecutionMode(CpuExecutionMode::Hardware);

        uint32_t stackBase = image.header.stackBase;
        uint32_t stackOffset = image.header.stackOffset;
        if (!stackLine.empty()) {
            // parse hex
            try {
                stackBase = std::stoul(stackLine, nullptr, 16);
                stackOffset = 0;
            } catch (...) {
            }
        }
        cpuPtr->resetFromExe(image.header.entryPoint, image.header.globalPointer, stackBase, stackOffset);
    }

    Cpu& cpu = *cpuPtr;
    System& system = *systemPtr;
    Bios& bios = *biosPtr;

    // SDL init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("PSXReborn", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          640, 480, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "window fail: " << SDL_GetError() << "\n"; return 1; }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) { std::cerr << "renderer fail: " << SDL_GetError() << "\n"; return 1; }
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 1024, 512);
    if (!texture) { std::cerr << "texture fail: " << SDL_GetError() << "\n"; return 1; }

    SDL_GameController* controller = nullptr;
    if (SDL_NumJoysticks() > 0) {
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i)) {
                controller = SDL_GameControllerOpen(i);
                break;
            }
        }
    }

    const char* dumpPath = std::getenv("PSXREBORN_PLAY_DUMP");
    uint64_t dumpSteps = 0;
    if (const char* value = std::getenv("PSXREBORN_PLAY_STEPS")) {
        dumpSteps = std::strtoull(value, nullptr, 10);
    }
    uint64_t totalSteps = 0;

    bool running = true;
    while (running) {
        const uint32_t frameStart = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                std::cerr << "SDL_QUIT received\n";
                running = false;
            } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                bool pressed = (e.type == SDL_KEYDOWN);
                switch (e.key.keysym.sym) {
                    case SDLK_UP: setButton(kBtnUp, pressed); break;
                    case SDLK_DOWN: setButton(kBtnDown, pressed); break;
                    case SDLK_LEFT: setButton(kBtnLeft, pressed); break;
                    case SDLK_RIGHT: setButton(kBtnRight, pressed); break;
                    case SDLK_RETURN: setButton(kBtnStart, pressed); break;
                    case SDLK_BACKSPACE: setButton(kBtnSelect, pressed); break;
                    case SDLK_z: setButton(kBtnCross, pressed); break;
                    case SDLK_x: setButton(kBtnCircle, pressed); break;
                    case SDLK_a: setButton(kBtnSquare, pressed); break;
                    case SDLK_s: setButton(kBtnTriangle, pressed); break;
                    case SDLK_q: setButton(kBtnL1, pressed); break;
                    case SDLK_w: setButton(kBtnL2, pressed); break;
                    case SDLK_e: setButton(kBtnR1, pressed); break;
                    case SDLK_r: setButton(kBtnR2, pressed); break;
                    case SDLK_ESCAPE: running = false; break;
                    default: break;
                }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
                bool pressed = (e.type == SDL_CONTROLLERBUTTONDOWN);
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP: setButton(kBtnUp, pressed); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: setButton(kBtnDown, pressed); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: setButton(kBtnLeft, pressed); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: setButton(kBtnRight, pressed); break;
                    case SDL_CONTROLLER_BUTTON_START: setButton(kBtnStart, pressed); break;
                    case SDL_CONTROLLER_BUTTON_BACK: setButton(kBtnSelect, pressed); break;
                    case SDL_CONTROLLER_BUTTON_A: setButton(kBtnCross, pressed); break;
                    case SDL_CONTROLLER_BUTTON_B: setButton(kBtnCircle, pressed); break;
                    case SDL_CONTROLLER_BUTTON_X: setButton(kBtnSquare, pressed); break;
                    case SDL_CONTROLLER_BUTTON_Y: setButton(kBtnTriangle, pressed); break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: setButton(kBtnL1, pressed); break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: setButton(kBtnR1, pressed); break;
                    default: break;
                }
            }
        }

        // Update BIOS pad state
        bios.setPadButtons(g_pad);

        // Run CPU
        CpuRunResult result = cpu.run(kCyclesPerFrame);
        totalSteps += result.steps;
        if (result.reason != CpuStopReason::StepLimit && result.reason != CpuStopReason::Syscall &&
            result.reason != CpuStopReason::Breakpoint) {
            std::cerr << "CPU stop: " << toString(result.reason) << " pc=" << result.pc << "\n";
            running = false;
        }

        // Render VRAM
        void* pixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(texture, nullptr, &pixels, &pitch) == 0) {
            uint32_t* dst = static_cast<uint32_t*>(pixels);
            const auto& vram = system.gpu().vram();
            for (uint32_t y = 0; y < 512; ++y) {
                for (uint32_t x = 0; x < 1024; ++x) {
                    dst[y * (pitch / 4) + x] = color555To888(vram[y * 1024 + x]);
                }
            }
            SDL_UnlockTexture(texture);
        }
        SDL_RenderClear(renderer);
        if (!system.gpu().displayDisabled()) {
            uint32_t srcX = system.gpu().displayStartX();
            uint32_t srcY = system.gpu().displayStartY();
            uint32_t srcW = system.gpu().displayWidth();
            uint32_t srcH = system.gpu().displayHeight();
            if (srcX >= Gpu::kVramWidth) {
                srcW = 0;
            } else if (srcW > Gpu::kVramWidth - srcX) {
                srcW = Gpu::kVramWidth - srcX;
            }
            if (srcY >= Gpu::kVramHeight) {
                srcH = 0;
            } else if (srcH > Gpu::kVramHeight - srcY) {
                srcH = Gpu::kVramHeight - srcY;
            }
            if (srcW != 0 && srcH != 0) {
                const SDL_Rect source{static_cast<int>(srcX), static_cast<int>(srcY),
                                      static_cast<int>(srcW), static_cast<int>(srcH)};
                SDL_RenderCopy(renderer, texture, &source, nullptr);
            }
        }
        SDL_RenderPresent(renderer);

        if (dumpPath != nullptr && dumpSteps != 0 && totalSteps >= dumpSteps) {
            std::cerr << "[PLAY] display x=" << system.gpu().displayStartX()
                      << " y=" << system.gpu().displayStartY()
                      << " w=" << system.gpu().displayWidth()
                      << " h=" << system.gpu().displayHeight()
                      << " mode=0x" << std::hex << system.gpu().displayMode() << std::dec
                      << " disabled=" << system.gpu().displayDisabled() << "\n";
            std::ofstream out(dumpPath, std::ios::binary);
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
            running = false;
        }

        const uint32_t elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < 16u) {
            SDL_Delay(16u - elapsed);
        }
    }

    if (controller) {
        SDL_GameControllerClose(controller);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
