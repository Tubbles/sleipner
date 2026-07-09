#pragma once

/* net_discovery.h -- LAN discovery beacon-send / join-list-collect logic
 * (S8.3a). Driven purely over an S8.1 NetTransport plus a delta_time, so
 * it is headless-testable against net_loopback.h's LoopbackNetwork
 * switchboard with no real sockets. Encodes/decodes wire bytes via
 * net_protocol.h's MSG_BEACON (S8.2); mutates network.h's JoinList
 * through its find/add_or_refresh/age/evict_timed_out helpers.
 *
 * The pause-menu Host/Join entries that flip NetworkState.mode and call
 * these ticks from game_update, plus the real net_udp.h socket bound to
 * DISCOVERY_PORT and the broadcast NetAddr construction, are S8.3b --
 * this slice only proves the mechanism headless over loopback. */

#include "net.h"
#include "network.h" // JoinList

#include <stdint.h>

/* Fixed UDP port every peer's discovery socket binds/broadcasts on --
 * distinct from BeaconMessage.listen_port, which is the HOST's actual
 * game-session port advertised inside the beacon payload (see network.h's
 * DiscoveredHost doc comment). Unused directly by this slice's pure tick
 * functions: the send destination is an explicit `broadcast_target`
 * parameter on discovery_host_tick, precisely so a loopback test can
 * point host straight at a client endpoint instead of a real broadcast
 * address. S8.3b's real UDP wiring binds its discovery socket to this
 * port and constructs the broadcast NetAddr from it. */
#define DISCOVERY_PORT 7777

/* Host beacons roughly this often -- not a hard real-time guarantee, just
 * the accumulator interval discovery_host_tick fires on. */
#define DISCOVERY_BEACON_INTERVAL_SECONDS 1.0F

/* A discovered host is dropped from a client's JoinList once its beacon
 * has been unseen for this long -- comfortably more than one missed
 * beacon interval, so a single dropped UDP packet doesn't flap a host in
 * and out of the list. */
#define DISCOVERY_TIMEOUT_SECONDS 3.0F

/* Accumulates delta_time into *beacon_timer; once it reaches
 * DISCOVERY_BEACON_INTERVAL_SECONDS, encodes a MSG_BEACON carrying
 * host_name/listen_port and net_sends it to broadcast_target, then
 * subtracts the interval from the accumulator (leftover time carries
 * over, so cadence doesn't drift). A no-op (silent) if the encode or send
 * fails -- there is no error channel for a fire-and-forget beacon.
 * *beacon_timer is caller-owned (NetworkState.beacon_timer in real use)
 * so callers can pause/resume hosting without losing accumulated phase. */
void discovery_host_tick(const NetTransport *transport,
                         NetAddr broadcast_target,
                         const char *host_name,
                         uint16_t listen_port,
                         float *beacon_timer,
                         float delta_time);

/* Drains every packet currently waiting on transport via net_recv. Each
 * one that decodes as a valid MSG_BEACON add-or-refreshes its sender in
 * list (network.h's join_list_add_or_refresh) -- addr is assembled from
 * the packet's source IP plus the beacon's own advertised listen_port,
 * NOT the UDP source port the beacon happened to arrive from. Anything
 * that fails to decode (wrong magic/version, truncated, wrong message
 * type) is silently ignored -- protocol_decode_packet/protocol_decode_beacon
 * already bounds-check, so a malformed packet can never corrupt list.
 * After draining, ages every entry by delta_time and evicts anything
 * whose beacon has been unseen for DISCOVERY_TIMEOUT_SECONDS. */
void discovery_client_tick(const NetTransport *transport, JoinList *list, float delta_time);
