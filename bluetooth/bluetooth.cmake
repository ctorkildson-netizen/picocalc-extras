# bluetooth.cmake -- a Bluetooth keyboard for a Pico SDK program on the
# PicoCalc with a Pico 2 W, using Blair Leduc's picocalc-text-starter drivers
# (keyboard.h for the key codes, fat32 for keeping the pairing).
#
#   set(PICO_BOARD pico2_w)
#   include(path/to/bluetooth/bluetooth.cmake)
#   target_link_libraries(myprogram picocalc_btkbd ...)
#
# The program's include path must reach the drivers as "drivers/keyboard.h"
# etc. This brings BTstack's settings (btstack_config.h) with it.
add_library(picocalc_btkbd INTERFACE)
target_sources(picocalc_btkbd INTERFACE ${CMAKE_CURRENT_LIST_DIR}/btkbd.c)
target_include_directories(picocalc_btkbd INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(picocalc_btkbd INTERFACE CYW43_LWIP=0)
# classic Bluetooth over the CYW43, run in the background (an interrupt);
# btkbd.c supplies btstack_cyw43_init() (pairings on the SD card, not in
# flash), so pico_btstack_cyw43 isn't linked
target_link_libraries(picocalc_btkbd INTERFACE
    pico_rand pico_cyw43_arch_threadsafe_background
    pico_btstack_base pico_btstack_classic
    pico_btstack_hci_transport_cyw43 pico_btstack_run_loop_async_context)
# the Bluetooth keyboard's keys come out of sb_read_keyboard() too
target_link_options(picocalc_btkbd INTERFACE -Wl,--wrap=sb_read_keyboard)
