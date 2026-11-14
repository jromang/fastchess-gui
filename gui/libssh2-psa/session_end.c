/* The end of a libssh2 session whose connection is over: see session_end.h.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "libssh2_priv.h"

#include "session_end.h"

void fcg_libssh2_drop_unsent(LIBSSH2_SESSION *session)
{
    struct transportpacket *p = &session->packet;
    struct packet_queue_listener_state *l = &session->packAdd_Qlstn_state;

    p->odata = NULL;
    p->olen = 0;
    p->osent = 0;
    p->ototal_num = 0;
    session->socket_block_directions &= ~LIBSSH2_SESSION_BLOCK_OUTBOUND;

    /* A connection the server forwarded whose confirmation was that packet: libssh2 holds its channel, made but not
     * queued for accepting yet, where nothing would free it. */
    if(l->state == ssh2_NB_state_created && l->channel) {
        SSH2_FREE(session, l->channel->channel_type);
        SSH2_FREE(session, l->channel);
        l->channel = NULL;
        l->state = ssh2_NB_state_idle;
    }
}
