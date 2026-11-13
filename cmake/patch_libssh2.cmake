# Run in the libssh2 sources after download. A connection the server forwards (forwarded-tcpip) is given a receive
# window of LIBSSH2_CHANNEL_WINDOW_DEFAULT, 2 MiB, which nothing lets a program change: what the server may send on
# it before it is read, and what libssh2 then holds in memory. The tunnel relays up to 80 of them (those waiting to be
# accepted included), any of which a viewer can fill by sending what the web server does not read: 160 MiB. 256 KiB
# instead, plenty for what a viewer sends to the web page. Only this window changes; the patch can run again (on
# sources patched already, it changes nothing), and fails the configuration if libssh2's code is no longer the one
# expected.
set(file src/packet.c)
set(original [=[
                    channel->remote.id = listen_state->sender_channel;
                    channel->remote.window_size_initial =
                        LIBSSH2_CHANNEL_WINDOW_DEFAULT;
                    channel->remote.window_size =
                        LIBSSH2_CHANNEL_WINDOW_DEFAULT;
]=])
set(patched [=[
                    channel->remote.id = listen_state->sender_channel;
                    /* fastchess-gui: 256 KiB, not LIBSSH2_CHANNEL_WINDOW_DEFAULT
                       (2 MiB), for what the server may send on a forwarded
                       connection before it is read (cmake/patch_libssh2.cmake) */
                    channel->remote.window_size_initial = 256 * 1024;
                    channel->remote.window_size = 256 * 1024;
]=])
file(READ ${file} text)
string(FIND "${text}" "${patched}" done)
if(done EQUAL -1)
    string(FIND "${text}" "${original}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "patch_libssh2.cmake: the forwarded connection's window is not where expected in ${file}")
    endif()
    string(REPLACE "${original}" "${patched}" text "${text}")
    file(WRITE ${file} "${text}")
endif()
