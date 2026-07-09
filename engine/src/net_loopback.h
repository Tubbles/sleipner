#pragma once

/* net_loopback.h — an in-memory NetTransport for headless multiplayer
 * tests (and, later, single-process host+client dev runs). A
 * LoopbackNetwork is a switchboard: any number of endpoints join it via
 * loopback_transport_create, each keyed by its own NetAddr. send() copies
 * the packet straight into the DESTINATION endpoint's inbox; recv()
 * drains the CALLING endpoint's own inbox. Routing is immediate (there is
 * no queue between a transport and the switchboard itself) -- poll() is
 * a no-op.
 *
 * Endpoints are individually allocated and referenced by pointer from
 * the network's vec, so the vec can grow/reallocate on later joins
 * without invalidating any NetTransport already handed out by
 * loopback_transport_create -- see "Vec growth and pointer stability" in
 * CLAUDE.md. The number of endpoints (host + N clients) is bounded by
 * how many times loopback_transport_create is called, a setup-time
 * count rather than a per-frame one.
 *
 * Per-endpoint inboxes are a fixed-cap ring buffer (LOOPBACK_INBOX_CAPACITY
 * slots of NET_MAX_PACKET_SIZE bytes each) -- bounded, no unbounded
 * per-packet growth. When an inbox is full, send() tail-drops the
 * INCOMING packet (the oldest queued packets are kept); see net_loopback.c. */

#include "alloc.h"
#include "net.h"
#include "vec.h"

#include <stdbool.h>
#include <stddef.h>

#define LOOPBACK_INBOX_CAPACITY 32

typedef struct {
    NetAddr src;
    unsigned char data[NET_MAX_PACKET_SIZE];
    size_t len;
} LoopbackPacket;

typedef struct {
    LoopbackPacket slots[LOOPBACK_INBOX_CAPACITY];
    size_t head;  /* index of the oldest queued packet */
    size_t count; /* number of packets currently queued, <= LOOPBACK_INBOX_CAPACITY */
} LoopbackInbox;

/* Forward-declared so LoopbackEndpoint can carry a back-pointer to the
 * switchboard it belongs to (send() needs it to look up the destination
 * endpoint); LoopbackNetwork is fully defined further down in this same
 * file, so this is not the cross-module opaque handle CLAUDE.md's
 * no-forward-decl rule forbids -- see tools/cppcheck/no_forward_decl.py,
 * which only flags a forward declaration when the type is defined in a
 * *different* file. */
typedef struct LoopbackNetwork LoopbackNetwork;

typedef struct {
    NetAddr addr;
    LoopbackInbox inbox;
    LoopbackNetwork *network;
} LoopbackEndpoint;

VEC_DECL(loopback_endpoint_ptr, LoopbackEndpoint *)

struct LoopbackNetwork {
    vec_loopback_endpoint_ptr endpoints;
    Allocator alloc;
};

/* Initialize an empty switchboard. alloc backs every endpoint allocation
 * (arena in engine use); endpoints join later via loopback_transport_create. */
void loopback_network_init(LoopbackNetwork *network, Allocator *alloc);

/* Register a new endpoint at my_addr and fill out with a NetTransport
 * routed through network. Returns false if my_addr is already registered
 * on this network -- each address must be unique within one switchboard. */
[[nodiscard]] bool loopback_transport_create(LoopbackNetwork *network, NetAddr my_addr, NetTransport *out);

/* Free every endpoint plus the endpoint vec itself. A no-op for
 * arena-backed networks (the arena reclaims on rewind/reset); real
 * cleanup for heap-backed allocators, e.g. in tests -- mirrors
 * str_free/vec_free's role for arena-backed Str/vec. Any NetTransport
 * handed out by loopback_transport_create becomes invalid afterward. */
void loopback_network_free(LoopbackNetwork *network);
