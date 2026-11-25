# ssh.cmake -- SSH for a program on a PicoCalc with a Pico 2 W: libssh2 with
# Mbed TLS (the Pico SDK's), over net/'s connections (see ssh/README.md).
#
#   picocalc_fatfs(...) and picocalc_net(...) first, then
#   include(path/to/picocalc-extras/ssh/ssh.cmake)
#   picocalc_ssh(path/to/libssh2)       # the program's libssh2 submodule (1.11.1)
#   target_link_libraries(myprogram picocalc_ssh ...)
#
# Mbed TLS is set up by ssh/config/mbedtls_config.h: only what SSH needs, and
# random numbers from the RP2350's TRNG. (The SDK's pico_mbedtls includes
# "mbedtls_config.h", and finds this one first; sshnet.c stops the build if
# it got Mbed TLS's own instead.)
set(PICOCALC_SSH_DIR ${CMAKE_CURRENT_LIST_DIR})
function(picocalc_ssh SSH2)
    if (TARGET picocalc_ssh)
        return()
    endif()
    if (NOT EXISTS ${SSH2}/src/mbedtls.c)
        message(FATAL_ERROR "ssh.cmake: ${SSH2} isn't libssh2 (no src/mbedtls.c)")
    endif()
    if (NOT TARGET picocalc_net)
        message(FATAL_ERROR "ssh.cmake: call picocalc_net() first (SSH runs over its connections)")
    endif()
    add_library(picocalc_ssh INTERFACE)
    foreach(f channel comp crypt crypto global hostkey keepalive kex knownhost mac misc
              packet pem publickey scp session sftp transport userauth userauth_kbd_packet
              version bcrypt_pbkdf blowfish chacha cipher-chachapoly poly1305)
        target_sources(picocalc_ssh INTERFACE ${SSH2}/src/${f}.c)
    endforeach()
    target_sources(picocalc_ssh INTERFACE ${PICOCALC_SSH_DIR}/sshnet.c ${PICOCALC_SSH_DIR}/sockets.c)
    target_include_directories(picocalc_ssh INTERFACE
        ${PICOCALC_SSH_DIR} ${PICOCALC_SSH_DIR}/config ${SSH2}/include)
    target_compile_definitions(picocalc_ssh INTERFACE HAVE_CONFIG_H)
    target_link_libraries(picocalc_ssh INTERFACE
        picocalc_net pico_mbedtls_crypto pico_mbedtls pico_sha256 pico_rand)
endfunction()
