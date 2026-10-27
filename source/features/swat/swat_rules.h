/* swat_rules.h: SWAT's rules as decisions on plain values (OpenCE's SWAT:
game_modes.c's game_modes_headshot_kills and damage.c's classification of the
shots that count), with no game dependency (tools/tests/compat/swat_rules_test.c). */
#ifndef HALO_SWAT_RULES_H
#define HALO_SWAT_RULES_H

/* a hit on a head kills at once only if it is a shot (a bullet, sniper round,
plasma bolt, needle or shotgun pellet: not a melee, grenade or vehicle) and
the one hit is a player's biped */
static inline int swat_rules_headshot_kills(int shot, int target_is_player_biped)
{
	return shot && target_is_player_biped;
}

/* the damage categories that are shots (damage.c passes whether the category
is one of them) */
#define SWAT_SHOT_CATEGORY_COUNT 5

#endif
