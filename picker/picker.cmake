# picker.cmake -- the file picker, for a Pico SDK program that uses Blair
# Leduc's picocalc-text-starter drivers (lcd, keyboard, fat32, southbridge).
#
#   include(path/to/picocalc-extras/picker/picker.cmake)
#   target_link_libraries(myprogram picocalc_picker ...)
#
# The program's include path must reach the drivers as "drivers/lcd.h" etc.
add_library(picocalc_picker INTERFACE)
target_sources(picocalc_picker INTERFACE ${CMAKE_CURRENT_LIST_DIR}/picker.c)
target_include_directories(picocalc_picker INTERFACE ${CMAKE_CURRENT_LIST_DIR})
