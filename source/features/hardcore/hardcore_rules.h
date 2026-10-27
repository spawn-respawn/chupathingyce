/* hardcore_rules.h: Hardcore's rules as decisions on plain values (OpenCE's
game_modes_player_killed), with no game dependency
(tools/tests/compat/hardcore_rules_test.c). */
#ifndef HALO_HARDCORE_RULES_H
#define HALO_HARDCORE_RULES_H

/* the host restores a killer's health and shields after a kill: not a client
(the host's word reaches it), not friendly fire, only by another player's
kill (not a suicide or the world's), of a killer with a living unit */
static inline int hardcore_rules_restores(int hardcore, int is_client, int friendly_fire, int killer_is_player,
	int killer_is_victim, int killer_has_unit, int killer_alive)
{
	return hardcore && !is_client && !friendly_fire && killer_is_player && !killer_is_victim && killer_has_unit &&
		killer_alive;
}

struct hardcore_vitals
{
	float body;
	float shield;
	float maximum_shield;
	int shield_depleted;
	int shield_charging;
};

/* health to full; shields to full unless there are none to have or an
overshield has them past full; a restored shield is no longer depleted or charging */
static inline void hardcore_rules_restore(struct hardcore_vitals *vitals)
{
	vitals->body = 1.0f;
	if (vitals->maximum_shield > 0.0f && vitals->shield < 1.0f)
	{
		vitals->shield = 1.0f;
		vitals->shield_depleted = 0;
		vitals->shield_charging = 0;
	}
}

#endif
