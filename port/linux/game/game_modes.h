/* game_modes.h: OpenCE's gametypes' options (port/linux/game/game_modes.c)

SWAT, Infection and Gun Game are modes of Slayer, and Hardcore is a set of
rules over any game engine's: a game's options (game_engine.h's struct
game_variant_extended_options, OpenCE's 0x1C to 0x2B of its game variant
options) say which. This file keeps them, the host's choice of them and the
ones a client was sent, and when a game starts the game takes them
(game_set_game_variant_extended_options). The rules of a mode are an
extension's (source/features/<mode>): the game engine selects it
(extension_api.h's halo_ruleset), and halo_extensions_plays_opence_mode says
which modes a build plays; a game of one it does not play is left, never
played as plain Slayer.

Upstream: OpenCE port/linux/game/game_modes.c (see
each feature's compatibility.json). Only the options, their ranges and the
melee classification are here; the rules of each mode are its extension's
(source/features/<mode>); the gametype lists and menus are not brought over. */
#ifndef HALO_GAME_MODES_H
#define HALO_GAME_MODES_H

#include "cseries.h"

struct game_variant;
struct game_variant_options;
struct game_engine;
struct game_variant_extended_options;

/* the options at their defaults, with no mode */
void game_modes_options_default(struct game_variant_extended_options *options);
/* each option in range (another machine's or a file's), its reserved bytes 0 */
void game_modes_options_sanitize(struct game_variant_extended_options *options);
/* the mode a gametype plays: its options' if its game engine and teams allow
it (SWAT and Infection need Slayer, Infection teams, Gun Game free for all),
else _game_mode_none. hardcore: its flag, if asked */
short game_modes_variant_mode(struct game_variant const *variant,
	struct game_variant_extended_options const *options, boolean *hardcore);
/* the mode of the game being played (_game_mode_none without one) */
short game_modes_mode(void);

/* ---- the options a game will be played with: the host's own choice, or
the record a client was sent (the 16 bytes as the network has them) */
void game_modes_pending_set(unsigned char const *bytes);
void game_modes_pending_get(unsigned char *bytes);
/* the host chooses Gun Game for its next free for all Slayer game */
void game_modes_pending_choose_gun_game(short order, boolean melee_demotes);
void game_modes_pending_clear(void);
/* a game is starting (network_game_manager.c): it takes the pending options,
only if its variant allows the mode. */
void game_modes_commit_pending(struct game_variant const *variant);

/* whether the game being played has Hardcore's flag (its rules are the
hardcore extension's) */
boolean game_modes_hardcore(void);
/* the 16 option bytes as the wire has them, at their defaults */
void game_modes_options_default_bytes(unsigned char *bytes);

/* the kind of the hosted game as its LAN advertisement and internet listing
carry it to the server browser: the mode it plays in the low seven bits,
Hardcore in bit 7; 0 for a stock game */
unsigned char game_modes_kind(struct game_variant const *variant);

/* ---- the built-in gametypes: SWAT, Infection, Gun Game and Hardcore's, as
OpenCE's OPENCE bank of Select Gametype lists them */
short game_modes_gametype_count(void);
wchar_t const *game_modes_gametype_name(short index);
/* its card's description in the gametype picker, NULL for none */
wchar_t const *game_modes_gametype_description(short index);
/* its variant, its PC options and its 16 option bytes */
void game_modes_gametype(short index, struct game_variant *variant, struct game_variant_options *options,
	unsigned char extended[16]);
/* the name the menus show for a game's kind (the mode, Hardcore), NULL for
its game engine's own */
wchar_t const *game_modes_variant_kind_name(struct game_variant const *variant, unsigned char const extended[16]);

/* ---- kill classification: the kill being recorded is a melee's (damage.c,
around game_statistics_record_kill) */
void game_modes_kill_by_melee(boolean melee);
boolean game_modes_killed_by_melee(void);

#endif
