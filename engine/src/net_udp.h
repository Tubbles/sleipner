#pragma once

/* net_udp.h — a real, non-blocking UDP transport backing NetTransport.
 * POSIX (Linux/Android) uses BSD sockets; Windows uses winsock2. See
 * net_udp.c for the platform split. */

#include "alloc.h"
#include "error.h"
#include "net.h"

#include <stdint.h>

/* Create a non-blocking UDP socket bound to bind_port (0 = let the OS
 * pick an ephemeral port) and fill out with a NetTransport backed by
 * it. The transport's state is allocated from alloc (an arena in engine
 * use, matching every other arena-backed subsystem).
 *
 * Every socket this creates has SO_REUSEADDR (and, where the platform
 * defines it, SO_REUSEPORT) set before bind -- best effort, failures are
 * silently ignored rather than failing the whole create. This is what
 * lets S8.3b's discovery sockets (host and client both binding the same
 * fixed DISCOVERY_PORT) coexist when host and client run as two
 * processes on the SAME machine (local dev/testing, or two engine unit
 * tests in the same binary creating successive sockets on that port) --
 * on separate LAN machines it is a no-op since there is no real
 * conflict to resolve.
 *
 * allow_broadcast additionally sets SO_BROADCAST, required before
 * sendto() will accept a broadcast destination address (e.g. the LAN
 * discovery beacon's 255.255.255.255) rather than failing with EACCES.
 * Only a beaconing HOST socket needs this; a socket that only listens
 * for beacons never sends to a broadcast address. Same best-effort
 * contract as the reuse options above: this is production-only wiring a
 * headless test can't fully verify (it can't observe a packet actually
 * leaving the LAN), so a failed setsockopt here does not fail the
 * create either -- see net_udp.c's net_udp_set_broadcast_option. */
[[nodiscard]] bool
net_udp_create(Allocator *alloc, uint16_t bind_port, bool allow_broadcast, NetTransport *out, ErrorState *err);

/* Close the socket and free the transport's state via the allocator it
 * was created with (a no-op for arena-backed state, consistent with
 * str_free/vec_free; a real free for heap-backed test allocators).
 * Clears transport->state, so a stray net_send/net_recv after destroy is
 * a safe no-op. */
void net_udp_destroy(NetTransport *transport);
