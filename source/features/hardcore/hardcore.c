/*
HARDCORE.C

Hardcore as an extension (extension_api.h), OpenCE's rules (port/linux/game/
game_modes.c's Hardcore parts, pinned in compatibility.json). It is a flag
over any game engine and any mode, so it owns no session: its hooks are
general ones, asked of every extension while a game with the flag plays.
*/

#include "cseries.h"
#include "extensions/extension_api.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "game_modes.h"
#include "network_rules.h"
#include "hardcore_rules.h"
#include "objects/objects.h"
#include "units/units.h"

int config_boolean(char const *name);
boolean network_game_distributed_client(void);

static boolean hardcore_playing(
	void)
{
	return game_modes_hardcore();
}

static boolean hardcore_hide_motion_sensor(
	void)
{
	return hardcore_playing();
}

static boolean hardcore_suppress_aim_assist(
	void)
{
	return hardcore_playing();
}

/* the host restores the killer's health, and their shields unless an
overshield has them past full */
static void hardcore_player_killed_player(
	long killing_player_index,
	long dead_player_index,
	boolean friendly_fire)
{
	struct player_datum *killer;
	struct unit_datum *unit;

	struct hardcore_vitals vitals;

	if (!hardcore_playing() || killing_player_index == NONE)
		return;
	killer = player_try_and_get(killing_player_index);
	unit = killer && killer->unit_index != NONE ? unit_try_and_get(killer->unit_index) : NULL;
	if (!hardcore_rules_restores(TRUE, network_game_distributed_client(), friendly_fire, TRUE,
		killing_player_index == dead_player_index, unit != NULL,
		unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit)))
	{
		return;
	}
	vitals.body = unit->object.body_vitality;
	vitals.shield = unit->object.shield_vitality;
	vitals.maximum_shield = unit->object.maximum_shield_vitality;
	vitals.shield_depleted = TEST_FLAG(unit->object.damage_flags, _object_shield_depleted_bit);
	vitals.shield_charging = TEST_FLAG(unit->object.damage_flags, _object_shield_charging_bit);
	hardcore_rules_restore(&vitals);
	unit->object.body_vitality = vitals.body;
	unit->object.shield_vitality = vitals.shield;
	SET_FLAG(unit->object.damage_flags, _object_shield_depleted_bit, vitals.shield_depleted != 0);
	SET_FLAG(unit->object.damage_flags, _object_shield_charging_bit, vitals.shield_charging != 0);
}

static boolean hardcore_host_game_options(
	struct game_variant const *variant,
	struct game_variant_options *options,
	unsigned char extended[16])
{
	(void)variant;
	if (!config_boolean("hardcore.host"))
		return FALSE;
	extended[NETWORK_RULES_OPTION_HARDCORE] = 1;
	options->friendly_fire = _friendly_fire_on;
	return TRUE;
}

struct halo_extension const hardcore_extension =
{
	.name = "hardcore",
	.opence_modes = 1UL << HALO_OPENCE_HARDCORE,
	.hide_motion_sensor = hardcore_hide_motion_sensor,
	.suppress_aim_assist = hardcore_suppress_aim_assist,
	.player_killed_player = hardcore_player_killed_player,
	.host_game_options = hardcore_host_game_options,
};
