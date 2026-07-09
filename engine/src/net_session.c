#include "net_session.h"

#include "game.h"
#include "input.h"
#include "network.h"

void network_host_tick(GameState *state)
{
    if (state->network.mode != NET_HOSTING) {
        return;
    }
    network_host_receive(&state->network, state->gamedata_hash);
}

void network_client_tick(GameState *state, const InputState *local_input)
{
    if (state->network.mode == NET_JOINING) {
        network_client_send_join(&state->network, state->gamedata_hash);
        state->network.mode = NET_CLIENT;
    }
    if (state->network.mode == NET_CLIENT) {
        network_client_send_input(&state->network, local_input);
    }
}
