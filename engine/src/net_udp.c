#include "net_udp.h"

#include "alloc.h"
#include "error.h"
#include "net.h"

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* sendto/recvfrom/htons/htonl/AF_INET/SOCK_DGRAM/struct sockaddr_in are
 * the same names and shapes under winsock2 and BSD sockets (winsock2 was
 * explicitly modeled on BSD sockets), so the actual send/recv/poll
 * bodies below are shared. Only socket lifetime (create/destroy),
 * non-blocking setup, and error-code retrieval genuinely diverge --
 * those are the pieces guarded by #if defined(_WIN32) further down. */

#if defined(_WIN32)
typedef SOCKET NetSocket;
typedef int NetSockLen;
#define NET_UDP_INVALID_SOCKET INVALID_SOCKET
#define NET_UDP_WOULD_BLOCK() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
typedef int NetSocket;
typedef socklen_t NetSockLen;
#define NET_UDP_INVALID_SOCKET (-1)
/* EWOULDBLOCK/EAGAIN come from errno.h via the glibc chain errno.h ->
 * bits/errno.h -> linux/errno.h -> asm-generic/errno(-base).h; <errno.h>
 * is the correct portable include, clang's mapping just doesn't
 * attribute through that many hops (same false-positive class as
 * debug.c's CLOCK_REALTIME note). */
// NOLINTNEXTLINE(misc-include-cleaner)
#define NET_UDP_WOULD_BLOCK() (errno == EWOULDBLOCK || errno == EAGAIN)
#endif

typedef struct {
    NetSocket socket_handle;
    Allocator alloc; /* stored so net_udp_destroy can free this struct itself */
} NetUdpState;

#define NET_UDP_SOCK_OPT_ENABLE 1

/* Best-effort SO_REUSEADDR (+ SO_REUSEPORT where the platform defines
 * it) so two sockets on the same machine can both bind DISCOVERY_PORT --
 * see net_udp_create's doc comment (net_udp.h) for why this matters.
 * setsockopt failures are silently ignored: worst case, a same-box
 * second bind fails later at bind() with its own error, which IS
 * reported by the caller. Called before bind(), the only point POSIX
 * honors these options. */
static void net_udp_set_reuse_options(NetSocket socket_handle)
{
    int enable = NET_UDP_SOCK_OPT_ENABLE;
    /* SOL_SOCKET/SO_REUSEADDR/SO_REUSEPORT come from sys/socket.h via the
     * glibc chain sys/socket.h -> bits/socket.h -> asm-generic/socket.h;
     * <sys/socket.h> (included above) is the correct portable include,
     * clang's mapping just doesn't attribute through that many hops --
     * same false-positive class as this file's own EWOULDBLOCK/EAGAIN
     * note above (confirmed via `clang -E -dM -x c` against
     * <sys/socket.h>, same verification method). */
    // NOLINTNEXTLINE(misc-include-cleaner)
    (void)setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR, (const char *)&enable, sizeof(enable));
#if defined(SO_REUSEPORT)
    (void)setsockopt(socket_handle, SOL_SOCKET, SO_REUSEPORT, (const char *)&enable, sizeof(enable));
#endif
}

/* Best-effort SO_BROADCAST -- see net_udp_create's doc comment (net_udp.h)
 * for why a beaconing host socket needs this before sendto() will accept
 * a broadcast destination. Silently ignored on failure, same tolerant
 * contract as net_udp_set_reuse_options above. */
static void net_udp_set_broadcast_option(NetSocket socket_handle)
{
    int enable = NET_UDP_SOCK_OPT_ENABLE;
    // NOLINTNEXTLINE(misc-include-cleaner) -- see net_udp_set_reuse_options above
    (void)setsockopt(socket_handle, SOL_SOCKET, SO_BROADCAST, (const char *)&enable, sizeof(enable));
}

static void net_addr_to_sockaddr(NetAddr addr, struct sockaddr_in *out)
{
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(addr.port);
    out->sin_addr.s_addr = htonl(addr.host);
}

static NetAddr sockaddr_to_net_addr(const struct sockaddr_in *address)
{
    return net_addr_make(ntohl(address->sin_addr.s_addr), ntohs(address->sin_port));
}

static int net_udp_send(void *state, NetAddr dest, const void *data, size_t len)
{
    if (len > NET_MAX_PACKET_SIZE) {
        return -1;
    }
    NetUdpState *udp_state = (NetUdpState *)state;
    struct sockaddr_in dest_addr;
    net_addr_to_sockaddr(dest, &dest_addr);

    int bytes_sent = (int)sendto(udp_state->socket_handle, (const char *)data, (int)len, 0,
                                 (const struct sockaddr *)&dest_addr, (int)sizeof(dest_addr));
    if (bytes_sent < 0) {
        if (NET_UDP_WOULD_BLOCK()) {
            return 0;
        }
        return -1;
    }
    return bytes_sent;
}

static int net_udp_recv(void *state, NetAddr *out_src, void *buffer, size_t cap)
{
    NetUdpState *udp_state = (NetUdpState *)state;
    struct sockaddr_in src_addr = {0};
    NetSockLen addr_len = sizeof(src_addr);

    int bytes_received =
        (int)recvfrom(udp_state->socket_handle, (char *)buffer, (int)cap, 0, (struct sockaddr *)&src_addr, &addr_len);
    if (bytes_received < 0) {
        if (NET_UDP_WOULD_BLOCK()) {
            return 0;
        }
        return -1;
    }
    if (out_src != nullptr) {
        *out_src = sockaddr_to_net_addr(&src_addr);
    }
    return bytes_received;
}

static void net_udp_poll(void *state)
{
    (void)state;
    /* No-op: recv() already drains the kernel socket buffer directly
     * and non-blockingly. See net.h's poll doc comment. */
}

#if defined(_WIN32)

bool net_udp_create(Allocator *alloc, uint16_t bind_port, bool allow_broadcast, NetTransport *out, ErrorState *err)
{
    WSADATA wsa_data;
    int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        error_set(err, "net_udp_create: WSAStartup failed (%d)", startup_result);
        return false;
    }

    SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_handle == NET_UDP_INVALID_SOCKET) {
        error_set(err, "net_udp_create: socket() failed (%d)", WSAGetLastError());
        WSACleanup();
        return false;
    }

    u_long nonblocking = 1;
    if (ioctlsocket(socket_handle, FIONBIO, &nonblocking) != 0) {
        error_set(err, "net_udp_create: ioctlsocket(FIONBIO) failed (%d)", WSAGetLastError());
        closesocket(socket_handle);
        WSACleanup();
        return false;
    }

    net_udp_set_reuse_options(socket_handle);
    if (allow_broadcast) {
        net_udp_set_broadcast_option(socket_handle);
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(bind_port);
    if (bind(socket_handle, (const struct sockaddr *)&bind_addr, sizeof(bind_addr)) == SOCKET_ERROR) {
        error_set(err, "net_udp_create: bind(port=%u) failed (%d)", (unsigned)bind_port, WSAGetLastError());
        closesocket(socket_handle);
        WSACleanup();
        return false;
    }

    NetUdpState *state = alloc->malloc_fn(alloc->ctx, sizeof(NetUdpState));
    state->socket_handle = socket_handle;
    state->alloc = *alloc;
    *out = (NetTransport){.send = net_udp_send, .recv = net_udp_recv, .poll = net_udp_poll, .state = state};
    return true;
}

void net_udp_destroy(NetTransport *transport)
{
    if (transport == nullptr || transport->state == nullptr) {
        return;
    }
    NetUdpState *state = (NetUdpState *)transport->state;
    closesocket(state->socket_handle);
    WSACleanup();
    state->alloc.free_fn(state->alloc.ctx, state);
    transport->state = nullptr;
}

#else /* POSIX */

bool net_udp_create(Allocator *alloc, uint16_t bind_port, bool allow_broadcast, NetTransport *out, ErrorState *err)
{
    int socket_handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_handle < 0) {
        error_set(err, "net_udp_create: socket() failed: %s", strerror(errno));
        return false;
    }

    int flags = fcntl(socket_handle, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) < 0) {
        error_set(err, "net_udp_create: fcntl(O_NONBLOCK) failed: %s", strerror(errno));
        close(socket_handle);
        return false;
    }

    net_udp_set_reuse_options(socket_handle);
    if (allow_broadcast) {
        net_udp_set_broadcast_option(socket_handle);
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(bind_port);
    if (bind(socket_handle, (const struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        error_set(err, "net_udp_create: bind(port=%u) failed: %s", (unsigned)bind_port, strerror(errno));
        close(socket_handle);
        return false;
    }

    NetUdpState *state = alloc->malloc_fn(alloc->ctx, sizeof(NetUdpState));
    state->socket_handle = socket_handle;
    state->alloc = *alloc;
    *out = (NetTransport){.send = net_udp_send, .recv = net_udp_recv, .poll = net_udp_poll, .state = state};
    return true;
}

void net_udp_destroy(NetTransport *transport)
{
    if (transport == nullptr || transport->state == nullptr) {
        return;
    }
    NetUdpState *state = (NetUdpState *)transport->state;
    close(state->socket_handle);
    state->alloc.free_fn(state->alloc.ctx, state);
    transport->state = nullptr;
}

#endif
