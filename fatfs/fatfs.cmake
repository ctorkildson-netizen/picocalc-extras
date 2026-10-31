# fatfs.cmake -- the SD card's file system for a program built on Blair
# Leduc's picocalc-text-starter (v0.17 or later), which uses ChaN's FatFs.
#
#   include(path/to/picocalc-extras/fatfs/fatfs.cmake)
#   picocalc_fatfs(path/to/picocalc-text-starter)
#   target_link_libraries(myprogram picocalc_fs ...)
#
# This does what the text starter's own CMakeLists.txt does (its notes are
# in docs/filesystem.md): FatFs comes from the Pico SDK (TinyUSB's copy) and
# is built with the starter's drivers/fatfs/ffconf.h, beside the starter's
# glue (diskio.c, filesystem.c) and SD card driver. picocalc_fs brings them
# with their include paths; fs_init() (or picocalc_init()) starts them.
function(picocalc_fatfs TS)
    if (TARGET picocalc_fs)
        return()
    endif()
    set(FATFS_SRC ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source)
    set(FATFS_DIR ${CMAKE_BINARY_DIR}/fatfs)
    if (NOT EXISTS ${TS}/drivers/filesystem.c)
        message(FATAL_ERROR "fatfs.cmake: ${TS} isn't picocalc-text-starter v0.17 or later (no drivers/filesystem.c)")
    endif()
    foreach(f ff.c ff.h ffunicode.c diskio.h)
        if (NOT EXISTS ${FATFS_SRC}/${f})
            message(FATAL_ERROR "fatfs.cmake: FatFs ${f} not found in ${FATFS_SRC}")
        endif()
        configure_file(${FATFS_SRC}/${f} ${FATFS_DIR}/${f} COPYONLY)
    endforeach()
    configure_file(${TS}/drivers/fatfs/ffconf.h ${FATFS_DIR}/ffconf.h COPYONLY)
    add_library(picocalc_fs INTERFACE)
    target_sources(picocalc_fs INTERFACE
        ${FATFS_DIR}/ff.c ${FATFS_DIR}/ffunicode.c
        ${TS}/drivers/diskio.c ${TS}/drivers/filesystem.c ${TS}/drivers/sdcard.c)
    target_include_directories(picocalc_fs INTERFACE
        ${FATFS_DIR} ${TS} ${TS}/drivers ${TS}/third_party/picocalc-shell)
    target_link_libraries(picocalc_fs INTERFACE pico_stdlib hardware_spi)
    # the shell's clock, for file dates (RP2350 only)
    if (TARGET hardware_powman)
        target_link_libraries(picocalc_fs INTERFACE hardware_powman)
    endif()
endfunction()
