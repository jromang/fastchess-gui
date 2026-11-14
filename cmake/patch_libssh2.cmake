# Run in the libssh2 sources after download. libssh2 keeps each data packet it receives for a channel as an
# allocation of its own, with a list node, until the channel is read: some 116 bytes of the tunnel's memory for a
# packet of one byte. Before a read hands anything over, it sends the channel's window adjustment when the window runs
# low, and while that cannot go (the connection to the server full), the server may still send up to the rest of the
# window, 2 MiB: in one-byte packets from a viewer, 174 MiB, far past what the tunnel lets the session hold (32 MiB),
# and the session ended. Patched (src/packet.c, src/libssh2_priv.h), with the reasons in comments there:
#  - a data packet is appended to the last one queued for its channel when that one is of the same kind and both fit
#    in 16 KiB, so that what libssh2 holds for a channel is about the bytes it received, however they were cut;
#  - data that comes for a channel after its CLOSE was sent is dropped: nothing reads it any more, and libssh2 kept it
#    until the channel was freed;
#  - a connection the server forwards (forwarded-tcpip) is given a window of 128 KiB instead of
#    LIBSSH2_CHANNEL_WINDOW_DEFAULT (2 MiB, which nothing lets a program change): what libssh2 may hold for it while
#    its reads cannot send their adjustment. With 2 MiB, 24 viewers sending their window in large packets meanwhile
#    ended the session. With 128 KiB, a connection costs 180 KiB of libssh2's memory at most (144 KiB of data, a
#    read's adjustment included, 13 % more for its packets, and a packet read in part), so that the 80 the tunnel
#    keeps (40 relayed, 40 waiting) and 80 more being closed fit in the session's 32 MiB. A viewer sends a request of
#    8 KiB at most.
# Otherwise the window accounting is libssh2's, unchanged. The patch can run again (on sources patched already, it
# changes nothing), and fails the configuration if libssh2's code is no longer the one expected.

# Replaces `original` by `patched` in `file`, once; nothing when `patched` is there already.
function(patch file original patched)
    file(READ ${file} text)
    string(FIND "${text}" "${patched}" done)
    if(NOT done EQUAL -1)
        return()
    endif()
    string(FIND "${text}" "${original}" first)
    string(FIND "${text}" "${original}" last REVERSE)
    if(first EQUAL -1 OR NOT first EQUAL last)
        message(FATAL_ERROR "patch_libssh2.cmake: the code to patch is not where expected in ${file}")
    endif()
    string(REPLACE "${original}" "${patched}" text "${text}")
    file(WRITE ${file} "${text}")
endfunction()

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
            if(!channelp) {
                ssh2_err(session, LIBSSH2_ERROR_CHANNEL_UNKNOWN,
                         "Packet received for unknown channel");
                SSH2_FREE(session, data);
                session->packAdd_state = ssh2_NB_state_idle;
                return 0;
            }
#ifdef LIBSSH2DEBUG
]=] [=[
            if(!channelp) {
                ssh2_err(session, LIBSSH2_ERROR_CHANNEL_UNKNOWN,
                         "Packet received for unknown channel");
                SSH2_FREE(session, data);
                session->packAdd_state = ssh2_NB_state_idle;
                return 0;
            }
            /* fastchess-gui: dropped, for a channel whose CLOSE was sent:
               nothing reads it any more (a program closes a channel it is done
               with), and it was kept until the channel was freed. Nor is the
               window adjusted for it below (ignored extended data). */
            if(channelp->local.close ||
               channelp->close_state == ssh2_NB_state_sent) {
                SSH2_FREE(session, data);
                session->packAdd_state = ssh2_NB_state_idle;
                return 0;
            }
#ifdef LIBSSH2DEBUG
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
                    /* fastchess-gui: 128 KiB, not LIBSSH2_CHANNEL_WINDOW_DEFAULT
                       (2 MiB), for what the server may send on a forwarded
                       connection before it is read: what libssh2 holds for it
                       while a read cannot send its window adjustment, for
                       each of the many a public web page has */
                    channel->remote.window_size_initial = 128 * 1024;
                    channel->remote.window_size = 128 * 1024;
]=])
