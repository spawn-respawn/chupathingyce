/*
SWAT.C

SWAT as an extension (extension_api.h), OpenCE's rules (port/linux/game/
game_modes.c's SWAT parts, pinned in compatibility.json): a ruleset over any
Slayer game whose options say SWAT. It plays on Slayer's own engine, takes
the shields from the variant and makes a shot to a player's head kill. There
is no state of its own: the mode byte is the whole wire fact.
*/

#include "cseries.h"
#include "extensions/extension_api.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game_modes.h"
#include "network_rules.h"
#include "swat_rules.h"
#include "objects/objects.h"
#include "units/units.h"

int config_boolean(char const *name);

static boolean swat_select(
	struct game_engine *original,
	struct game_variant *variant,
	struct game_engine **selected)
{
	if (!original || !variant || !selected || original->type != game_engine_slayer ||
		game_modes_variant_mode(variant, game_variant_extended_options_get(), NULL) != _game_mode_swat)
	{
		return FALSE;
	}
	/* (SWAT is played without shields, whatever its variant's) */
	SET_FLAG(variant->universal_variant.flags, _game_variant_no_shields_bit, TRUE);
	*selected = original;
	return TRUE;
}

/* a shot's hit on a player's head kills at once */
static boolean swat_headshot_kills(
	long object_index,
	boolean shot)
{
	struct unit_datum *unit;

	unit = (struct unit_datum *)object_try_and_get_and_verify_type(object_index, _object_mask_biped);
	return swat_rules_headshot_kills(shot, unit && unit->unit.player_index != NONE) != 0;
}

/* the host's game: OpenCE's SWAT gametype's PC options (a pistol and an
assault rifle, nothing on the map) */
static boolean swat_host_game_options(
	struct game_variant const *variant,
	struct game_variant_options *options,
	unsigned char extended[16])
{
	if (!config_boolean("swat.host") || variant->game_engine_index != game_engine_slayer)
		return FALSE;
	extended[NETWORK_RULES_OPTION_MODE] = NETWORK_RULES_MODE_SWAT;
	options->loadout = _loadout_custom;
	options->primary_weapon = _loadout_weapon_pistol;
	options->secondary_weapon = _loadout_weapon_assault_rifle;
	options->no_map_weapons = TRUE;
	return TRUE;
}

static struct halo_ruleset const swat_ruleset =
{
	.select_game_engine = swat_select,
	.local_only = FALSE,
	.headshot_kills = swat_headshot_kills,
};

struct halo_extension const swat_extension =
{
	.name = "swat",
	.opence_modes = 1UL << _game_mode_swat,
	.host_game_options = swat_host_game_options,
	.ruleset = &swat_ruleset,
};
