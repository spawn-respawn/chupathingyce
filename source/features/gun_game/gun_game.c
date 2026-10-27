/*
GUN_GAME.C

Gun Game, a mode of free for all Slayer (port/linux/game/game_modes.c), as
an extension (extension_api.h) of this fork, adapted from OpenCE's
port/linux/game/gun_game.c (pinned in compatibility.json). Where it differs:
the options are game_variant_extended_options (OpenCE's 0x1C to 0x2B), the
engine is selected by this extension's ruleset and the spawn weapon is its
postspawn_player; the gameplay is upstream's, deliberately. Everyone starts with
the first weapon of a list and nothing else; each kill gives the killer the
next, and a kill with the last wins. A player's level is their Slayer score
(the kills less the suicides), so the scores the host sends its clients
(game_engine_slayer_write_network_state) carry it, and the game ends as
Slayer's does, at the score to win, which is the list's length.

The list is those of the loadout weapons (game_engine.h's _loadout_weapon_*)
the map has, the strongest first (the rocket launcher, the shotgun, the
sniper rifle, the plasma rifle, the assault rifle, the needler, the plasma
pistol, the pistol), the weakest first, or in an order the host draws for
each game. Only the host gives weapons: a player's weapons go to every
machine with their inventory (network_objects.c). No one picks up a
weapon; a weapon emptied is replaced with a full one; and a melee kill can
take the victim back a weapon (gun_game_melee_demotes).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_engine_slayer.h"
#include "game/players.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "units/units.h"
#include "extensions/extension_api.h"
#include "gun_game_rules.h"
#include "game_modes.h"
#include "network_rules.h"

static byte const gun_game_strongest_first[] =
{
	_loadout_weapon_rocket_launcher,
	_loadout_weapon_shotgun,
	_loadout_weapon_sniper_rifle,
	_loadout_weapon_plasma_rifle,
	_loadout_weapon_assault_rifle,
	_loadout_weapon_needler,
	_loadout_weapon_plasma_pistol,
	_loadout_weapon_pistol,
};

#define GUN_GAME_MAXIMUM_LEVELS NUMBEROF(gun_game_strongest_first)

static struct
{
	struct game_engine engine;
	struct game_engine slayer;
	/* the map's list, its weapons as _loadout_weapon_* */
	byte levels[GUN_GAME_MAXIMUM_LEVELS];
	short level_count;
	/* the host's: each player's unit given a level's weapon, and that level
	(by absolute player index) */
	long equipped_unit[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short equipped_level[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* a weapon being given (gun_game_allow_pick_up allows it) */
	boolean equipping;
} gun_game;

boolean network_game_distributed_client(void);
long config_integer(char const *name);
int config_boolean(char const *name);
/* (port/linux/game/game_modes.c) */
boolean game_modes_killed_by_melee(void);
short game_modes_variant_mode(struct game_variant const *variant,
	struct game_variant_extended_options const *options, boolean *hardcore);

/* a player's level: their score, within the list */
static short gun_game_player_level(
	long player_index)
{
	long score = gun_game.slayer.get_player_score(player_index, _get_score_individual);

	return gun_game_rules_level(score, gun_game.level_count);
}

/* a weapon with no rounds left (none loaded nor carried), or a battery
spent */
#define MAXIMUM_WEAPON_MAGAZINES_FOR_RULES 8
static boolean gun_game_weapon_empty(
	long weapon_index)
{
	struct weapon_datum *weapon = weapon_try_and_get(weapon_index);
	struct weapon_definition *definition;
	short magazine;
	short loaded[MAXIMUM_WEAPON_MAGAZINES_FOR_RULES];
	short total[MAXIMUM_WEAPON_MAGAZINES_FOR_RULES];
	short count;

	if (!weapon)
		return FALSE;
	definition = weapon_definition_get(weapon->definition_index);
	count = (short)MIN(definition->weapon.magazines.count, NUMBEROF(weapon->weapon.magazines));
	count = (short)MIN(count, MAXIMUM_WEAPON_MAGAZINES_FOR_RULES);
	for (magazine = 0; magazine < count; magazine++)
	{
		loaded[magazine] = weapon->weapon.magazines[magazine].rounds_loaded;
		total[magazine] = weapon->weapon.magazines[magazine].rounds_total;
	}
	return gun_game_rules_weapon_empty(definition->weapon.magazines.count <= 0 ? 0 : count, weapon->weapon.age,
		loaded, total) != 0;
}

/* every weapon of a unit gone, the one in its hand too (which
unit_delete_all_weapons keeps) */
static void gun_game_delete_weapons(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	short slot;
	short other;

	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long weapon_index = unit->unit.weapon_object_indices[slot];

		if (weapon_index == NONE)
			continue;
		/* (each deleted once, should two slots hold it) */
		for (other = slot; other < MAXIMUM_WEAPONS_PER_UNIT; other++)
		{
			if (unit->unit.weapon_object_indices[other] == weapon_index)
			{
				unit->unit.weapon_object_indices[other] = NONE;
				if (unit->unit.current_weapon_index == other)
					unit->unit.current_weapon_index = NONE;
				if (unit->unit.desired_weapon_index == other)
					unit->unit.desired_weapon_index = NONE;
			}
		}
		if (object_try_and_get(weapon_index))
			object_delete(weapon_index);
	}
}

/* the host gives a living player their level's weapon (their others gone),
again when it is spent */
static void gun_game_equip(
	long player_index)
{
	struct player_datum *player = player_get(player_index);
	long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	struct unit_datum *unit;
	short level;
	long definition_index;
	long weapon_index;
	struct object_placement_data placement;

	if (network_game_distributed_client() || !VALID_INDEX(slot, HALO_PORT_MAXIMUM_NETWORK_PLAYERS) ||
		player->quit_out_of_game || player->unit_index == NONE || !gun_game.level_count)
	{
		return;
	}
	unit = unit_try_and_get(player->unit_index);
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return;
	level = gun_game_player_level(player_index);
	{
		long current = VALID_INDEX(unit->unit.current_weapon_index, MAXIMUM_WEAPONS_PER_UNIT) ?
			unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE;

		if (!gun_game_rules_needs_weapon(gun_game.equipped_unit[slot], gun_game.equipped_level[slot],
			player->unit_index, level, current != NONE, current != NONE && gun_game_weapon_empty(current)))
		{
			return;
		}
	}
	/* (no weapon in the hand: a grenade being thrown has it, so wait) */
	if (unit->unit.grenade_throw_state != _unit_grenade_throw_idle)
		return;
	definition_index = game_engine_loadout_weapon_definition(gun_game.levels[level]);
	if (definition_index == NONE)
		return;
	gun_game.equipping = TRUE;
	gun_game_delete_weapons(player->unit_index);
	object_placement_data_new(&placement, definition_index, NONE);
	weapon_index = object_new(&placement);
	if (weapon_index != NONE && !unit_add_weapon_to_inventory(player->unit_index, weapon_index, 2))
	{
		object_delete(weapon_index);
		weapon_index = NONE;
	}
	gun_game.equipping = FALSE;
	gun_game.equipped_unit[slot] = player->unit_index;
	gun_game.equipped_level[slot] = level;
}

/* a player spawned: their weapon, after the starting equipment replaced
(extension_api.h's postspawn_player) */
static void gun_game_postspawn(
	long player_index)
{
	long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

	if (VALID_INDEX(slot, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
		gun_game.equipped_unit[slot] = NONE;
	gun_game_equip(player_index);
}

/* the map's list, in the gametype's order */
static long gun_game_random(
	void *context,
	long low,
	long high)
{
	(void)context;
	return local_random_range(low, high);
}

static void gun_game_levels_build(
	void)
{
	short order = game_variant_extended_options_get()->gun_game_order;
	unsigned char available[GUN_GAME_MAXIMUM_LEVELS];
	unsigned char slots[GUN_GAME_MAXIMUM_LEVELS];
	short index;

	for (index = 0; index < (short)GUN_GAME_MAXIMUM_LEVELS; index++)
		available[index] = game_engine_loadout_weapon_definition(gun_game_strongest_first[index]) != NONE;
	gun_game.level_count = gun_game_rules_build_levels(order, GUN_GAME_MAXIMUM_LEVELS, available, slots);
	/* (the host's draw: only the host gives weapons) */
	if (order == _gun_game_order_random && !network_game_distributed_client())
		gun_game_rules_shuffle(slots, gun_game.level_count, gun_game_random, NULL);
	for (index = 0; index < gun_game.level_count; index++)
		gun_game.levels[index] = gun_game_strongest_first[slots[index]];
}

static boolean gun_game_initialize_for_new_map(
	void)
{
	gun_game_levels_build();
	csmemset(gun_game.equipped_unit, 0xFF, sizeof(gun_game.equipped_unit));
	csmemset(gun_game.equipped_level, 0xFF, sizeof(gun_game.equipped_level));
	gun_game.equipping = FALSE;
	/* (the game is won with a kill with the last weapon: every machine's
	list is as long) */
	if (gun_game.level_count)
		game_engine_get_variant()->universal_variant.score_to_win = gun_game_rules_score_to_win(gun_game.level_count);
	return gun_game.slayer.initialize_for_new_map ? gun_game.slayer.initialize_for_new_map() : TRUE;
}

static void gun_game_player_update_each_tick(
	long player_index)
{
	if (gun_game.slayer.player_update_each_tick)
		gun_game.slayer.player_update_each_tick(player_index);
	gun_game_equip(player_index);
}

/* only the weapons given (and a weapon held, which readying it asks of) */
static boolean gun_game_allow_pick_up(
	long unit_index,
	long weapon_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	short slot;

	boolean holds = FALSE;

	for (slot = 0; unit && slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		if (unit->unit.weapon_object_indices[slot] == weapon_index)
			holds = TRUE;
	}
	return gun_game_rules_allow_pick_up(gun_game.equipping, holds) != 0;
}

static void gun_game_player_killed_player(
	long killing_player_index,
	long killing_object_index,
	long dead_player_index,
	boolean friendly_fire)
{
	gun_game.slayer.player_killed_player(killing_player_index, killing_object_index, dead_player_index, friendly_fire);
	if (network_game_distributed_client() || player_get(dead_player_index)->quit_out_of_game)
		return;
	/* a melee kill takes the victim back a weapon */
	{
		int demotion = gun_game_rules_demotion(game_variant_extended_options_get()->gun_game_melee_demotes,
			game_modes_killed_by_melee(), friendly_fire, killing_player_index != NONE,
			killing_player_index == dead_player_index,
			gun_game.slayer.get_player_score(dead_player_index, _get_score_individual));

		if (demotion)
			game_engine_slayer_adjust_score(dead_player_index, demotion);
	}
	/* (a suicide at the first weapon leaves the score at 0) */
	{
		int restore = gun_game_rules_floor(gun_game.slayer.get_player_score(dead_player_index, _get_score_individual));

		if (restore)
			game_engine_slayer_adjust_score(dead_player_index, restore);
	}
}

static struct game_engine *gun_game_engine_get(
	struct game_engine const *slayer)
{
	gun_game.slayer = *slayer;
	gun_game.engine = *slayer;
	gun_game.engine.initialize_for_new_map = gun_game_initialize_for_new_map;
	gun_game.engine.player_update_each_tick = gun_game_player_update_each_tick;
	gun_game.engine.allow_pick_up = gun_game_allow_pick_up;
	gun_game.engine.player_killed_player = gun_game_player_killed_player;
	return &gun_game.engine;
}

/* ---------- the extension */

static boolean gun_game_select(
	struct game_engine *original,
	struct game_variant *variant,
	struct game_engine **selected)
{
	if (!original || !variant || !selected || original->type != game_engine_slayer ||
		variant->universal_variant.teams ||
		game_modes_variant_mode(variant, game_variant_extended_options_get(), NULL) != _game_mode_gun_game)
	{
		return FALSE;
	}
	*selected = gun_game_engine_get(original);
	return TRUE;
}

/* the host's gametype: this game plays Gun Game when gun_game.host is set and
its variant is free for all Slayer. Gun Game's weapons are the host's alone,
so the map has none of its own. */
static boolean gun_game_host_game_options(
	struct game_variant const *variant,
	struct game_variant_options *options,
	unsigned char extended[16])
{
	short order = (short)PIN(config_integer("gun_game.weapon_order"), 0, NUMBER_OF_GUN_GAME_ORDERS - 1);
	boolean demotes = config_boolean("gun_game.melee_demotes");
	struct game_variant_extended_options chosen;

	if (!config_boolean("gun_game.host") || variant->game_engine_index != game_engine_slayer ||
		variant->universal_variant.teams)
	{
		return FALSE;
	}
	(void)chosen;
	extended[NETWORK_RULES_OPTION_MODE] = NETWORK_RULES_MODE_GUN_GAME;
	extended[NETWORK_RULES_OPTION_WEAPON_ORDER] = (unsigned char)order;
	extended[NETWORK_RULES_OPTION_MELEE_DEMOTES] = demotes != FALSE;
	options->no_map_weapons = TRUE;
	return TRUE;
}

static struct halo_ruleset const gun_game_ruleset =
{
	.select_game_engine = gun_game_select,
	/* Slayer's scores and the weapons' objects replicate as they always do */
	.local_only = FALSE,
	.postspawn_player = gun_game_postspawn,
};

struct halo_extension const gun_game_extension =
{
	.name = "gun_game",
	.opence_modes = 1UL << _game_mode_gun_game,
	.host_game_options = gun_game_host_game_options,
	.ruleset = &gun_game_ruleset,
};
