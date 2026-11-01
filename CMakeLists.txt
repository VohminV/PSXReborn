cmake_minimum_required(VERSION 3.20)
project(PSXReborn VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PSXREBORN_BUILD_TESTS "Build PSXReborn tests" ON)
option(PSXREBORN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PSXREBORN_BUILD_SHADERS "Compile GLSL shaders and embed SPIR-V at build time" ON)
option(PSXREBORN_ENABLE_VULKAN "Enable Vulkan shader-module support when Vulkan is available" ON)

function(psxreborn_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(PSXREBORN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(PSXREBORN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(psxreborn_core STATIC
    src/bios.cpp
    src/cdrom.cpp
    src/config.cpp
    src/cpu.cpp
    src/dma.cpp
    src/memcard.cpp
    src/menu.cpp
    src/gpu.cpp
    src/gte.cpp
    src/hardware.cpp
    src/input_trace.cpp
    src/jit.cpp
    src/memory.cpp
    src/mips.cpp
    src/overlay.cpp
    src/pad.cpp
    src/ps1_pad_device.cpp
    src/psx_exe.cpp
    src/sio0.cpp
    src/spu.cpp
    src/vulkan_shader.cpp
)
target_include_directories(psxreborn_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
psxreborn_enable_warnings(psxreborn_core)

set(PSXREBORN_SHADER_SOURCES
    shaders/present.vert
    shaders/present.frag
    shaders/easu.comp
    shaders/rcas.comp
)
set(PSXREBORN_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(PSXREBORN_GENERATED_SHADER_HEADER ${PSXREBORN_GENERATED_DIR}/spirv_data.h)
set(PSXREBORN_SPIRV_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/generated)

find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslangValidator.exe)
if(PSXREBORN_BUILD_SHADERS AND Python3_Interpreter_FOUND AND GLSLANG_VALIDATOR)
    set(PSXREBORN_SPV_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    set(PSXREBORN_SPV_OUTPUTS
        ${PSXREBORN_SPV_DIR}/present.vert.spv
        ${PSXREBORN_SPV_DIR}/present.frag.spv
        ${PSXREBORN_SPV_DIR}/easu.comp.spv
        ${PSXREBORN_SPV_DIR}/rcas.comp.spv
    )
    add_custom_command(
        OUTPUT ${PSXREBORN_SPV_OUTPUTS} ${PSXREBORN_GENERATED_SHADER_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PSXREBORN_SPV_DIR} ${PSXREBORN_GENERATED_DIR}
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.1
                -o ${PSXREBORN_SPV_DIR}/present.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/present.vert
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.1
                -o ${PSXREBORN_SPV_DIR}/present.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/present.frag
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.1
                -I${CMAKE_CURRENT_SOURCE_DIR}/third_party/fsr1
                -o ${PSXREBORN_SPV_DIR}/easu.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/easu.comp
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.1
                -I${CMAKE_CURRENT_SOURCE_DIR}/third_party/fsr1
                -o ${PSXREBORN_SPV_DIR}/rcas.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/rcas.comp
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_spirv.py
                --spv-dir ${PSXREBORN_SPV_DIR}
                --output ${PSXREBORN_GENERATED_SHADER_HEADER}
        DEPENDS ${PSXREBORN_SHADER_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_spirv.py
        COMMENT "Compiling GLSL shaders and embedding SPIR-V"
        VERBATIM
    )
    add_custom_target(psxreborn_shaders DEPENDS ${PSXREBORN_SPV_OUTPUTS} ${PSXREBORN_GENERATED_SHADER_HEADER})
    add_dependencies(psxreborn_core psxreborn_shaders)
    set(PSXREBORN_SPIRV_INCLUDE_DIR ${PSXREBORN_GENERATED_DIR})
    set_source_files_properties(${PSXREBORN_GENERATED_SHADER_HEADER} PROPERTIES GENERATED TRUE)
    target_sources(psxreborn_core PRIVATE ${PSXREBORN_GENERATED_SHADER_HEADER})
endif()
target_include_directories(psxreborn_core PRIVATE ${PSXREBORN_SPIRV_INCLUDE_DIR})

find_package(Vulkan QUIET)
if(PSXREBORN_ENABLE_VULKAN AND Vulkan_FOUND)
    target_compile_definitions(psxreborn_core PUBLIC PSXREBORN_HAS_VULKAN=1)
    target_link_libraries(psxreborn_core PUBLIC Vulkan::Vulkan)
endif()

add_executable(psxreborn tools/psxreborn.cpp)
target_link_libraries(psxreborn PRIVATE psxreborn_core)
psxreborn_enable_warnings(psxreborn)

if(PSXREBORN_BUILD_TESTS)
    enable_testing()
    add_executable(psxreborn_tests
        tests/test_bios.cpp
        tests/test_config.cpp
        tests/test_cop0.cpp
        tests/test_cpu.cpp
        tests/test_cdrom.cpp
        tests/test_end_to_end.cpp
        tests/test_gte.cpp
        tests/test_hardware.cpp
        tests/test_jit.cpp
        tests/test_main.cpp
        tests/test_memcard.cpp
        tests/test_menu.cpp
        tests/test_memory.cpp
        tests/test_mips.cpp
        tests/test_pad.cpp
        tests/test_psx_exe.cpp
        tests/test_sio0.cpp
        tests/test_vulkan_shader.cpp
    )
    target_link_libraries(psxreborn_tests PRIVATE psxreborn_core)
    psxreborn_enable_warnings(psxreborn_tests)
    add_test(NAME psxreborn_tests COMMAND psxreborn_tests $<TARGET_FILE:psxreborn>)
    add_test(NAME psxreborn_help COMMAND psxreborn --help)

    # Standalone PadSource -> SIO0 latency harness (no arguments needed).
    add_executable(psxreborn_input_latency tools/input_latency.cpp)
    target_link_libraries(psxreborn_input_latency PRIVATE psxreborn_core)
    psxreborn_enable_warnings(psxreborn_input_latency)
endif()

find_package(SDL2 QUIET)
if(SDL2_FOUND)
    add_executable(psxreborn_play tools/psxreborn_play.cpp tools/xinput_backend.cpp)
    if(PSXREBORN_ENABLE_VULKAN AND Vulkan_FOUND)
        target_sources(psxreborn_play PRIVATE src/vulkan_presenter.cpp)
    endif()
    target_link_libraries(psxreborn_play PRIVATE psxreborn_core SDL2::SDL2)
    psxreborn_enable_warnings(psxreborn_play)
endif()
