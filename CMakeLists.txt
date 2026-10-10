# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if (WIN32)
    set(USERHOME $ENV{USERPROFILE})
else ()
    set(USERHOME $ENV{HOME})
endif ()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif ()
# ====================================================================================

if (PICO_W_BUILD)
        set(PICO_BOARD pico_w CACHE STRING "Board type")
        set(PICO_RP2350 OFF CACHE BOOL "Not using RP2350")
else ()
        set(PICO_BOARD pico2_w CACHE STRING "Board type")
endif ()
#set(PICO_CYW43_SUPPORTED 1)
#set(PICO_CYW43_ARCH_THREADSAFE_BACKGROUND 1)

option(ENABLE_SERIAL "Enable USB CDC serial printf output" OFF)
if (ENABLE_SERIAL)
        set(ENABLE_SERIAL_VALUE 1)
else ()
        set(ENABLE_SERIAL_VALUE 0)
endif ()
option(ENABLE_VERBOSE "Enable Log Verbose" OFF)
if(ENABLE_VERBOSE)
    set(ENABLE_VERBOSE_VALUE 1)
else()
    set(ENABLE_VERBOSE_VALUE 0)
endif ()
option(ENABLE_BATT_LED "Blink Pico LED when DualSense battery is low (<=10%)" ON)
if (ENABLE_BATT_LED)
        set(ENABLE_BATT_LED_VALUE 1)
else ()
        set(ENABLE_BATT_LED_VALUE 0)
endif ()
option(DISABLE_SPEAKER_PROC "Disable Speaker in controller" OFF)
if (DISABLE_SPEAKER_PROC)
        set(DISABLE_SPEAKER_PROC_VALUE 1)
else ()
        set(DISABLE_SPEAKER_PROC_VALUE 0)
endif ()
option(PICO_W_BUILD "Build for Rasberry Pi Pico W, audio is missing" OFF)
if (PICO_W_BUILD)
        set(CYW43_PIO_CLOCK_DIV_VALUE 3)
        set(SYS_CLOCK_KHZ_VALUE 200000)
        set(DISABLE_SPEAKER_PROC_VALUE 1)
else ()
        set(CYW43_PIO_CLOCK_DIV_VALUE 5)
        set(SYS_CLOCK_KHZ_VALUE 320000)
endif ()



# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(ds5-bridge C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(ds5-bridge
        src/usb_descriptors.cpp
        src/main.cpp
        src/usb.cpp
        src/audio.cpp
        src/bt.cpp
        src/config.cpp
        src/cmd.cpp
        src/state_mgr.cpp
        src/oled.cpp
        src/slots.cpp
        src/remap.cpp
        src/ram_mem.c
)

if (ENABLE_BATT_LED)
        target_sources(ds5-bridge PRIVATE src/battery_led.cpp)
endif ()

# Cockos WDLResampler
add_library(wdl_resampler STATIC
        lib/WDL/WDL/resample.cpp
)
target_include_directories(wdl_resampler PUBLIC
        lib/WDL/WDL
)

# Opus
#set(OPUS_FIXED_POINT ON)
add_subdirectory(lib/opus)

# --- Core1 audio path in RAM (ported from upstream awalol Phases A.1-A.3, B.4
# queue ops, B.5). Running libopus and the resampler from flash left core1
# stalled on the XIP cache, which core0 (BTstack, TinyUSB, OLED) keeps evicting;
# the OLED Diagnostics "Drop r" counter showed ~7% of speaker chunks dropped
# because core1 couldn't encode in time. The Pico SDK linker places
# .time_critical.* in RAM (loaded from flash at boot).

# A.3: rename libopus .text/.rodata to .time_critical.* (~220 KB of RAM).
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
    COMMAND ${CMAKE_OBJCOPY} --rename-section .text=.time_critical.opus_text --rename-section .rodata=.time_critical.opus_rodata --rename-section .rodata.str1.4=.time_critical.opus_strings $<TARGET_FILE:opus>
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
    DEPENDS $<TARGET_FILE:opus>
    COMMENT "Relocating libopus to .time_critical.* sections"
    VERBATIM
)
add_custom_target(opus_ram_relocate ALL DEPENDS ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp)
add_dependencies(ds5-bridge opus_ram_relocate)

# A.2: the two WDL_Resampler methods called every audio frame.
target_compile_options(wdl_resampler PRIVATE -ffunction-sections)
add_custom_command(TARGET wdl_resampler POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text._ZN13WDL_Resampler11ResampleOutEPfiii=.time_critical.WDL_ResampleOut
        --rename-section .text._ZN13WDL_Resampler15ResamplePrepareEiiPPf=.time_critical.WDL_ResamplePrepare
        $<TARGET_FILE:wdl_resampler>
    COMMENT "Relocating WDL_Resampler hot-path methods to .time_critical.*"
)

# Rename hot .text.<fn> sections of an SDK object (located by path suffix) at
# PRE_LINK; see cmake/relocate_to_ram.cmake.
function(relocate_to_ram _suffix _renames _comment)
    add_custom_command(TARGET ds5-bridge PRE_LINK
        COMMAND ${CMAKE_COMMAND}
            "-DOBJROOT=${CMAKE_BINARY_DIR}/CMakeFiles/ds5-bridge.dir"
            "-DOBJCOPY=${CMAKE_OBJCOPY}"
            "-DSUFFIX=${_suffix}"
            "-DRENAMES=${_renames}"
            -P "${CMAKE_SOURCE_DIR}/cmake/relocate_to_ram.cmake"
        COMMENT "${_comment}"
        VERBATIM)
endfunction()

# B.4 (queue part): queue add/remove back the inter-core audio FIFOs.
relocate_to_ram(src/common/pico_util/queue.c.o
    ".text.queue_try_add=.time_critical.queue_try_add|.text.queue_try_remove=.time_critical.queue_try_remove"
    "pico_util queue add/remove -> RAM")

# B.5: RAM memcpy/memset/memmove (newlib's live in flash; libopus calls them
# every frame). Must not be lowered back into calls to themselves.
set_source_files_properties(src/ram_mem.c PROPERTIES COMPILE_OPTIONS
        "-fno-builtin;-fno-tree-loop-distribute-patterns")

target_compile_options(wdl_resampler PRIVATE -fsigned-char)
target_compile_options(ds5-bridge PRIVATE -fsigned-char)
target_compile_definitions(wdl_resampler PRIVATE
        WDL_RESAMPLE_TYPE=float
)
target_compile_definitions(ds5-bridge PRIVATE
        CYW43_LWIP=0
        ENABLE_SERIAL=${ENABLE_SERIAL_VALUE}
        PICO_FLASH_ASSUME_CORE1_SAFE=1
        WDL_RESAMPLE_TYPE=float
        CYW43_PIO_CLOCK_DIV_INT=${CYW43_PIO_CLOCK_DIV_VALUE}
        PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
        PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0
        PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
        ENABLE_VERBOSE=${ENABLE_VERBOSE_VALUE}
        ENABLE_BATT_LED=${ENABLE_BATT_LED_VALUE}
        SYS_CLOCK_KHZ=${SYS_CLOCK_KHZ_VALUE}
        DISABLE_SPEAKER_PROC=${DISABLE_SPEAKER_PROC_VALUE}
)

option(ENABLE_WAKE_HID "Wake the Windows host on PS-button press" OFF)
if(ENABLE_WAKE_HID)
    target_compile_definitions(ds5-bridge PRIVATE
            ENABLE_WAKE_HID
    )
    target_sources(ds5-bridge PRIVATE
            src/wake.cpp
    )
endif()

option(WAKE_DEBUG "Print wake-FSM transitions to UART (developer use)" OFF)
if(WAKE_DEBUG)
    target_compile_definitions(ds5-bridge PRIVATE
            WAKE_DEBUG
    )
endif()
set(VERSION "dev" CACHE STRING "Program version string")

# Expose VERSION to firmware C++ as FIRMWARE_VERSION so src/oled.cpp's
# Status header always reflects the release tag (set by release.yml via
# -DVERSION="$FIRMWARE_VERSION"). Local builds without -DVERSION show
# "dev" — visible signal that you're not on a tagged release.
target_compile_definitions(ds5-bridge PRIVATE
        FIRMWARE_VERSION="${VERSION}"
)

set_target_properties(ds5-bridge PROPERTIES OUTPUT_NAME "ds5-bridge-oled")
pico_set_program_name(ds5-bridge "ds5-bridge-oled")
pico_set_program_version(ds5-bridge "${VERSION}")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(ds5-bridge 1)
pico_enable_stdio_usb(ds5-bridge ${ENABLE_SERIAL_VALUE})

# Add the standard include files to the build
#target_include_directories(ds5-bridge PRIVATE
#        ${CMAKE_CURRENT_LIST_DIR}
#)

target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add any user requested libraries
target_link_libraries(ds5-bridge
        pico_stdlib
        pico_multicore
        hardware_watchdog
        hardware_timer
        hardware_flash
        hardware_spi
        hardware_adc
        pico_btstack_classic
#        pico_cyw43_arch_threadsafe_background
        pico_cyw43_arch_poll
        pico_btstack_cyw43
        tinyusb_device
        tinyusb_board
        wdl_resampler
        opus
)

pico_add_extra_outputs(ds5-bridge)

