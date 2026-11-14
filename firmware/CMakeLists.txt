# The Pico SDK needs CMake 3.13 or newer.
cmake_minimum_required(VERSION 3.13)

# Default to the plain RP2040 Pico unless -DPICO_BOARD=... says otherwise (sets LED pin, flash size, etc.).
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico CACHE STRING "Board type")
endif()

# Locate the SDK via PICO_SDK_PATH. Must come BEFORE project() so the ARM cross-compiler is selected.
include(pico_sdk_import.cmake)

# Name the project; ASM is needed for the SDK's boot stage 2 and startup code, CXX for SDK internals.
project(theremin_mouse C CXX ASM)

# Language standards used for our code and the SDK.
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Create the SDK library targets (pico_stdlib, tinyusb, ...) that we link against below.
pico_sdk_init()

# Our firmware executable, built from a single source file for now.
add_executable(theremin_mouse main.c)

# pico_stdlib pulls in GPIO, time and stdio (printf) support.
target_link_libraries(theremin_mouse pico_stdlib)

# printf goes to USB CDC serial: one cable only. This also links TinyUSB and the 1200-baud BOOTSEL reset.
pico_enable_stdio_usb(theremin_mouse 1)

# No stdio on UART: GPIO0/GPIO1 stay free for the Debug Probe.
pico_enable_stdio_uart(theremin_mouse 0)

# Also produce theremin_mouse.uf2 (drag-and-drop format for BOOTSEL), plus .bin/.hex/.dis/.map.
pico_add_extra_outputs(theremin_mouse)
