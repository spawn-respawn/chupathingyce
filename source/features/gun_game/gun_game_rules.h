/* gun_game_rules.h: Gun Game's rules as decisions on plain numbers (OpenCE's
gun_game.c, which this is taken from), with no game dependency, so a test can
check them against OpenCE's contract (tools/tests/compat/gun_game_rules_test.c).
gun_game.c asks these; it does the game's part (objects, units, scores). */
#ifndef HALO_GUN_GAME_RULES_H
#define HALO_GUN_GAME_RULES_H

/* the weapon orders (game_engine.h's _gun_game_order_*) */
#define GUN_GAME_ORDER_STRONGEST_FIRST 0
#define GUN_GAME_ORDER_WEAKEST_FIRST 1
#define GUN_GAME_ORDER_RANDOM 2

/* a player's level: their Slayer score (kills less suicides), within the list */
static inline short gun_game_rules_level(long score, short level_count)
{
	long last = level_count > 0 ? level_count - 1 : 0;

	return (short)(score < 0 ? 0 : score > last ? last : score);
}

/* the game is won at the score of the list's length (Slayer's score to win:
a kill with the last weapon) */
static inline long gun_game_rules_score_to_win(short level_count)
{
	return level_count;
}

/* the map's list, in the gametype's order: slots of the strongest-first list,
those the map has (available[slot]) in order, the weakest first reversing
them; each slot is written to levels (as its index in the list), returning
how many. */
static inline short gun_game_rules_build_levels(short order, short slot_count, unsigned char const *available,
	unsigned char *levels)
{
	short count = 0;
	short index;

	for (index = 0; index < slot_count; index++)
	{
		short slot = order == GUN_GAME_ORDER_WEAKEST_FIRST ? (short)(slot_count - 1 - index) : index;

		if (available[slot])
			levels[count++] = (unsigned char)slot;
	}
	return count;
}

/* the host's draw for a random order: a Fisher-Yates shuffle, from the back,
with random_range(context, low, high) in [low, high) */
static inline void gun_game_rules_shuffle(unsigned char *levels, short count,
	long (*random_range)(void *context, long low, long high), void *context)
{
	short index;

	for (index = (short)(count - 1); index > 0; index--)
	{
		short other = (short)random_range(context, 0, index + 1);
		unsigned char swap = levels[index];

		levels[index] = levels[other];
		levels[other] = swap;
	}
}

/* the score change a kill makes to the victim: -1 for a melee kill by another
player that is not friendly fire, with demotion on and a score above 0; else 0 */
static inline int gun_game_rules_demotion(int melee_demotes, int melee, int friendly_fire, int killer_is_player,
	int killer_is_victim, long victim_score)
{
	return melee_demotes && melee && !friendly_fire && killer_is_player && !killer_is_victim && victim_score > 0 ? -1 : 0;
}

/* a suicide at the first weapon leaves the score at 0 (a score under it is
put back by one) */
static inline int gun_game_rules_floor(long victim_score)
{
	return victim_score < 0 ? 1 : 0;
}

/* a weapon with no rounds left (none loaded nor carried in any magazine), or
a battery spent (a weapon with no magazines: its age at 1.0 or more) */
static inline int gun_game_rules_weapon_empty(int magazine_count, float age, short const *rounds_loaded,
	short const *rounds_total)
{
	int index;

	if (magazine_count <= 0)
		return age >= 1.0f;
	for (index = 0; index < magazine_count; index++)
	{
		if (rounds_loaded[index] > 0 || rounds_total[index] > 0)
			return 0;
	}
	return 1;
}

/* whether the host replaces a living player's weapon: when they have not been
given this level's (another unit, or another level), or the one in hand is empty */
static inline int gun_game_rules_needs_weapon(long equipped_unit, short equipped_level, long unit, short level,
	int has_current_weapon, int current_empty)
{
	if (equipped_unit == unit && equipped_level == level)
		return !has_current_weapon || current_empty;
	return 1;
}

/* only the weapons given (and one held, which readying asks of) may be picked up */
static inline int gun_game_rules_allow_pick_up(int equipping, int holds_it)
{
	return equipping || holds_it;
}

#endif
