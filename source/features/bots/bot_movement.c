/* Normal-bot distance predicate and planar controls recovered in
 * BotMovement.cpp. Profile numbers and CE special-link execution are separate. */
#include "bot_movement.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>

bool bot_movement_point_reached(struct sn_point origin, struct sn_point target,
    float reach_distance_squared, bool horizontal_only)
{
    float x = target.x - origin.x;
    float y = target.y - origin.y;
    float z = target.z - origin.z;
    float distance_squared;
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) ||
        !isfinite(reach_distance_squared) || reach_distance_squared < 0.f) {
        return false;
    }
    distance_squared = x * x + y * y;
    if (!horizontal_only) {
        distance_squared += z * z;
    }
    return isfinite(distance_squared) && distance_squared <= reach_distance_squared;
}

bool bot_movement_direction(struct sn_point origin, struct sn_point target,
    struct sn_point *direction)
{
    float x = target.x - origin.x;
    float y = target.y - origin.y;
    float length_squared = x * x + y * y;
    float length;
    assert(direction != NULL);
    *direction = (struct sn_point){0};
    if (!isfinite(length_squared) || length_squared <= 0.00000001f) {
        return false;
    }
    length = sqrtf(length_squared);
    direction->x = x / length;
    direction->y = y / length;
    return true;
}

bool bot_movement_escape(struct sn_point toward_threat, struct sn_point cached,
    bot_movement_direction_clear clear, void *context, struct sn_point *escape)
{
    struct sn_point zero = {0};
    struct sn_point toward;
    struct sn_point direction;
    uint32_t index;
    assert(escape != NULL);
    *escape = zero;
    if (!clear) {
        return false;
    }
    if (bot_movement_direction(zero, cached, &direction) && clear(context, direction)) {
        *escape = direction;
        return true;
    }
    if (!bot_movement_direction(zero, toward_threat, &toward)) {
        return false;
    }
    for (index = 0; index < 3; index++) {
        if (index == 0) {
            direction.x = -toward.y; direction.y = toward.x;
        } else if (index == 1) {
            direction.x = toward.y; direction.y = -toward.x;
        } else {
            direction.x = -toward.x; direction.y = -toward.y;
        }
        direction.z = 0.f;
        if (clear(context, direction)) {
            *escape = direction;
            return true;
        }
    }
    return false;
}
