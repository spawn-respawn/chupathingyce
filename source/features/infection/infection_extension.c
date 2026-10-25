/*
INFECTION_EXTENSION.C

Infection as an extension (extension_api.h). The rules are OpenCE's
(infection.c, brought over from port/linux/game/infection.c of the
FractumSeraph merge; pinned in compatibility.json): a ruleset that claims team
Slayer games whose options say Infection, answers the engine's queries (a
zombie takes no grenades, keeps its skull, hits riders), keeps its own clock
and sends its own state, which is the one wire fact: the state in
network_infection.h goes in the game type's state as it does in OpenCE.
*/

#include "cseries.h"
#include "extensions/extension_api.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game_modes.h"
#include "network_rules.h"
#include "infection.h"

long config_integer(char const *name);
int config_boolean(char const *name);

static boolean infection_select(
	struct game_engine *original,
	struct game_variant *variant,
	struct game_engine **selected)
{
	if (!original || !variant || !selected || original->type != game_engine_slayer ||
		!variant->universal_variant.teams ||
		game_modes_variant_mode(variant, game_variant_extended_options_get(), NULL) != _game_mode_infection)
	{
		return FALSE;
	}
	*selected = infection_engine_get(original);
	return TRUE;
}

static boolean infection_should_end_game(
	void)
{
	/* the round ends by its own rules (infection.c) */
	return FALSE;
}

static boolean infection_time_remaining(
	long *ticks)
{
	*ticks = infection_remaining_ticks();
	return TRUE;
}

static long infection_respawn_ticks(
	void)
{
	return game_variant_extended_options_get()->infection_respawn_time * TICKS_PER_SECOND;
}

static boolean infection_write_state(
	byte *buffer,
	long size,
	long *written)
{
	*written = infection_write_network_state(buffer, size);
	return TRUE;
}

static boolean infection_read_state(
	byte const *buffer,
	long size,
	boolean first,
	boolean *read)
{
	*read = infection_read_network_state(buffer, size, first);
	return TRUE;
}

/* the host's game: infection.host, on team Slayer, with OpenCE's built-in
gametype's PC options and the settings' Infection options (OpenCE's ranges) */
static boolean infection_host_game_options(
	struct game_variant const *variant,
	struct game_variant_options *options,
	unsigned char extended[16])
{
	if (!config_boolean("infection.host") || variant->game_engine_index != game_engine_slayer ||
		!variant->universal_variant.teams)
	{
		return FALSE;
	}
	extended[NETWORK_RULES_OPTION_MODE] = NETWORK_RULES_MODE_INFECTION;
	extended[NETWORK_RULES_OPTION_FIRST_ZOMBIES] = (unsigned char)config_integer("infection.initial_zombies");
	extended[NETWORK_RULES_OPTION_ZOMBIE_SPEED] = (unsigned char)config_integer("infection.zombie_speed");
	extended[NETWORK_RULES_OPTION_SKULL_DAMAGE] = (unsigned char)config_integer("infection.skull_damage");
	extended[NETWORK_RULES_OPTION_ZOMBIE_RESPAWN] = (unsigned char)config_integer("infection.respawn_time");
	extended[NETWORK_RULES_OPTION_COUNTDOWN] = (unsigned char)config_integer("infection.countdown");
	extended[NETWORK_RULES_OPTION_ZOMBIE_SHIELDS] = config_boolean("infection.zombie_shields") != 0;
	/* (out-of-range values are OpenCE's defaults: network_rules_extended_options_sanitize) */
	options->time_limit = 5;
	options->friendly_fire = _friendly_fire_off;
	return TRUE;
}

static struct halo_ruleset const infection_ruleset =
{
	.select_game_engine = infection_select,
	/* Infection's state replicates in the game type's state */
	.local_only = FALSE,
	.should_end_game = infection_should_end_game,
	.postspawn_player = infection_postspawn,
	.unit_restricted = infection_unit_is_zombie,
	.allow_weapon_drop = infection_allow_weapon_drop,
	.can_delete_objective_weapon = infection_can_delete_skull,
	.melee_multiplier = infection_melee_multiplier,
	.melee_rider_target = infection_melee_rider_target,
	.melee_rider_visible = infection_melee_rider_visible,
	.player_unit_attached = infection_reconcile_player,
	.time_remaining = infection_time_remaining,
	.respawn_ticks = infection_respawn_ticks,
	.write_network_state = infection_write_state,
	.read_network_state = infection_read_state,
	.manages_teams = TRUE,
	.uses_ball_weapon = TRUE,
};

struct halo_extension const infection_extension =
{
	.name = "infection",
	.opence_modes = 1UL << _game_mode_infection,
	.host_game_options = infection_host_game_options,
	.ruleset = &infection_ruleset,
};
