# showpic -- an example of the picker and picture decoders (see HOWTO.md).
#
# Build (Pico SDK 2.x):
#   git submodule update --init --recursive
#   cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
#   cmake --build build          ->  build/showpic.uf2
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(showpic C CXX ASM)
pico_sdk_init()

# the two pieces from picocalc-extras: one include() each
set(EXTRAS ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${EXTRAS}/picker/picker.cmake)
include(${EXTRAS}/images/images.cmake)

# Blair Leduc's drivers and shell header, submodules of this example
set(TS ${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-text-starter)
set(DRV ${TS}/drivers)
add_executable(showpic
    main.c
    ${DRV}/fat32.c ${DRV}/sdcard.c ${DRV}/font-5x10.c ${DRV}/font-8x10.c
    ${DRV}/keyboard.c ${DRV}/lcd.c ${DRV}/southbridge.c
)
target_include_directories(showpic PRIVATE
    ${TS}                                   # "drivers/lcd.h" and so on
    ${DRV}
    ${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-shell/include)
pico_set_program_name(showpic "showpic")
pico_enable_stdio_uart(showpic 0)
pico_enable_stdio_usb(showpic 0)
# the decoders check for NULL from malloc(); without this the SDK stops instead
target_compile_definitions(showpic PRIVATE PICO_MALLOC_PANIC=0)
target_link_libraries(showpic picocalc_picker picocalc_images
    pico_stdlib hardware_gpio hardware_i2c hardware_spi hardware_pio hardware_watchdog)
pico_add_extra_outputs(showpic)
