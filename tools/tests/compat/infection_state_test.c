/* What a client takes of the host's Infection state (infection_state.h):
a full state, an incremental one, and stale, duplicated, out-of-range and
malformed ones. Stand-ins for the game's types; only the validator is under test. */
#include <assert.h>
#include <string.h>

#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 8
#define GAME_ENGINE_MAXIMUM_NETWORK_STATE_SIZE 4096
#define NONE (-1)
typedef int boolean;
typedef unsigned char byte;
#define TRUE 1
#define FALSE 0

#include "network_infection.h"
#include "infection_state.h"

static struct distributed_infection_state good(long game, long revision)
{
	struct distributed_infection_state state;

	memset(&state, 0, sizeof(state));
	state.game_number = game;
	state.revision = revision;
	state.phase = _distributed_infection_active;
	state.phase_tick = 100;
	state.winner = NONE;
	state.players[0].identity = 11;
	state.players[0].role = _distributed_infection_role_zombie;
	state.players[1].identity = 12;
	state.players[1].role = _distributed_infection_role_human;
	return state;
}

int main(void)
{
	struct distributed_infection_state s;

	/* a first (full) state is taken whatever revision the client had; later ones only if not older */
	s = good(3, 5);
	assert(infection_state_valid(&s, 3, 0));
	assert(infection_state_valid(&s, 3, 5)); /* a duplicate */
	assert(infection_state_valid(&s, 3, 4)); /* an incremental */
	assert(!infection_state_valid(&s, 3, 6)); /* stale */
	assert(!infection_state_valid(&s, 4, 0)); /* another game's */
	s = good(3, -1); assert(!infection_state_valid(&s, 3, 0));
	/* phases and the winner go together */
	s = good(3, 5); s.phase = _distributed_infection_finished + 1; assert(!infection_state_valid(&s, 3, 0));
	s = good(3, 5); s.phase = -1; assert(!infection_state_valid(&s, 3, 0));
	s = good(3, 5); s.winner = 0; assert(!infection_state_valid(&s, 3, 0)); /* a winner before the end */
	s = good(3, 5); s.phase = _distributed_infection_finished; s.winner = 1; assert(infection_state_valid(&s, 3, 0));
	s.winner = 2; assert(!infection_state_valid(&s, 3, 0));
	s = good(3, 5); s.phase_tick = -1; assert(!infection_state_valid(&s, 3, 0));
	/* players */
	s = good(3, 5); s.players[0].role = 3; assert(!infection_state_valid(&s, 3, 0));
	s = good(3, 5); s.players[0].score = -1; assert(!infection_state_valid(&s, 3, 0));
	s = good(3, 5); s.players[0].score = INFECTION_MAXIMUM_SCORE + 1; assert(!infection_state_valid(&s, 3, 0));
	s = good(3, 5); s.players[0].score = INFECTION_MAXIMUM_SCORE; assert(infection_state_valid(&s, 3, 0));
	s = good(3, 5); s.players[2].role = _distributed_infection_role_human; assert(!infection_state_valid(&s, 3, 0)); /* a role with no identity */
	s = good(3, 5); s.players[0].reserved[1] = 1; assert(!infection_state_valid(&s, 3, 0));
	return 0;
}
