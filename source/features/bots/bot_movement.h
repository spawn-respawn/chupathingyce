/* Recovered normal bot follower math (BotMovement.cpp, 0x1426eba18).
 * This is NOT the separate ordinary-AI steering branch. Special-link flags
 * and the downward-only horizontal-metric predicate remain unrecovered. */
#ifndef HALO_BOT_MOVEMENT_H
#define HALO_BOT_MOVEMENT_H
#include <stdbool.h>
#include "../../engine_ai/surface_navigation.h"

/* Profile value is already squared: compare directly, never square it again.
 * The caller may request horizontal-only distance only for an established
 * special/downward link. Ordinary CE surface routes use full 3D distance. */
bool bot_movement_point_reached(struct sn_point origin, struct sn_point target,
    float reach_distance_squared, bool horizontal_only);
/* Normalized planar control direction; no velocity/position writes. A
 * degenerate/invalid direction returns false and a zero output. */
bool bot_movement_direction(struct sn_point origin, struct sn_point target,
    struct sn_point *direction);
/* BotAvoid: reuse an accepted cached direction, else lateral, opposite lateral,
 * then backward. At most four caller-owned forward/support probes; no world
 * reads here and no claim that this is Infinite's shared crowd solver. */
typedef bool (*bot_movement_direction_clear)(void *context, struct sn_point direction);
bool bot_movement_escape(struct sn_point toward_threat, struct sn_point cached,
    bot_movement_direction_clear clear, void *context, struct sn_point *escape);
#endif
