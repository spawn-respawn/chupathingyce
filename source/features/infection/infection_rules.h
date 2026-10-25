/* infection_rules.h: Infection's rules as decisions on plain numbers (OpenCE's
infection.c, which this is taken from), with no game dependency
(tools/tests/compat/infection_rules_test.c). infection.c does the game's part. */
#ifndef HALO_INFECTION_RULES_H
#define HALO_INFECTION_RULES_H

#define INFECTION_RULES_HUMAN 1
#define INFECTION_RULES_ZOMBIE 2

/* the round's result: 0 the humans won, 1 the zombies, 2 no one (everyone
left), -1 still on. Turning the last human wins over the time running out on
the same tick. */
static inline long infection_rules_round_result(long humans, long zombies, long elapsed, long limit)
{
	if (!humans && !zombies)
		return 2;
	if (!humans)
		return 1;
	if (!zombies || (limit > 0 && elapsed >= limit))
		return 0;
	return -1;
}

/* the first zombies: the option's, leaving at least one human */
static inline long infection_rules_initial_zombies(long players, long option)
{
	long most = players - 1;

	return most < option ? (most < 0 ? 0 : most) : option;
}

struct infection_kill
{
	/* the killer scores a kill of the other side */
	int killer_scores;
	/* the victim is a zombie after it */
	int victim_turns;
};

/* a kill: the killer scores only against the other side (not themselves, not a
teammate, not past the score limit); a human killed by a zombie, by themselves
or by the world turns, one killed by a human teammate (friendly fire) does not;
a zombie stays one. killer_role 0: no killing player. */
static inline struct infection_kill infection_rules_kill(int victim_role, int killer_role, int killer_is_victim,
	int friendly_fire, long killer_score, long maximum_score)
{
	struct infection_kill kill;

	kill.killer_scores = killer_role && !killer_is_victim && killer_role != victim_role && killer_score < maximum_score;
	kill.victim_turns = victim_role == INFECTION_RULES_HUMAN &&
		!(friendly_fire && killer_role && !killer_is_victim && killer_role == victim_role);
	return kill;
}

/* the time left shown: the whole limit until the round is active, then the limit less the time since it began */
static inline long infection_rules_remaining(int active, long limit, long elapsed)
{
	long left = limit - elapsed;

	return active ? (left > 0 ? left : 0) : limit;
}

/* whether a player won: zombies are winner 1, humans 0 */
static inline long infection_rules_did_win(int role, long winner)
{
	if (winner < 0 || !role)
		return -1;
	return (role == INFECTION_RULES_ZOMBIE ? 1 : 0) == winner;
}

/* the melee factor of a zombie holding its skull in an active round, else 1;
percent is the option */
static inline float infection_rules_melee_multiplier(int zombie, int active, int holds_skull, int percent)
{
	if (!zombie || !active || !holds_skull)
		return 1.0f;
	return percent / 100.0f;
}

/* a zombie's speed, a human's 1: percent is the option */
static inline float infection_rules_speed(int zombie, int percent)
{
	return zombie ? percent / 100.0f : 1.0f;
}

#endif
