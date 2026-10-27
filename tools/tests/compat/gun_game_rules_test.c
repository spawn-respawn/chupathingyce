/* Gun Game's rules against OpenCE's contract (gun_game_rules.h): progression
by Slayer score, the list's order and length, the win, demotion, the floor at
level 0, refills and pickups. */
#include <assert.h>
#include <string.h>
#include "gun_game_rules.h"

static long counter_random(void *context, long low, long high)
{
	long *state = context;

	*state = (*state * 1103515245L + 12345L) & 0x7FFFFFFF;
	return low + (*state >> 8) % (high - low);
}

int main(void)
{
	unsigned char all8[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
	unsigned char levels[8];
	short count, index;

	/* level is the Slayer score, clamped to the list */
	assert(gun_game_rules_level(0, 8) == 0 && gun_game_rules_level(3, 8) == 3 && gun_game_rules_level(7, 8) == 7);
	assert(gun_game_rules_level(8, 8) == 7 && gun_game_rules_level(500, 8) == 7);
	assert(gun_game_rules_level(-1, 8) == 0 && gun_game_rules_level(-50, 8) == 0);
	assert(gun_game_rules_level(5, 0) == 0); /* no list: level 0 */
	/* the win is a kill with the last weapon: score to win is the list's length */
	assert(gun_game_rules_score_to_win(8) == 8 && gun_game_rules_score_to_win(6) == 6);

	/* strongest first: the list as it stands; weakest first: reversed */
	count = gun_game_rules_build_levels(GUN_GAME_ORDER_STRONGEST_FIRST, 8, all8, levels);
	assert(count == 8);
	for (index = 0; index < 8; index++)
		assert(levels[index] == index);
	count = gun_game_rules_build_levels(GUN_GAME_ORDER_WEAKEST_FIRST, 8, all8, levels);
	assert(count == 8);
	for (index = 0; index < 8; index++)
		assert(levels[index] == 7 - index);

	/* a map missing weapons (slot 2 and 5): same count and win condition on every machine */
	{
		unsigned char missing[8] = { 1, 1, 0, 1, 1, 0, 1, 1 };

		count = gun_game_rules_build_levels(GUN_GAME_ORDER_STRONGEST_FIRST, 8, missing, levels);
		assert(count == 6 && levels[0] == 0 && levels[1] == 1 && levels[2] == 3 && levels[3] == 4 && levels[4] == 6 &&
			levels[5] == 7);
		assert(gun_game_rules_score_to_win(count) == 6);
		count = gun_game_rules_build_levels(GUN_GAME_ORDER_WEAKEST_FIRST, 8, missing, levels);
		assert(count == 6 && levels[0] == 7 && levels[5] == 0);
		/* the order a client builds (random is host-drawn; a client keeps the base order and its weapons come from the host) */
		count = gun_game_rules_build_levels(GUN_GAME_ORDER_RANDOM, 8, missing, levels);
		assert(count == 6 && levels[0] == 0 && levels[5] == 7);
	}
	{
		unsigned char none[8] = { 0 };

		assert(gun_game_rules_build_levels(GUN_GAME_ORDER_STRONGEST_FIRST, 8, none, levels) == 0);
	}

	/* the random draw is a permutation of the same weapons, whatever the seed */
	{
		long seed;

		for (seed = 1; seed < 200; seed++)
		{
			long state = seed;
			unsigned char seen[8] = { 0 };

			gun_game_rules_build_levels(GUN_GAME_ORDER_RANDOM, 8, all8, levels);
			gun_game_rules_shuffle(levels, 8, counter_random, &state);
			for (index = 0; index < 8; index++)
			{
				assert(levels[index] < 8 && !seen[levels[index]]);
				seen[levels[index]] = 1;
			}
		}
		/* one weapon, nothing to draw */
		{
			unsigned char one = 3;
			long state = 1;

			gun_game_rules_shuffle(&one, 1, counter_random, &state);
			assert(one == 3);
		}
	}

	/* a melee kill takes the victim back one weapon, once, when enabled and not friendly fire */
	assert(gun_game_rules_demotion(1, 1, 0, 1, 0, 3) == -1);
	assert(gun_game_rules_demotion(0, 1, 0, 1, 0, 3) == 0);   /* demotion off */
	assert(gun_game_rules_demotion(1, 0, 0, 1, 0, 3) == 0);   /* not a melee */
	assert(gun_game_rules_demotion(1, 1, 1, 1, 0, 3) == 0);   /* friendly fire */
	assert(gun_game_rules_demotion(1, 1, 0, 0, 0, 3) == 0);   /* no killing player (the world) */
	assert(gun_game_rules_demotion(1, 1, 0, 1, 1, 3) == 0);   /* a suicide by melee */
	assert(gun_game_rules_demotion(1, 1, 0, 1, 0, 0) == 0);   /* already at level 0 */
	assert(gun_game_rules_demotion(1, 1, 0, 1, 0, -1) == 0);
	/* suicides cannot push the victim below level 0: Slayer's -1 at 0 is put back */
	assert(gun_game_rules_floor(-1) == 1 && gun_game_rules_floor(0) == 0 && gun_game_rules_floor(4) == 0);
	{
		/* a suicide at score 0: Slayer makes it -1, the floor restores 0 */
		long score = 0 - 1;

		score += gun_game_rules_floor(score);
		assert(score == 0 && gun_game_rules_level(score, 8) == 0);
		/* a melee demotion at 1 gives 0, and the floor leaves it */
		score = 1;
		score += gun_game_rules_demotion(1, 1, 0, 1, 0, score);
		score += gun_game_rules_floor(score);
		assert(score == 0);
		/* a second demotion at 0 does nothing: exactly once */
		score += gun_game_rules_demotion(1, 1, 0, 1, 0, score);
		assert(score == 0);
	}

	/* empty weapons: no rounds loaded nor carried in any magazine; or a spent battery */
	{
		short loaded[2] = { 0, 0 }, total[2] = { 0, 0 };

		assert(gun_game_rules_weapon_empty(2, 0.f, loaded, total));
		loaded[1] = 1;
		assert(!gun_game_rules_weapon_empty(2, 0.f, loaded, total));
		loaded[1] = 0; total[0] = 5;
		assert(!gun_game_rules_weapon_empty(2, 0.f, loaded, total));
		assert(gun_game_rules_weapon_empty(0, 1.0f, loaded, total) && gun_game_rules_weapon_empty(0, 1.5f, loaded, total));
		assert(!gun_game_rules_weapon_empty(0, 0.99f, loaded, total));
	}
	/* the host replaces a weapon on a new unit, a new level, when none is held or the one held is empty */
	assert(gun_game_rules_needs_weapon(-1, -1, 10, 0, 0, 0));      /* just spawned */
	assert(gun_game_rules_needs_weapon(10, 0, 11, 0, 1, 0));       /* a new unit */
	assert(gun_game_rules_needs_weapon(10, 0, 10, 1, 1, 0));       /* a new level */
	assert(!gun_game_rules_needs_weapon(10, 2, 10, 2, 1, 0));      /* same, with rounds */
	assert(gun_game_rules_needs_weapon(10, 2, 10, 2, 1, 1));       /* emptied: a full one */
	assert(gun_game_rules_needs_weapon(10, 2, 10, 2, 0, 0));       /* none in hand */
	/* ordinary pickups stay blocked; only the host's giving and a weapon already held pass */
	assert(!gun_game_rules_allow_pick_up(0, 0));
	assert(gun_game_rules_allow_pick_up(1, 0) && gun_game_rules_allow_pick_up(0, 1));
	return 0;
}
