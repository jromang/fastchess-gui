/* libssh2's build configuration (HAVE_CONFIG_H), for the two platforms fastchess-gui is built for: Linux with glibc,
 * and Windows with mingw-w64. Written by hand: libssh2 is compiled by our CMake, not by its own.
 *
 * It also brings in the cryptography: libssh2 is built with LIBSSH2_MBEDTLS, its own mbedtls.h left out (its include
 * guard, LIBSSH2_MBEDTLS_H, is defined on the command line), and mbedtls_psa.h in its place, which needs Mbed TLS 4
 * and its PSA API only. This file is the first libssh2 includes, before its own headers. */

#ifndef FCG_LIBSSH2_CONFIG_H
#define FCG_LIBSSH2_CONFIG_H

#ifdef _WIN32
/* As libssh2_setup.h sets them after this file: the same before any system header. */
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOGDI
#    define NOGDI
#  endif
#  ifdef __MINGW32__
#    undef _FILE_OFFSET_BITS
#    define _FILE_OFFSET_BITS 64
#  endif
#  define HAVE_UNISTD_H
#  define HAVE_INTTYPES_H
/* Non-blocking sockets through ioctlsocket(), libssh2's default on Windows. */
#else
#  define HAVE_UNISTD_H
#  define HAVE_INTTYPES_H
#  define HAVE_SYS_SELECT_H
#  define HAVE_SYS_UIO_H
#  define HAVE_SYS_IOCTL_H
#  define HAVE_SYS_UN_H
#  define HAVE_POLL
#  define HAVE_O_NONBLOCK
#  ifdef __linux__
#    define HAVE_EXPLICIT_BZERO
#  endif
#endif

#include "mbedtls_psa.h"

#endif /* FCG_LIBSSH2_CONFIG_H */
