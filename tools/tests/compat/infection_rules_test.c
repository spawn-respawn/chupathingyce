/* Infection's rules against OpenCE's contract (infection_rules.h): the round's
result, the first zombies, kills and infection, the clock, winners, the skull's
melee and the zombies' speed. */
#include <assert.h>
#include "infection_rules.h"

#define H INFECTION_RULES_HUMAN
#define Z INFECTION_RULES_ZOMBIE

int main(void)
{
	struct infection_kill k;

	/* the round: zombies win when no human is left, humans when the time runs out */
	assert(infection_rules_round_result(3, 1, 100, 9000) == -1);   /* still on */
	assert(infection_rules_round_result(0, 4, 100, 9000) == 1);    /* no human left */
	assert(infection_rules_round_result(2, 2, 9000, 9000) == 0);   /* time out: humans */
	assert(infection_rules_round_result(2, 2, 8999, 9000) == -1);
	assert(infection_rules_round_result(2, 0, 10, 9000) == 0);     /* every zombie left */
	assert(infection_rules_round_result(0, 0, 10, 9000) == 2);     /* everyone left */
	assert(infection_rules_round_result(2, 2, 999999, 0) == -1);   /* no time limit: never times out */
	/* turning the last human wins over the time running out on the same tick */
	assert(infection_rules_round_result(0, 3, 9000, 9000) == 1);

	/* the first zombies: the option, leaving a human */
	assert(infection_rules_initial_zombies(8, 1) == 1 && infection_rules_initial_zombies(8, 4) == 4);
	assert(infection_rules_initial_zombies(3, 16) == 2);   /* a human is always left */
	assert(infection_rules_initial_zombies(2, 2) == 1);
	assert(infection_rules_initial_zombies(1, 1) == 0 && infection_rules_initial_zombies(0, 1) == 0);

	/* a human killed by a zombie turns, and the zombie scores */
	k = infection_rules_kill(H, Z, 0, 0, 5, 1000000);
	assert(k.killer_scores && k.victim_turns);
	/* by themselves or the world: turns, nobody scores */
	k = infection_rules_kill(H, H, 1, 0, 5, 1000000);
	assert(!k.killer_scores && k.victim_turns);
	k = infection_rules_kill(H, 0, 0, 0, 0, 1000000);
	assert(!k.killer_scores && k.victim_turns);
	/* by a human teammate (friendly fire): does not turn, no score */
	k = infection_rules_kill(H, H, 0, 1, 5, 1000000);
	assert(!k.killer_scores && !k.victim_turns);
	/* by a human, not friendly fire (a team kill with the penalty off): turns, but a kill of the same side scores nothing */
	k = infection_rules_kill(H, H, 0, 0, 5, 1000000);
	assert(!k.killer_scores && k.victim_turns);
	/* a human kills a zombie: scores, the zombie stays one */
	k = infection_rules_kill(Z, H, 0, 0, 0, 1000000);
	assert(k.killer_scores && !k.victim_turns);
	/* a zombie killed by a zombie, or itself: neither */
	k = infection_rules_kill(Z, Z, 0, 1, 0, 1000000);
	assert(!k.killer_scores && !k.victim_turns);
	k = infection_rules_kill(Z, Z, 1, 0, 0, 1000000);
	assert(!k.killer_scores && !k.victim_turns);
	/* a score is capped (what a client takes of the state: infection_state.h) */
	k = infection_rules_kill(H, Z, 0, 0, 1000000, 1000000);
	assert(!k.killer_scores && k.victim_turns);

	/* the clock: the whole limit before the round is active, then counting down to 0 */
	assert(infection_rules_remaining(0, 9000, 500) == 9000);
	assert(infection_rules_remaining(1, 9000, 500) == 8500);
	assert(infection_rules_remaining(1, 9000, 9000) == 0 && infection_rules_remaining(1, 9000, 12000) == 0);

	/* winners: zombies are winner 1, humans 0; no winner, or no role: no answer */
	assert(infection_rules_did_win(Z, 1) == 1 && infection_rules_did_win(Z, 0) == 0);
	assert(infection_rules_did_win(H, 0) == 1 && infection_rules_did_win(H, 1) == 0);
	assert(infection_rules_did_win(H, -1) == -1 && infection_rules_did_win(0, 1) == -1);

	/* the skull's melee only for an active zombie holding it; the option's percent */
	assert(infection_rules_melee_multiplier(1, 1, 1, 200) == 2.0f);
	assert(infection_rules_melee_multiplier(1, 1, 1, 100) == 1.0f && infection_rules_melee_multiplier(1, 1, 1, 250) == 2.5f);
	assert(infection_rules_melee_multiplier(0, 1, 1, 200) == 1.0f);   /* a human */
	assert(infection_rules_melee_multiplier(1, 0, 1, 200) == 1.0f);   /* not active */
	assert(infection_rules_melee_multiplier(1, 1, 0, 200) == 1.0f);   /* no skull */
	/* the zombies' speed: the option's percent; humans' 1 */
	assert(infection_rules_speed(1, 150) == 1.5f && infection_rules_speed(1, 200) == 2.0f && infection_rules_speed(0, 150) == 1.0f);
	return 0;
}
