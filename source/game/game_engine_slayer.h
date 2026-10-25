/*
GAME_ENGINE_SLAYER.H
*/

#ifndef __GAME_ENGINE_SLAYER_H
#define __GAME_ENGINE_SLAYER_H
#pragma once

/* ---------- headers */

#include "game/game_engine.h"

/* ---------- prototypes/GAME_ENGINE_SLAYER.C */

/* port: a player's score (and their team's) changed by the host */
void game_engine_slayer_adjust_score(
	long player_index,
	long score_delta);
void update_speed_for_score(
	long dead_player_index,
	long killing_player_index);

/* ---------- globals */

extern struct game_engine slayer_engine;

#endif // __GAME_ENGINE_SLAYER_H
