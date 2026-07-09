#pragma once

/* net_session.h -- host-authoritative session tick hooks (S8.4a): the
 * per-frame glue between NetworkState (network.h's JOIN/INPUT primitives)
 * and GameState. Split out from network.h/network.c because network.h is
 * deliberately decoupled from game.h (game.h includes network.h for
 * GameState.network, so the reverse include would cycle -- see network.h's
 * own top doc comment); net_session.c is the file that's allowed to know
 * about both.
 *
 * Both ticks are self-guarding on state->network.mode, mirroring game.c's
 * tick_network (S8.3b): a caller can call both unconditionally every
 * frame and each is a no-op under any mode it doesn't apply to. frame.c's
 * run_active_frame calls both, unconditionally of state->editor_mode, right
 * before game_update -- same "doesn't care whether the player happens to be
 * in the level editor" reasoning tick_network's own doc comment gives.
 * Exposed here (rather than file-local to frame.c) specifically so a
 * headless test can drive a HOST GameState's and a CLIENT GameState's
 * session ticks directly against a net_loopback.h transport, without
 * needing the full frame_update/menu apparatus. */

#include "game.h"
#include "input.h"

/* HOSTING only: network_host_receive (network.h) drains every pending
 * JOIN/INPUT packet on state->network.transport, hash-verifying JOINs
 * against state->gamedata_hash and storing the latest InputState for
 * every already-registered client. Call BEFORE game_update so this tick's
 * behavior dispatch (game.c's input_for_entity) sees the freshest input a
 * client sent. No-op under any mode other than NET_HOSTING. */
void network_host_tick(GameState *state);

/* JOINING or NET_CLIENT only. NET_JOINING: sends this session's MSG_JOIN
 * (network_client_send_join, state->gamedata_hash) and advances
 * state->network.mode to NET_CLIENT in the same call -- S8.4a's implicit
 * acceptance model, see NetMode's doc comment (network.h) for why there is
 * no accept/reject reply yet. NET_CLIENT (including the NET_CLIENT this
 * call itself just entered): sends local_input as this tick's MSG_INPUT
 * (network_client_send_input). No-op under any other mode. */
void network_client_tick(GameState *state, const InputState *local_input);
