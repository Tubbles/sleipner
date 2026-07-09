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
 * use, matching every other arena-backed subsystem). */
[[nodiscard]] bool net_udp_create(Allocator *alloc, uint16_t bind_port, NetTransport *out, ErrorState *err);

/* Close the socket and free the transport's state via the allocator it
 * was created with (a no-op for arena-backed state, consistent with
 * str_free/vec_free; a real free for heap-backed test allocators).
 * Clears transport->state, so a stray net_send/net_recv after destroy is
 * a safe no-op. */
void net_udp_destroy(NetTransport *transport);
