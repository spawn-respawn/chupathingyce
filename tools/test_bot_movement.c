#include "../source/features/bots/bot_movement.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

struct probes { struct sn_point directions[4]; uint32_t count, accept; };
static bool clear(void *context, struct sn_point direction)
{
    struct probes *p = context;
    assert(p->count < 4);
    assert(fabsf(direction.x * direction.x + direction.y * direction.y - 1.f) < 0.00001f);
    assert(direction.z == 0.f);
    p->directions[p->count++] = direction;
    return p->count == p->accept;
}

int main(void)
{
    struct sn_point origin = {0}, target = {0.5f, 0.f, 0.f}, direction;
    struct sn_point toward = {2.f, 0.f, 0.f}, cached = {1.f, 0.f, 0.f};
    struct probes probes = {0};
    assert(bot_movement_point_reached(origin, target, 0.25f, false));
    assert(!bot_movement_point_reached(origin, target, 0.249f, false));
    target.z = 1.f;
    assert(!bot_movement_point_reached(origin, target, 0.25f, false));
    assert(bot_movement_point_reached(origin, target, 0.25f, true));
    assert(!bot_movement_point_reached(origin, target, NAN, false));
    assert(!bot_movement_point_reached(origin, target, -1.f, false));
    target.x = NAN;
    assert(!bot_movement_point_reached(origin, target, 1.f, true));
    target.x = 3.f; target.y = 4.f;
    assert(bot_movement_direction(origin, target, &direction));
    assert(fabsf(direction.x - 0.6f) < 0.00001f && fabsf(direction.y - 0.8f) < 0.00001f);
    assert(!bot_movement_direction(origin, origin, &direction) && direction.x == 0.f);
    target.x = INFINITY;
    assert(!bot_movement_direction(origin, target, &direction));
    probes.accept = 1;
    assert(bot_movement_escape(toward, cached, clear, &probes, &direction));
    assert(probes.count == 1 && direction.x == 1.f); /* accepted cache */
    probes.count = 0; probes.accept = 4;
    assert(bot_movement_escape(toward, cached, clear, &probes, &direction));
    assert(probes.count == 4 && direction.x == -1.f);
    assert(probes.directions[1].y == 1.f && probes.directions[2].y == -1.f);
    probes.count = 0; probes.accept = 0;
    assert(!bot_movement_escape(toward, cached, clear, &probes, &direction));
    assert(probes.count == 4 && direction.x == 0.f && direction.y == 0.f);
    cached.x = NAN; probes.count = 0; probes.accept = 2;
    assert(bot_movement_escape(toward, cached, clear, &probes, &direction));
    assert(probes.count == 2 && direction.y == -1.f); /* invalid cache not probed */
    puts("Bot movement: squared profile metric, normalized controls, bounded escape probes passed");
    return 0;
}
