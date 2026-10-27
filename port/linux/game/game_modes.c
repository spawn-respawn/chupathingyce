/*
GAME_MODES.C

OpenCE's gametypes' options (game_modes.h). Their wire form is network_rules.c's
(the 16 further bytes of the game settings record, network version 35): this
file only gives the game's struct to those bytes and back, and range checks
through the same function every machine uses, so no second rule exists.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_engine_playlist.h"
#include "text/unicode.h"
#include "game_modes.h"
#include "network_rules.h"

typedef char game_modes_size_assert[
	sizeof(struct game_variant_extended_options) == NETWORK_RULES_EXTENDED_OPTIONS_SIZE ? 1 : -1];

/* the options the next game takes, as the wire has them */
static unsigned char game_modes_pending[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
static boolean game_modes_pending_known;
static boolean game_modes_melee_kill;

void game_modes_options_default(
	struct game_variant_extended_options *options)
{
	unsigned char bytes[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];

	network_rules_extended_options_default(bytes);
	csmemcpy(options, bytes, sizeof(bytes));
}

void game_modes_options_sanitize(
	struct game_variant_extended_options *options)
{
	unsigned char bytes[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];

	csmemcpy(bytes, options, sizeof(bytes));
	network_rules_extended_options_sanitize(bytes);
	csmemcpy(options, bytes, sizeof(bytes));
}

short game_modes_variant_mode(
	struct game_variant const *variant,
	struct game_variant_extended_options const *options,
	boolean *hardcore)
{
	unsigned char bytes[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	int hardcore_flag = 0;
	int mode;

	csmemcpy(bytes, options, sizeof(bytes));
	network_rules_extended_options_sanitize(bytes);
	mode = network_rules_extended_options_mode(bytes, variant->game_engine_index == game_engine_slayer,
		variant->universal_variant.teams != 0, &hardcore_flag);
	if (hardcore)
		*hardcore = (boolean)(hardcore_flag != 0);
	return (short)mode;
}

void game_modes_options_default_bytes(
	unsigned char *bytes)
{
	network_rules_extended_options_default(bytes);
}

short game_modes_mode(
	void)
{
	if (!game_engine_running())
		return _game_mode_none;
	return game_modes_variant_mode(game_engine_get_variant(), game_variant_extended_options_get(), NULL);
}

boolean game_modes_hardcore(
	void)
{
	return game_engine_running() && game_variant_extended_options_get()->hardcore;
}

void game_modes_pending_set(
	unsigned char const *bytes)
{
	csmemcpy(game_modes_pending, bytes, sizeof(game_modes_pending));
	network_rules_extended_options_sanitize(game_modes_pending);
	game_modes_pending_known = TRUE;
}

void game_modes_pending_get(
	unsigned char *bytes)
{
	if (game_modes_pending_known)
		csmemcpy(bytes, game_modes_pending, sizeof(game_modes_pending));
	else
		network_rules_extended_options_default(bytes);
}

void game_modes_pending_choose_gun_game(
	short order,
	boolean melee_demotes)
{
	unsigned char bytes[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];

	network_rules_extended_options_default(bytes);
	bytes[NETWORK_RULES_OPTION_MODE] = NETWORK_RULES_MODE_GUN_GAME;
	bytes[NETWORK_RULES_OPTION_WEAPON_ORDER] = (unsigned char)order;
	bytes[NETWORK_RULES_OPTION_MELEE_DEMOTES] = melee_demotes != FALSE;
	game_modes_pending_set(bytes);
}

void game_modes_pending_clear(
	void)
{
	game_modes_pending_known = FALSE;
}

void game_modes_commit_pending(
	struct game_variant const *variant)
{
	struct game_variant_extended_options options;
	boolean hardcore;

	if (!game_modes_pending_known || !variant)
	{
		game_set_game_variant_extended_options(NULL);
		return;
	}
	csmemcpy(&options, game_modes_pending, sizeof(options));
	/* (a mode its variant does not allow is none; hardcore stays its flag) */
	if (game_modes_variant_mode(variant, &options, &hardcore) == _game_mode_none)
		options.mode = _game_mode_none;
	game_set_game_variant_extended_options(&options);
}

unsigned char game_modes_kind(
	struct game_variant const *variant)
{
	unsigned char bytes[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	struct game_variant_extended_options options;
	boolean hardcore = FALSE;
	int mode;

	game_modes_pending_get(bytes);
	csmemcpy(&options, bytes, sizeof(options));
	mode = game_modes_variant_mode(variant, &options, &hardcore);
	return (unsigned char)(mode | (hardcore ? 0x80 : 0));
}

void game_modes_kill_by_melee(
	boolean melee)
{
	game_modes_melee_kill = melee;
}

boolean game_modes_killed_by_melee(
	void)
{
	return game_modes_melee_kill;
}


/* ---- the built-in gametypes (OpenCE's game_modes_gametype, with the option
bytes apart: this build keeps its game_variant_options at 0x1C bytes) */

/* game_engine.c's values of the variant's (its enums goal_radar,
game_engine_weapons and game_engine_vehicles) */
enum
{
	GOAL_RADAR_NAV_POINT = 1,
	WEAPON_SET_NO_GRENADES = 10,
	VEHICLE_SET_NONE = 1,
	INFECTION_RESPAWN_TIME = 5
};

enum
{
	_gametype_swat = 0,
	_gametype_infection,
	_gametype_gun_game,
	_gametype_hardcore_slayer,
	_gametype_hardcore_ctf,
	_gametype_hardcore_oddball,
	_gametype_hardcore_king,
	NUMBER_OF_GAMETYPES
};

static struct
{
	/* the gametype lists' */
	wchar_t const *display_name;
	/* the variant's own (11 characters at most: the lobby's and the server
	browser's) */
	wchar_t const *variant_name;
} const game_modes_gametypes[NUMBER_OF_GAMETYPES] =
{
	{ L"SWAT", L"SWAT" },
	{ L"Infection", L"Infection" },
	{ L"Gun Game", L"Gun Game" },
	{ L"Hardcore Slayer", L"Hardcore" },
	{ L"Hardcore CTF", L"HC CTF" },
	{ L"Hardcore Oddball", L"HC Oddball" },
	{ L"Hardcore King", L"HC King" },
};

/* what the gametype picker's card says under its name (the Xbox's list has
none for these) */
static wchar_t const *const game_modes_descriptions[NUMBER_OF_GAMETYPES] =
{
	L"No shields and a shot to the\nhead kills. Pistol and assault\nrifle only. Team Slayer.",
	L"Humans killed join the zombies,\nwho win when no human is left.\nTeam Slayer.",
	L"Each kill gives the next weapon;\na kill with the last wins.\nFree for all Slayer.",
	L"No motion sensor or aim assist;\na kill restores your health and\nshields. Team Slayer.",
	L"No motion sensor or aim assist;\na kill restores your health and\nshields. Capture the Flag.",
	L"No motion sensor or aim assist;\na kill restores your health and\nshields. Team Oddball.",
	L"No motion sensor or aim assist;\na kill restores your health and\nshields. Team King of the Hill.",
};

wchar_t const *game_modes_gametype_description(
	short index)
{
	return VALID_INDEX(index, NUMBER_OF_GAMETYPES) ? game_modes_descriptions[index] : NULL;
}

short game_modes_gametype_count(
	void)
{
	return NUMBER_OF_GAMETYPES;
}

wchar_t const *game_modes_gametype_name(
	short index)
{
	return VALID_INDEX(index, NUMBER_OF_GAMETYPES) ? game_modes_gametypes[index].display_name : L"";
}

/* a variant without a motion sensor: no players on it, and a goal (a flag,
the ball, the hill) shown by its nav point instead */
static void game_modes_no_motion_sensor(
	struct game_variant *variant,
	struct game_variant_options *options)
{
	SET_FLAG(variant->universal_variant.flags, _game_variant_draw_object_in_motion_sensor_bit, FALSE);
	variant->universal_variant.goal_radar = GOAL_RADAR_NAV_POINT;
	options->radar_players = _radar_players_none;
}

void game_modes_gametype(
	short index,
	struct game_variant *variant,
	struct game_variant_options *options,
	unsigned char extended[NETWORK_RULES_EXTENDED_OPTIONS_SIZE])
{
	struct game_variant temporary;

	switch (index)
	{
	case _gametype_hardcore_ctf:
		*variant = *build_game_variant_ctf(&temporary);
		break;
	case _gametype_hardcore_oddball:
		*variant = *build_game_variant_team_oddball(&temporary);
		break;
	case _gametype_hardcore_king:
		*variant = *build_game_variant_team_king(&temporary);
		break;
	case _gametype_gun_game:
		*variant = *build_game_variant_slayer(&temporary);
		break;
	default:
		*variant = *build_game_variant_team_slayer(&temporary);
		break;
	}
	game_variant_options_default(variant, options);
	network_rules_extended_options_default(extended);

	switch (index)
	{
	case _gametype_swat:
		extended[NETWORK_RULES_OPTION_MODE] = NETWORK_RULES_MODE_SWAT;
		SET_FLAG(variant->universal_variant.flags, _game_variant_no_shields_bit, TRUE);
		variant->universal_variant.weapon_set = WEAPON_SET_NO_GRENADES;
		variant->universal_variant.vehicle_set = VEHICLE_SET_NONE;
		variant->universal_variant.respawn_time = 150;
		game_modes_no_motion_sensor(variant, options);
		options->loadout = _loadout_custom;
		options->primary_weapon = _loadout_weapon_pistol;
		options->secondary_weapon = _loadout_weapon_assault_rifle;
		options->no_map_weapons = TRUE;
		break;
	case _gametype_infection:
		extended[NETWORK_RULES_OPTION_MODE] = NETWORK_RULES_MODE_INFECTION;
		variant->universal_variant.vehicle_set = VEHICLE_SET_NONE;
		variant->universal_variant.respawn_time = INFECTION_RESPAWN_TIME * TICKS_PER_SECOND;
		variant->universal_variant.suicide_penalty = 0;
		options->time_limit = 5;
		options->friendly_fire = _friendly_fire_off;
		break;
	case _gametype_gun_game:
		extended[NETWORK_RULES_OPTION_MODE] = NETWORK_RULES_MODE_GUN_GAME;
		variant->universal_variant.weapon_set = WEAPON_SET_NO_GRENADES;
		variant->universal_variant.vehicle_set = VEHICLE_SET_NONE;
		variant->universal_variant.respawn_time = 90;
		variant->universal_variant.suicide_penalty = 0;
		options->no_map_weapons = TRUE;
		break;
	default:
		extended[NETWORK_RULES_OPTION_HARDCORE] = 1;
		options->friendly_fire = _friendly_fire_on;
		game_modes_no_motion_sensor(variant, options);
		break;
	}
	/* (each team's vehicles the variant's, as game_variant_options_default
	gives them) */
	options->vehicle_set[0] = options->vehicle_set[1] = (byte)variant->universal_variant.vehicle_set;
	network_rules_extended_options_sanitize(extended);
	csmemset(variant->human_readable_game_description, 0, sizeof(variant->human_readable_game_description));
	if (VALID_INDEX(index, NUMBER_OF_GAMETYPES))
	{
		ustrncpy(variant->human_readable_game_description, game_modes_gametypes[index].variant_name,
			NUMBEROF(variant->human_readable_game_description) - 1);
	}
}

/* the name the menus show for a game's kind ("INFECTION", "HARDCORE CTF"),
NULL for its game engine's own: network_rules_kind_name, in wide characters */
wchar_t const *game_modes_variant_kind_name(
	struct game_variant const *variant,
	unsigned char const extended[NETWORK_RULES_EXTENDED_OPTIONS_SIZE])
{
	static wchar_t name[32];
	struct game_variant_extended_options options;
	boolean hardcore = FALSE;
	char const *text;
	int mode;
	short index;

	csmemcpy(&options, extended, sizeof(options));
	mode = game_modes_variant_mode(variant, &options, &hardcore);
	text = network_rules_kind_name(mode | (hardcore ? 0x80 : 0), variant->game_engine_index, 0);
	if (!text)
		return NULL;
	for (index = 0; text[index] && index < (short)NUMBEROF(name) - 1; index++)
		name[index] = (wchar_t)(unsigned char)text[index];
	name[index] = 0;
	return name;
}
