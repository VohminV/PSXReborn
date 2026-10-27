cmake_minimum_required(VERSION 3.20)
project(PSXReborn VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PSXREBORN_BUILD_TESTS "Build PSXReborn tests" ON)
option(PSXREBORN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

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
    src/cpu.cpp
    src/dma.cpp
    src/gpu.cpp
    src/gte.cpp
    src/hardware.cpp
    src/memory.cpp
    src/mips.cpp
    src/psx_exe.cpp
    src/spu.cpp
)
target_include_directories(psxreborn_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
psxreborn_enable_warnings(psxreborn_core)

add_executable(psxreborn tools/psxreborn.cpp)
target_link_libraries(psxreborn PRIVATE psxreborn_core)
psxreborn_enable_warnings(psxreborn)

if(PSXREBORN_BUILD_TESTS)
    enable_testing()
    add_executable(psxreborn_tests
        tests/test_cop0.cpp
        tests/test_cpu.cpp
        tests/test_end_to_end.cpp
        tests/test_gte.cpp
        tests/test_hardware.cpp
        tests/test_main.cpp
        tests/test_memory.cpp
        tests/test_mips.cpp
        tests/test_psx_exe.cpp
    )
    target_link_libraries(psxreborn_tests PRIVATE psxreborn_core)
    psxreborn_enable_warnings(psxreborn_tests)
    add_test(NAME psxreborn_tests COMMAND psxreborn_tests $<TARGET_FILE:psxreborn>)
    add_test(NAME psxreborn_help COMMAND psxreborn --help)
endif()
