# ping -- pings a host from the PicoCalc over Wi-Fi: an example of
# picocalc-extras' net/ (Pico 2 W).
#
# Build (Pico SDK 2.x):
#   git submodule update --init --recursive
#   cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
#   cmake --build build          ->  build/ping.uf2
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(ping C CXX ASM)
pico_sdk_init()

set(EXTRAS ${CMAKE_CURRENT_LIST_DIR}/../..)
# Blair Leduc's drivers and shell, submodules of this example
set(TS ${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-text-starter)
set(SHELL ${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-shell)
set(DRV ${TS}/drivers)

# the pieces from picocalc-extras: the card, then the network
include(${EXTRAS}/fatfs/fatfs.cmake)
picocalc_fatfs(${TS})
include(${EXTRAS}/net/net.cmake)
picocalc_net(${SHELL})

add_executable(ping
    main.c
    ${DRV}/picocalc.c ${DRV}/display.c ${DRV}/clib.c ${DRV}/audio.c ${DRV}/onboard_led.c
    ${DRV}/font-5x10.c ${DRV}/font-8x10.c
    ${DRV}/keyboard.c ${DRV}/lcd.c ${DRV}/southbridge.c
)
pico_generate_pio_header(ping ${DRV}/audio.pio)
target_include_directories(ping PRIVATE ${TS} ${DRV} ${SHELL}/include)
pico_set_program_name(ping "ping")
pico_set_program_version(ping "1.0")
pico_enable_stdio_uart(ping 0)
pico_enable_stdio_usb(ping 0)
target_compile_definitions(ping PRIVATE CYW43_HOST_NAME="picocalc-ping")
target_link_libraries(ping picocalc_net picocalc_fs
    pico_stdlib pico_status_led hardware_gpio hardware_i2c hardware_spi hardware_pio
    hardware_clocks hardware_powman)
pico_add_extra_outputs(ping)
