/* The end of a libssh2 session whose connection is over.
 *
 * A packet libssh2 started to send and could not finish (the socket full) holds back every other until the call that
 * started it is made again: libssh2_session_free() too, whose channels' closing never goes then, and which returns
 * LIBSSH2_ERROR_EAGAIN for ever. The session, its channels and its keys would be left behind, at every connection
 * that ends so (a server that stopped reading, say).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FCG_LIBSSH2_SESSION_END_H
#define FCG_LIBSSH2_SESSION_END_H

#include <libssh2.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forgets what is left of the packet under way, once the socket is shut: libssh2_session_free() can then end, what it
 * would send failing at once. */
void fcg_libssh2_drop_unsent(LIBSSH2_SESSION *session);

#ifdef __cplusplus
}
#endif

#endif
