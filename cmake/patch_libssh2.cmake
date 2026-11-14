# Run in the libssh2 sources after download. libssh2 keeps each data packet it receives for a channel as an
# allocation of its own, with a list node, until the channel is read: some 116 bytes of the tunnel's memory for a
# packet of one byte. Before a read hands anything over, it sends the channel's window adjustment when the window runs
# low, and while that cannot go (the connection to the server full), the server may still send up to the rest of the
# window, 2 MiB: in one-byte packets from a viewer, 174 MiB, far past what the tunnel lets the session hold (32 MiB),
# and the session ended. Patched (src/packet.c, src/session.c, src/libssh2_priv.h, include/libssh2.h), with the
# reasons in comments there:
#  - a data packet is appended to the last one queued for its channel when that one is of the same kind and both fit
#    in 16 KiB, so that what libssh2 holds for a channel is about the bytes it received, however they were cut;
#  - a connection the server forwards (forwarded-tcpip) is given a window of FCG_LIBSSH2_FORWARD_WINDOW, 128 KiB,
#    instead of LIBSSH2_CHANNEL_WINDOW_DEFAULT (2 MiB, which nothing lets a program change): what libssh2 may hold for
#    it while its reads cannot send their adjustment. With 2 MiB, 24 viewers sending their window in large packets
#    meanwhile ended the session; the tunnel checks at compile time that all it may hold fits (ssh_tunnel.cpp). A
#    viewer sends a request of 8 KiB at most.
#  - fcg_libssh2_session_end(), appended to src/session.c, for a session whose connection is over, before
#    libssh2_session_free(): libssh2 sends nothing else while a packet it started is not finished (the socket full: a
#    server that stopped reading), the channels' closing included, so that the session could never be freed. It sends
#    and receives nothing more then, forgets the rest of that packet, and frees a forwarded connection's channel whose
#    confirmation was held back. The code it relies on is checked to be the one it was written for (src/packet.c,
#    src/transport.c, src/channel.c). And the channel of a forwarded connection whose confirmation failed is freed
#    (libssh2 left it behind).
#  - fcg_libssh2_send_unsent(), appended to src/session.c too, for the tunnel's goodbye: the rest of a packet libssh2
#    could not finish (the socket full), which only the call that started it sends otherwise, and which holds back the
#    DISCONNECT. The code it relies on is checked too (src/transport.c).
#  - include/libssh2.h says so: FCG_LIBSSH2_PATCHLEVEL, which the tunnel requires, so that sources not patched (an
#    override of FETCHCONTENT_SOURCE_DIR_LIBSSH2) or patched by an older version of this script fail to compile.
# Otherwise the window accounting is libssh2's, unchanged.
#
# FetchContent runs a patch again on the sources already there, when its command changes (the command has this
# script's hash, CMakeLists.txt): each file is patched from the original kept beside it (.fcg-orig), never twice. The
# configuration fails if libssh2's code is no longer the one expected.
set(FILES src/packet.c src/session.c src/libssh2_priv.h include/libssh2.h)
foreach(file ${FILES})
    if(EXISTS ${file}.fcg-orig)
        configure_file(${file}.fcg-orig ${file} COPYONLY)
    else()
        file(READ ${file} text)
        string(FIND "${text}" "fastchess-gui" patched)
        if(NOT patched EQUAL -1)
            message(FATAL_ERROR "patch_libssh2.cmake: ${CMAKE_CURRENT_SOURCE_DIR}/${file} was patched by an older "
                                "version of this script, which did not keep the original: delete the libssh2 "
                                "directories in the build tree's _deps and configure again")
        endif()
        configure_file(${file} ${file}.fcg-orig COPYONLY)
    endif()
endforeach()

# Replaces `original` by `patched` in `file`; `original` must be there, once.
function(patch file original patched)
    file(READ ${file} text)
    string(FIND "${text}" "${original}" first)
    string(FIND "${text}" "${original}" last REVERSE)
    if(first EQUAL -1 OR NOT first EQUAL last)
        message(FATAL_ERROR "patch_libssh2.cmake: the code to patch is not where expected in ${file}")
    endif()
    string(REPLACE "${original}" "${patched}" text "${text}")
    file(WRITE ${file} "${text}")
endfunction()

# Fails unless `original` is in `file`, once: code that something patched here relies on, as it was written for.
function(expect file original)
    file(READ ${file} text)
    string(FIND "${text}" "${original}" first)
    string(FIND "${text}" "${original}" last REVERSE)
    if(first EQUAL -1 OR NOT first EQUAL last)
        message(FATAL_ERROR "patch_libssh2.cmake: the code relied on is not as expected in ${file}")
    endif()
endfunction()

# What the tunnel checks, and the window it is given.
patch(include/libssh2.h [=[
#ifndef LIBSSH2_H
#define LIBSSH2_H 1
]=] [=[
#ifndef LIBSSH2_H
#define LIBSSH2_H 1

/* fastchess-gui: patched by its cmake/patch_libssh2.cmake, at this level,
   with this window for the connections a server forwards */
#define FCG_LIBSSH2_PATCHLEVEL 4
#define FCG_LIBSSH2_FORWARD_WINDOW (128 * 1024)
]=])

# The size of a queued packet's allocation, which a merge may leave larger than its data.
patch(src/libssh2_priv.h [=[
    /* Where to start reading data from,
     * used for channel data that is been partially consumed */
    size_t data_head;
};
]=] [=[
    /* Where to start reading data from,
     * used for channel data that is been partially consumed */
    size_t data_head;

    /* fastchess-gui: the size of the allocation 'data' points to, at least
       data_len: fcg_merge_data() in packet.c appends to it */
    size_t data_size;
};
]=])

patch(src/packet.c [=[
/*
 * Create a new packet and attach it to the brigade. Called from the transport
 * layer when it has received a packet.
]=] [=[
/*
 * fastchess-gui: a channel's data packet ('data', its payload from data_head
 * to datalen) appended to the last data packet queued for the same channel,
 * when that one is of the same kind (message type, and stream for extended
 * data) and the two fit in FCG_MERGE_MAX bytes (half the largest packet
 * libssh2 takes: large packets are queued as before, never copied). Each
 * packet queued costs an allocation and a list node, about a hundred bytes
 * whatever its size: a peer sending one-byte packets within the channel's
 * window (while a read cannot send its window adjustment) made libssh2 hold a
 * hundred times the bytes. Merged, what is held is about the bytes: a packet
 * grows FCG_MERGE_STEP bytes at a time, so that it is never more than that
 * larger than its data, and two packets in a row hold more than
 * FCG_MERGE_MAX bytes (or they would be one), some 13 % more at worst. A byte
 * is copied 8 times on average as a packet fills (the reallocations).
 *
 * Every reader of queued channel data (ssh2_channel_read, ssh2_channel_flush,
 * ssh2_channel_packet_data_len, libssh2_channel_window_read_ex) takes its
 * bytes from data[data_head] to data[data_len], and the message type,
 * channel and stream from the first 9 bytes: appended after data_len, which
 * may have been read in part already, the bytes come in the order received.
 * Only the channel's last data packet is appended to, so that its data and
 * extended data do not change order. The window accounting (read_avail, then
 * remote.window_size as the data is read) is the caller's, as for any packet.
 *
 * Returns 1 when appended: 'data' is then the caller's to free.
 * See cmake/patch_libssh2.cmake in fastchess-gui.
 */
#define FCG_MERGE_MAX 16384
#define FCG_MERGE_STEP 1024
static int fcg_merge_data(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channelp,
                          const unsigned char *data, size_t datalen,
                          size_t data_head)
{
    struct packet *last = (struct packet *)session->packets.last;
    size_t length = datalen - data_head;
    size_t need;

    while(last && !(last->data_len >= 5 &&
                    (last->data[0] == SSH_MSG_CHANNEL_DATA ||
                     last->data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA) &&
                    ssh2_ntohu32(last->data + 1) == channelp->local.id))
        last = ssh2_list_prev(&last->node);

    if(!last || last->data[0] != data[0] || last->data_len < data_head ||
       (data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA &&
        memcmp(last->data + 5, data + 5, 4)) ||
       last->data_len + length > FCG_MERGE_MAX)
        return 0;

    need = last->data_len + length;
    if(need > last->data_size) {
        size_t size = (need + FCG_MERGE_STEP - 1) / FCG_MERGE_STEP *
                      FCG_MERGE_STEP;
        unsigned char *grown;
        if(size > FCG_MERGE_MAX)
            size = FCG_MERGE_MAX;
        grown = SSH2_REALLOC(session, last->data, size);
        if(!grown)
            return 0;
        last->data = grown;
        last->data_size = size;
    }
    memcpy(last->data + last->data_len, data + data_head, length);
    last->data_len = need;
    return 1;
}

/*
 * Create a new packet and attach it to the brigade. Called from the transport
 * layer when it has received a packet.
]=])

patch(src/packet.c [=[
            ssh2_deb((session, LIBSSH2_TRACE_CONN,
                      "increasing read_avail by %ld bytes to %ld/%u",
                      (long)(datalen - data_head),
                      (long)channelp->read_avail,
                      channelp->remote.window_size));

            break;
]=] [=[
            ssh2_deb((session, LIBSSH2_TRACE_CONN,
                      "increasing read_avail by %ld bytes to %ld/%u",
                      (long)(datalen - data_head),
                      (long)channelp->read_avail,
                      channelp->remote.window_size));

            /* fastchess-gui: appended to the last packet queued for the
               channel when it can be, rather than queued (fcg_merge_data) */
            if(fcg_merge_data(session, channelp, data, datalen, data_head)) {
                SSH2_FREE(session, data);
                session->packAdd_state = ssh2_NB_state_idle;
                return 0;
            }
            break;
]=])

patch(src/packet.c [=[
        packetp->data = data;
        packetp->data_len = datalen;
        packetp->data_head = data_head;
]=] [=[
        packetp->data = data;
        packetp->data_len = datalen;
        packetp->data_head = data_head;
        packetp->data_size = datalen; /* fastchess-gui: fcg_merge_data() */
]=])

patch(src/packet.c [=[
                    channel->remote.id = listen_state->sender_channel;
                    channel->remote.window_size_initial =
                        LIBSSH2_CHANNEL_WINDOW_DEFAULT;
                    channel->remote.window_size =
                        LIBSSH2_CHANNEL_WINDOW_DEFAULT;
]=] [=[
                    channel->remote.id = listen_state->sender_channel;
                    /* fastchess-gui: FCG_LIBSSH2_FORWARD_WINDOW (128 KiB), not
                       LIBSSH2_CHANNEL_WINDOW_DEFAULT (2 MiB), for what the
                       server may send on a forwarded connection before it is
                       read: what libssh2 holds for it while a read cannot send
                       its window adjustment, for each of the many a public web
                       page has */
                    channel->remote.window_size_initial =
                        FCG_LIBSSH2_FORWARD_WINDOW;
                    channel->remote.window_size = FCG_LIBSSH2_FORWARD_WINDOW;
]=])

# The end of a session whose connection is over: declared, then appended to src/session.c, with what it relies on.
patch(include/libssh2.h [=[
LIBSSH2_API int libssh2_session_free(LIBSSH2_SESSION *session);
]=] [=[
LIBSSH2_API int libssh2_session_free(LIBSSH2_SESSION *session);
/* fastchess-gui: for a session whose connection is over, before
   libssh2_session_free(): see src/session.c */
LIBSSH2_API void fcg_libssh2_session_end(LIBSSH2_SESSION *session);
/* fastchess-gui: sends the rest of the packet under way, before a
   DISCONNECT: see src/session.c */
LIBSSH2_API int fcg_libssh2_send_unsent(LIBSSH2_SESSION *session);
]=])

# A forwarded connection's channel, until its confirmation went: made in packet_queue_listener() holding only its
# channel_type, and in no list (state ssh2_NB_state_created). fcg_libssh2_session_end() frees it then. Where libssh2
# freed it without forgetting it, it is forgotten; where it left it behind (the confirmation failed), it is freed.
patch(src/packet.c [=[
                    listen_state->channel = channel;

                    channel->session = session;
                    channel->channel_type_len = sizeof("forwarded-tcpip") - 1;
                    channel->channel_type =
                        SSH2_ALLOC(session, channel->channel_type_len + 1);
                    if(!channel->channel_type) {
                        ssh2_err(session, LIBSSH2_ERROR_ALLOC,
                                 "Unable to allocate a channel for "
                                 "new connection");
                        SSH2_FREE(session, channel);
                        failure_code = SSH_OPEN_RESOURCE_SHORTAGE;
                        listen_state->state = ssh2_NB_state_sent;
                        break;
                    }
                    memcpy(channel->channel_type, "forwarded-tcpip",
                           channel->channel_type_len + 1);
]=] [=[
                    listen_state->channel = channel;

                    channel->session = session;
                    channel->channel_type_len = sizeof("forwarded-tcpip") - 1;
                    channel->channel_type =
                        SSH2_ALLOC(session, channel->channel_type_len + 1);
                    if(!channel->channel_type) {
                        ssh2_err(session, LIBSSH2_ERROR_ALLOC,
                                 "Unable to allocate a channel for "
                                 "new connection");
                        SSH2_FREE(session, channel);
                        /* fastchess-gui: freed, so forgotten */
                        listen_state->channel = NULL;
                        failure_code = SSH_OPEN_RESOURCE_SHORTAGE;
                        listen_state->state = ssh2_NB_state_sent;
                        break;
                    }
                    memcpy(channel->channel_type, "forwarded-tcpip",
                           channel->channel_type_len + 1);
]=])

patch(src/packet.c [=[
                    p = listen_state->packet;
                    *(p++) = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
                    ssh2_store_u32(&p, channel->remote.id);
                    ssh2_store_u32(&p, channel->local.id);
                    ssh2_store_u32(&p, channel->remote.window_size_initial);
                    ssh2_store_u32(&p, channel->remote.packet_size);

                    listen_state->state = ssh2_NB_state_created;
                }

                if(listen_state->state == ssh2_NB_state_created) {
                    rc = ssh2_transport_send(session, listen_state->packet,
                                             17, NULL, 0);
                    if(rc == LIBSSH2_ERROR_EAGAIN)
                        return rc;
                    else if(rc) {
                        listen_state->state = ssh2_NB_state_idle;
                        return ssh2_err(session, rc,
                                        "Unable to send channel "
                                        "open confirmation");
                    }

                    /* Link the channel into the end of the queue list */
                    if(listen_state->channel) {
                        ssh2_list_add(&listn->queue,
                                      &listen_state->channel->node);
                        listn->queue_size++;
                    }

                    listen_state->state = ssh2_NB_state_idle;
                    return 0;
                }
]=] [=[
                    p = listen_state->packet;
                    *(p++) = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
                    ssh2_store_u32(&p, channel->remote.id);
                    ssh2_store_u32(&p, channel->local.id);
                    ssh2_store_u32(&p, channel->remote.window_size_initial);
                    ssh2_store_u32(&p, channel->remote.packet_size);

                    listen_state->state = ssh2_NB_state_created;
                }

                if(listen_state->state == ssh2_NB_state_created) {
                    rc = ssh2_transport_send(session, listen_state->packet,
                                             17, NULL, 0);
                    if(rc == LIBSSH2_ERROR_EAGAIN)
                        return rc;
                    else if(rc) {
                        /* fastchess-gui: the channel, in no list yet, freed:
                           nothing else would */
                        if(listen_state->channel) {
                            SSH2_FREE(session,
                                      listen_state->channel->channel_type);
                            SSH2_FREE(session, listen_state->channel);
                            listen_state->channel = NULL;
                        }
                        listen_state->state = ssh2_NB_state_idle;
                        return ssh2_err(session, rc,
                                        "Unable to send channel "
                                        "open confirmation");
                    }

                    /* Link the channel into the end of the queue list */
                    if(listen_state->channel) {
                        ssh2_list_add(&listn->queue,
                                      &listen_state->channel->node);
                        listn->queue_size++;
                    }

                    listen_state->state = ssh2_NB_state_idle;
                    return 0;
                }
]=])

# What is left of the packet under way: what fcg_libssh2_session_end() forgets.
expect(src/transport.c [=[
            /* the whole packet could not be sent, save the rest */
            session->socket_block_directions |= LIBSSH2_SESSION_BLOCK_OUTBOUND;
            p->odata = orgdata;
            p->olen = orgdata_len;
            p->osent = ret <= 0 ? 0 : ret;
            p->ototal_num = total_length;
            return LIBSSH2_ERROR_EAGAIN;
]=])
# The rest of the packet under way, sent from the call that started it only: what fcg_libssh2_send_unsent() sends.
expect(src/transport.c [=[
    if(data != p->odata || data_len != p->olen) {
        /* When we are about to complete the sending of a packet, it is vital
           that the caller does not try to send a new/different packet since
           we do not add this one up until the previous one has been sent. To
           make the caller really notice his/hers flaw, we return error for
           this case */
        ssh2_deb((session, LIBSSH2_TRACE_SOCKET,
                  "Address is different, returning EAGAIN"));
        return LIBSSH2_ERROR_EAGAIN;
    }
]=])
expect(src/transport.c [=[
    rc = SSH2_SEND(session, &p->outbuf[p->osent], length,
                   SSH2_SOCKET_SEND_FLAGS(session));
]=])
# A channel is closed only while the connection is: fcg_libssh2_session_end() marks it disconnected.
expect(src/channel.c [=[
    /* Allow channel freeing even when the socket has lost its connection */
    if(!channel->local.close &&
       (session->socket_state == SSH2_SOCKET_CONNECTED)) {
        rc = ssh2_channel_close(channel);
]=])

file(APPEND src/session.c [=[

/*
 * fastchess-gui: the end of a session whose connection is over, called before
 * libssh2_session_free(). libssh2 sends nothing else while a packet it
 * started is not finished (the socket full: a peer that stopped reading),
 * only the rest of it, and only from the call that started it:
 * libssh2_session_free() returned LIBSSH2_ERROR_EAGAIN for ever then, the
 * channels' closing never going, and the session was left behind with its
 * channels and keys. Here, whatever the caller did with its socket:
 *  - the session sends and receives nothing more: its send and receive
 *    callbacks fail at once (the stream could not go on after a packet cut
 *    short anyway), and it is marked disconnected, so that its channels are
 *    freed without their closing (ssh2_channel_free);
 *  - what is left of the packet under way is forgotten (ssh2_transport_send
 *    keeps it in session->packet);
 *  - a connection the peer forwarded whose confirmation was held back, by
 *    the packet under way or one before it (packet_queue_listener in
 *    packet.c, in state ssh2_NB_state_created: its channel made, holding only
 *    its channel_type, in no list until the confirmation went), has its
 *    channel freed: nothing else would. Its state stays: were the
 *    CHANNEL_OPEN taken up again (by the reads of a key re-exchange that
 *    libssh2_session_free() resumes), its confirmation is sent for no channel
 *    and fails, where from ssh2_NB_state_idle a new channel would be made,
 *    and left behind too.
 * cmake/patch_libssh2.cmake in fastchess-gui checks that the code this relies
 * on is still the one it was written for.
 */
static LIBSSH2_SEND_FUNC(fcg_send_nothing)
{
    (void)socket;
    (void)buffer;
    (void)length;
    (void)flags;
    (void)abstract;
    return -EPIPE;
}

static LIBSSH2_RECV_FUNC(fcg_receive_nothing)
{
    (void)socket;
    (void)buffer;
    (void)length;
    (void)flags;
    (void)abstract;
    return -EPIPE;
}

LIBSSH2_API void fcg_libssh2_session_end(LIBSSH2_SESSION *session)
{
    struct transportpacket *p = &session->packet;
    struct packet_queue_listener_state *l = &session->packAdd_Qlstn_state;

    session->send_cb = fcg_send_nothing;
    session->recv_cb = fcg_receive_nothing;
    session->socket_state = SSH2_SOCKET_DISCONNECTED;

    p->odata = NULL;
    p->olen = 0;
    p->osent = 0;
    p->ototal_num = 0;
    session->socket_block_directions &= ~LIBSSH2_SESSION_BLOCK_OUTBOUND;

    if(l->state == ssh2_NB_state_created && l->channel) {
        SSH2_FREE(session, l->channel->channel_type);
        SSH2_FREE(session, l->channel);
        l->channel = NULL;
    }
}

/*
 * fastchess-gui: sends the rest of the packet under way, before a DISCONNECT.
 * A packet libssh2 started to send and could not finish (the socket full)
 * holds back every other: ssh2_transport_send() returns LIBSSH2_ERROR_EAGAIN
 * for any but the one it started, whose rest only the same call sends, made
 * again with the same arguments (transport_send_existing in transport.c).
 * A program that stops makes none of those calls again (a channel's write, a
 * window adjustment, a keepalive), and libssh2_session_disconnect() returned
 * LIBSSH2_ERROR_EAGAIN until it gave up: the peer saw the connection end with
 * no goodbye. The rest of the packet goes from here, as that call would send
 * it; the call itself must not be made again then, or its packet would go
 * twice. Returns 0 once nothing is left to send, LIBSSH2_ERROR_EAGAIN while
 * some is (the socket full), LIBSSH2_ERROR_SOCKET_SEND if it cannot go.
 */
LIBSSH2_API int fcg_libssh2_send_unsent(LIBSSH2_SESSION *session)
{
    struct transportpacket *p = &session->packet;
    ssize_t rc;

    if(!p->olen)
        return 0;
    rc = SSH2_SEND(session, &p->outbuf[p->osent],
                   (size_t)(p->ototal_num - (ssize_t)p->osent),
                   SSH2_SOCKET_SEND_FLAGS(session));
    if(rc < 0) {
        if(rc != -EAGAIN)
            return LIBSSH2_ERROR_SOCKET_SEND;
        session->socket_block_directions |= LIBSSH2_SESSION_BLOCK_OUTBOUND;
        return LIBSSH2_ERROR_EAGAIN;
    }
    p->osent += (size_t)rc;
    if((ssize_t)p->osent < p->ototal_num) {
        session->socket_block_directions |= LIBSSH2_SESSION_BLOCK_OUTBOUND;
        return LIBSSH2_ERROR_EAGAIN;
    }
    p->odata = NULL;
    p->olen = 0;
    p->osent = 0;
    p->ototal_num = 0;
    session->socket_block_directions &= ~LIBSSH2_SESSION_BLOCK_OUTBOUND;
    return 0;
}
]=])
