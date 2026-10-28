# btpair -- pairs a Bluetooth keyboard with the PicoCalc (Pico 2 W)
#
# Build (Pico SDK 2.x):
#   git submodule update --init --recursive
#   cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
#   cmake --build build          ->  build/btpair.uf2
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(btpair C CXX ASM)
pico_sdk_init()

# the Bluetooth keyboard code, one folder up
include(${CMAKE_CURRENT_LIST_DIR}/../bluetooth.cmake)

# Blair Leduc's drivers and shell header, submodules of this program
set(TS ${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-text-starter)
set(DRV ${TS}/drivers)
add_executable(btpair
    btpair.c
    ${DRV}/fat32.c ${DRV}/sdcard.c ${DRV}/font-5x10.c ${DRV}/font-8x10.c
    ${DRV}/keyboard.c ${DRV}/lcd.c ${DRV}/southbridge.c
)
target_include_directories(btpair PRIVATE
    ${TS}                                   # "drivers/lcd.h" and so on
    ${DRV}
    ${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-shell/include)
# F5 saves the Bluetooth log beside the screen, for reports (256 KB of RAM)
target_compile_definitions(btpair PRIVATE BTKBD_LOG)
pico_set_program_name(btpair "btpair")
pico_enable_stdio_uart(btpair 0)
pico_enable_stdio_usb(btpair 0)
target_link_libraries(btpair picocalc_btkbd
    pico_stdlib hardware_gpio hardware_i2c hardware_spi hardware_pio hardware_watchdog hardware_powman)
pico_add_extra_outputs(btpair)
