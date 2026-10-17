# settime -- set the PicoCalc's shared clock (RP2350 always-on timer) from
# NTP over WiFi, then return to the shell. Needs a Pico 2 W.
#
# Build (Pico SDK 2.x, with its lib/cyw43-driver and lib/lwip submodules):
#   git submodule update --init
#   cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
#   cmake --build build          ->  build/settime.uf2
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(picocalc-settime C CXX ASM)
pico_sdk_init()

set(DRV modules/picocalc-text-starter/drivers)
add_executable(settime
    main.c
    ${DRV}/clib.c ${DRV}/fat32.c ${DRV}/font-8x10.c ${DRV}/font-5x10.c
    ${DRV}/lcd.c ${DRV}/sdcard.c ${DRV}/southbridge.c
)
target_include_directories(settime PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..                      # picocalc_clock.h
    ${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-text-starter)
pico_set_program_name(settime "settime")
pico_enable_stdio_uart(settime 0)
pico_enable_stdio_usb(settime 0)
target_link_libraries(settime pico_stdlib hardware_gpio hardware_i2c hardware_spi
    hardware_pio hardware_clocks hardware_watchdog
    pico_cyw43_arch_lwip_poll pico_lwip_sntp pico_aon_timer)
pico_add_extra_outputs(settime)
