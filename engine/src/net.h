#pragma once

/* net.h — the network transport abstraction (S8.1). Platform-agnostic:
 * no sockets, no platform headers here. Two concrete implementations
 * back this ops struct:
 *   net_udp.h      — a real non-blocking UDP socket (POSIX + winsock2).
 *   net_loopback.h — an in-memory switchboard for headless tests.
 *
 * Both fill an identical NetTransport, so protocol code (S8.2 onward) is
 * written once against send/recv/poll and never touches a socket or a
 * loopback inbox directly. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Maximum single packet size, in bytes. Chosen below the common LAN/
 * Ethernet MTU (1500 bytes) minus IPv4 + UDP headers, so a packet built
 * to this bound never fragments at the IP layer. Both transports use it
 * to size fixed receive buffers -- packets are bounded by protocol
 * design (S8.2's header carries a length field), never unbounded, so a
 * fixed size here is a justified bound, not a MAX_* smell. */
#define NET_MAX_PACKET_SIZE 1400

/* An endpoint identifier usable by both transports.
 *
 * host is an IPv4 address in HOST byte order -- net_udp converts to/from
 * network byte order at the socket boundary (htonl/ntohl), so every
 * caller of this header reasons about host/port in host order only.
 * net_loopback treats (host, port) purely as an opaque endpoint id with
 * no networking meaning; callers pick any distinct pair per endpoint
 * (e.g. host=1 for the host, host=2.. for clients). */
typedef struct {
    uint32_t host;
    uint16_t port;
} NetAddr;

/* Build a NetAddr from a host (IPv4, host byte order) and port. */
static inline NetAddr net_addr_make(uint32_t host, uint16_t port)
{
    return (NetAddr){.host = host, .port = port};
}

/* Parse a dotted-decimal "a.b.c.d" IPv4 string plus a port into a
 * NetAddr. Returns false if the string is not exactly four 0-255
 * decimal octets separated by '.'. Used by net_udp callers targeting a
 * real peer; net_loopback callers construct a NetAddr directly via
 * net_addr_make since its host/port carry no real address meaning. */
[[nodiscard]] bool net_addr_from_ipv4_string(const char *ipv4_string, uint16_t port, NetAddr *out);

static inline bool net_addr_eq(NetAddr lhs, NetAddr rhs)
{
    return lhs.host == rhs.host && lhs.port == rhs.port;
}

/* Ops struct (Allocator-style: function pointers + opaque state)
 * abstracting the wire. recv is NON-BLOCKING: it returns 0 promptly when
 * no packet is waiting rather than blocking the caller. */
typedef struct {
    /* Returns bytes sent, or <0 on error. */
    int (*send)(void *state, NetAddr dest, const void *data, size_t len);
    /* Returns bytes received, 0 if nothing is waiting (non-blocking), or
     * <0 on error. */
    int (*recv)(void *state, NetAddr *out_src, void *buffer, size_t cap);
    /* Pump/advance the transport. net_udp: no-op (recv already drains
     * the kernel socket buffer directly). net_loopback: no-op (send
     * routes into the destination's inbox immediately). */
    void (*poll)(void *state);
    void *state;
} NetTransport;

/* Wrapper functions: null-op-safe. A null transport, or a transport with
 * a null ops entry, behaves as if nothing happened rather than crashing. */

static inline int net_send(const NetTransport *transport, NetAddr dest, const void *data, size_t len)
{
    if (transport == nullptr || transport->send == nullptr) {
        return 0;
    }
    return transport->send(transport->state, dest, data, len);
}

static inline int net_recv(const NetTransport *transport, NetAddr *out_src, void *buffer, size_t cap)
{
    if (transport == nullptr || transport->recv == nullptr) {
        return 0;
    }
    return transport->recv(transport->state, out_src, buffer, cap);
}

static inline void net_poll(const NetTransport *transport)
{
    if (transport == nullptr || transport->poll == nullptr) {
        return;
    }
    transport->poll(transport->state);
}
