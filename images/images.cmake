# images.cmake -- the picture decoders, for a Pico SDK program.
#
#   include(path/to/picocalc-extras/images/images.cmake)
#   target_link_libraries(myprogram picocalc_images ...)
#
# The program supplies src_read(), src_seek(), src_tell() and src_size()
# (see format.h).
set(PICOCALC_IMAGES ${CMAKE_CURRENT_LIST_DIR})
set(PICOCALC_PNGDEC ${PICOCALC_IMAGES}/modules/PNGdec/src)
set(PICOCALC_ZLIB
    ${PICOCALC_PNGDEC}/adler32.c ${PICOCALC_PNGDEC}/crc32.c ${PICOCALC_PNGDEC}/infback.c
    ${PICOCALC_PNGDEC}/inffast.c ${PICOCALC_PNGDEC}/inflate.c ${PICOCALC_PNGDEC}/inftrees.c
    ${PICOCALC_PNGDEC}/zutil.c)
# PNGdec's own code (and fmt_png.c, which includes it) isn't warning-clean
set_source_files_properties(${PICOCALC_ZLIB} ${PICOCALC_IMAGES}/fmt_png.c
    PROPERTIES COMPILE_OPTIONS "-w")
add_library(picocalc_images INTERFACE)
target_sources(picocalc_images INTERFACE
    ${PICOCALC_IMAGES}/formats.c ${PICOCALC_IMAGES}/canvas.c
    ${PICOCALC_IMAGES}/fmt_jpeg.c ${PICOCALC_IMAGES}/fmt_bmp.c ${PICOCALC_IMAGES}/fmt_png.c
    ${PICOCALC_IMAGES}/tjpgd/tjpgd.c
    ${PICOCALC_ZLIB})
target_include_directories(picocalc_images INTERFACE
    ${PICOCALC_IMAGES} ${PICOCALC_IMAGES}/tjpgd ${PICOCALC_PNGDEC})
