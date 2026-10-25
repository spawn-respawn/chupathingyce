/* infection_state.h: what a client takes of the host's Infection state
(infection.c's infection_read_network_state). Kept apart, with no game
dependency, so a test can feed it full, stale, duplicated and malformed
states (tools/tests/compat/infection_state_test.c). Needs
network_infection.h's types and HALO_PORT_MAXIMUM_NETWORK_PLAYERS. */
#ifndef HALO_INFECTION_STATE_H
#define HALO_INFECTION_STATE_H

/* a score past this is not a client's to take */
#define INFECTION_MAXIMUM_SCORE 1000000

/* a state a client may take: of this game, no older than its own, every
value in range */
static inline boolean infection_state_valid(
	struct distributed_infection_state const *state,
	long game_number,
	long revision)
{
	short index;

	if (state->game_number != game_number || state->revision < revision || state->revision < 0 ||
		state->phase < _distributed_infection_waiting || state->phase > _distributed_infection_finished ||
		state->phase_tick < 0 || state->winner < NONE || state->winner > 1 ||
		(state->phase != _distributed_infection_finished && state->winner != NONE))
	{
		return FALSE;
	}
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct distributed_infection_player const *player = &state->players[index];

		if (player->role > _distributed_infection_role_zombie || player->score < 0 ||
			player->score > INFECTION_MAXIMUM_SCORE || (player->role && !player->identity) ||
			player->reserved[0] || player->reserved[1] || player->reserved[2])
		{
			return FALSE;
		}
	}
	return TRUE;
}

#endif
