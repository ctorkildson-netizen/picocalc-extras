# net.cmake -- Wi-Fi, a TCP connection, name lookups and ping for a program
# on a PicoCalc with a Pico 2 W (see net/README.md).
#
#   include(path/to/picocalc-extras/fatfs/fatfs.cmake)
#   picocalc_fatfs(path/to/picocalc-text-starter)       # /ntp/ntp.cfg is on the card
#   include(path/to/picocalc-extras/net/net.cmake)
#   picocalc_net(path/to/picocalc-shell)                # for the shell's ntp.cfg parser
#   target_link_libraries(myprogram picocalc_net ...)
#
# lwIP's settings come from net/config/lwipopts.h, unless the program sets
# PICOCALC_NET_OWN_LWIPOPTS and puts its own lwipopts.h on its include path
# (it needs LWIP_RAW 1 for ping, and a TCP_WND under 16 KB).
set(PICOCALC_NET_DIR ${CMAKE_CURRENT_LIST_DIR})
function(picocalc_net SHELL)
    if (TARGET picocalc_net)
        return()
    endif()
    if (NOT EXISTS ${SHELL}/src/core/ntp.c)
        message(FATAL_ERROR "net.cmake: ${SHELL} isn't Blair Leduc's picocalc-shell (no src/core/ntp.c)")
    endif()
    if (NOT TARGET picocalc_fs)
        message(FATAL_ERROR "net.cmake: call picocalc_fatfs() first (net reads /ntp/ntp.cfg from the card)")
    endif()
    add_library(picocalc_net INTERFACE)
    target_sources(picocalc_net INTERFACE
        ${PICOCALC_NET_DIR}/net.c ${PICOCALC_NET_DIR}/ping.c ${SHELL}/src/core/ntp.c)
    target_include_directories(picocalc_net INTERFACE ${PICOCALC_NET_DIR} ${SHELL}/src/core)
    if (NOT PICOCALC_NET_OWN_LWIPOPTS)
        target_include_directories(picocalc_net INTERFACE ${PICOCALC_NET_DIR}/config)
    endif()
    target_link_libraries(picocalc_net INTERFACE
        picocalc_fs pico_stdlib pico_cyw43_arch_lwip_threadsafe_background)
endfunction()
