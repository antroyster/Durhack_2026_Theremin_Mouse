# The Pico SDK needs CMake 3.13 or newer.
cmake_minimum_required(VERSION 3.13)

# Default to the plain RP2040 Pico unless -DPICO_BOARD=... says otherwise (sets LED pin, flash size, etc.).
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico CACHE STRING "Board type")
endif()

# Sensor backend: HW = real sensors (default), SIM = scripted data from common/sim_data.c.
# Choose with: cmake -DSENSORS=SIM   or   cmake -DSENSORS=HW
set(SENSORS "HW" CACHE STRING "Sensor backend: HW or SIM")
# Offer the two values in cmake-gui / ccmake.
set_property(CACHE SENSORS PROPERTY STRINGS HW SIM)
# Stop at configure time on a typo instead of silently building the wrong thing.
if(NOT SENSORS STREQUAL "HW" AND NOT SENSORS STREQUAL "SIM")
    message(FATAL_ERROR "SENSORS must be HW or SIM (got '${SENSORS}')")
endif()

# Locate the SDK via PICO_SDK_PATH. Must come BEFORE project() so the ARM cross-compiler is selected.
include(pico_sdk_import.cmake)

# Name the project; ASM is needed for the SDK's boot stage 2 and startup code, CXX for SDK internals.
project(theremin_mouse C CXX ASM)

# Language standards used for our code and the SDK.
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Create the SDK library targets (pico_stdlib, tinyusb_device, ...) that we link against below.
pico_sdk_init()

# Short git hash of the checkout, captured when cmake CONFIGURES (spec B5). It goes into the status line.
# Re-run cmake after a commit to refresh it.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT)
# No git, or not a checkout: fall back to "unknown".
if(NOT GIT_RESULT EQUAL 0 OR GIT_HASH STREQUAL "")
    set(GIT_HASH "unknown")
endif()
message(STATUS "theremin_mouse: SENSORS=${SENSORS} git=${GIT_HASH}")

# Pure logic modules shared with the PC tests (no Pico SDK calls inside).
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# Our own source files, listed once so the warning flags below apply to all of them.
set(APP_SOURCES
    main.c                          # timer tick, main loop, USB callbacks
    dbg.c                           # non-blocking debug text on CDC
    usb_descriptors.c               # device, configuration, HID report and string descriptors
    ${COMMON_DIR}/cmd_match.c       # recognises the "@RUN" / "@STOP" bench commands on CDC
    ${COMMON_DIR}/debounce.c        # 5 ms time-based debounce
    ${COMMON_DIR}/mouse_report.c    # builds the 4-byte HID report, "send only on change"
    ${COMMON_DIR}/scroll.c          # joystick deadzone and scroll accumulator
    ${COMMON_DIR}/carry.c           # optical clamp-and-carry
    ${COMMON_DIR}/crc16.c           # CRC-16/CCITT-FALSE for the packets
    ${COMMON_DIR}/imu_frame.c       # packs IMU and log frames
)

# Add exactly one sensor backend; both implement sensors.h.
if(SENSORS STREQUAL "SIM")
    # Scripted sensors: the wrapper plus the script itself.
    list(APPEND APP_SOURCES sensors_sim.c ${COMMON_DIR}/sim_data.c)
else()
    # Real sensors.
    list(APPEND APP_SOURCES sensors_hw.c opt_pmw3360.c imu_mpu6050.c)
endif()

# Our firmware executable.
add_executable(theremin_mouse ${APP_SOURCES})

# Strict warnings for OUR files only (the SDK and TinyUSB sources are compiled into the same target
# and are not written to pass -Wextra).
set_source_files_properties(${APP_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# Header search path: this folder (config.h, sensors.h and tusb_config.h, which TinyUSB includes by name)
# and common/ (debounce.h, mouse_report.h, sim_data.h, protocol.h).
target_include_directories(theremin_mouse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${COMMON_DIR})

# Compile-time constants: the git hash as a C string, and which backend was chosen
# (SENSORS_SIM=1 or SENSORS_HW=1) for any code that needs to know.
target_compile_definitions(theremin_mouse PRIVATE
    CFG_GIT_HASH="${GIT_HASH}"
    SENSORS_${SENSORS}=1)

# Libraries:
#   pico_stdlib      GPIO, time, repeating timers, runtime start-up
#   pico_unique_id   the flash chip's unique ID, used as the USB serial number
#   pico_bootrom     reset_usb_boot() for the 1200-baud reboot into BOOTSEL
#   hardware_gpio    button inputs and the LED
#   hardware_adc     joystick Y axis
#   hardware_spi     optical sensor
#   hardware_i2c     IMU
#   hardware_sync    save_and_disable_interrupts() around the shared wheel total
#   tinyusb_device   TinyUSB device stack with OUR descriptors (usb_descriptors.c, tusb_config.h)
# tinyusb_board is NOT linked: it is the TinyUSB examples' board layer (its own LED/UART set-up on
# GPIO0/1), nothing in the stack needs it, and GPIO0/1 stay free for the Debug Probe.
target_link_libraries(theremin_mouse
    pico_stdlib
    pico_unique_id
    pico_bootrom
    hardware_gpio
    hardware_adc
    hardware_i2c
    hardware_spi
    hardware_sync
    tinyusb_device)

# No SDK stdio on USB: the SDK's stdio_usb brings its own CDC-only descriptors and would clash with ours.
# Debug text goes through dbg_printf() instead.
pico_enable_stdio_usb(theremin_mouse 0)

# No stdio on UART: GPIO0/GPIO1 stay free for the Debug Probe.
pico_enable_stdio_uart(theremin_mouse 0)

# Also produce theremin_mouse.uf2 (drag-and-drop format for BOOTSEL), plus .bin/.hex/.dis/.map.
pico_add_extra_outputs(theremin_mouse)
