#include "net_loopback.h"

#include "alloc.h"
#include "net.h"
#include "vec.h"

#include <string.h>

VEC_IMPL(loopback_endpoint_ptr, LoopbackEndpoint *)

static LoopbackEndpoint *find_endpoint(const LoopbackNetwork *network, NetAddr addr)
{
    for (int index = 0; index < network->endpoints.count; index++) {
        if (net_addr_eq(network->endpoints.data[index]->addr, addr)) {
            return network->endpoints.data[index];
        }
    }
    return nullptr;
}

/* Queue a packet into inbox. Tail-drops the INCOMING packet when the
 * inbox is already full -- the already-queued (older) packets are kept,
 * mirroring how a real UDP receive buffer silently discards datagrams
 * under congestion. The sender is never told, matching send()'s
 * fire-and-forget contract below. */
static void inbox_push(LoopbackInbox *inbox, NetAddr src, const void *data, size_t len)
{
    if (inbox->count >= LOOPBACK_INBOX_CAPACITY) {
        return;
    }
    size_t tail = (inbox->head + inbox->count) % LOOPBACK_INBOX_CAPACITY;
    LoopbackPacket *slot = &inbox->slots[tail];
    slot->src = src;
    slot->len = len;
    memcpy(slot->data, data, len);
    inbox->count++;
}

static int inbox_pop(LoopbackInbox *inbox, NetAddr *out_src, void *buffer, size_t cap)
{
    if (inbox->count == 0) {
        return 0;
    }
    LoopbackPacket *slot = &inbox->slots[inbox->head];
    size_t copy_len = slot->len < cap ? slot->len : cap;
    memcpy(buffer, slot->data, copy_len);
    if (out_src != nullptr) {
        *out_src = slot->src;
    }
    inbox->head = (inbox->head + 1) % LOOPBACK_INBOX_CAPACITY;
    inbox->count--;
    return (int)copy_len;
}

static int loopback_send(void *state, NetAddr dest, const void *data, size_t len)
{
    if (len > NET_MAX_PACKET_SIZE) {
        return -1;
    }
    LoopbackEndpoint *self = (LoopbackEndpoint *)state;
    LoopbackEndpoint *dest_endpoint = find_endpoint(self->network, dest);
    if (dest_endpoint == nullptr) {
        /* No such endpoint on this network -- silently dropped, mirroring
         * a real UDP send to an address nobody is listening on. */
        return (int)len;
    }
    inbox_push(&dest_endpoint->inbox, self->addr, data, len);
    return (int)len;
}

static int loopback_recv(void *state, NetAddr *out_src, void *buffer, size_t cap)
{
    LoopbackEndpoint *self = (LoopbackEndpoint *)state;
    return inbox_pop(&self->inbox, out_src, buffer, cap);
}

static void loopback_poll(void *state)
{
    (void)state;
    /* No-op: send() routes into the destination's inbox immediately, so
     * there is nothing to pump. See net.h's poll doc comment. */
}

void loopback_network_init(LoopbackNetwork *network, Allocator *alloc)
{
    network->alloc = *alloc;
    network->endpoints = vec_loopback_endpoint_ptr_new(*alloc);
}

bool loopback_transport_create(LoopbackNetwork *network, NetAddr my_addr, NetTransport *out)
{
    if (find_endpoint(network, my_addr) != nullptr) {
        return false;
    }

    LoopbackEndpoint *endpoint = network->alloc.malloc_fn(network->alloc.ctx, sizeof(LoopbackEndpoint));
    *endpoint = (LoopbackEndpoint){.addr = my_addr, .network = network};
    if (!vec_loopback_endpoint_ptr_push(&network->endpoints, endpoint)) {
        return false;
    }

    *out = (NetTransport){
        .send = loopback_send,
        .recv = loopback_recv,
        .poll = loopback_poll,
        .state = endpoint,
    };
    return true;
}

void loopback_network_free(LoopbackNetwork *network)
{
    for (int index = 0; index < network->endpoints.count; index++) {
        network->alloc.free_fn(network->alloc.ctx, network->endpoints.data[index]);
    }
    vec_loopback_endpoint_ptr_free(&network->endpoints);
}
