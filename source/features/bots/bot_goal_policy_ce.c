/* TEMPORARY: CE-authored supplier for Infinite's unrecovered goal-scoring Lua.
 * Keep all goal priorities/curves here, not in recovered evaluators or claims.
 * Confidence weights and the CTF publisher's values are also CE tuning. */
#include "bot_goal_policy_ce.h"

#include <assert.h>
#include <stddef.h>

static struct bot_utility_tuning const bot_ce_tuning =
{
    0.95f, 0.15f, /* seated, traversal */
    0.6f, 0.8f, 0.75f, 0.95f, /* weapon, equipment, vehicle, in reach */
    { ENGINE_AI_UTILITY_LINEAR, 30.f, 0.f, 0.45f, 1.f, 0 },
    { ENGINE_AI_UTILITY_LINEAR, 35.f, 0.f, 0.2f, 1.f, 0 },
    { 3.f, 2.f, 1.f, 1.f }, 15.f
};

struct bot_utility_tuning const *bot_decision_default_tuning(void)
{
    return &bot_ce_tuning;
}

float bot_goal_policy_ce_objective_value(enum bot_ambition_kind kind)
{
    switch (kind) {
    case _bot_ambition_deliver: return 1.f;
    case _bot_ambition_return: return 0.99f;
    case _bot_ambition_hunt: return 0.985f;
    case _bot_ambition_take: return 0.97f;
    case _bot_ambition_guard: return 0.65f;
    default: return 0.f;
    }
}

static float bot_goal_policy_ce_pickup(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs)
{
    float value;
    engine_ai_utility_curve const *curve;
    if (!inputs->opportunity_relevant || !inputs->opportunity_distance_known) {
        return 0.f;
    }
    if (inputs->opportunity_in_reach) {
        return tuning->scavenge_in_reach;
    }
    value = inputs->opportunity_powerup ? tuning->scavenge_powerup : tuning->scavenge_weapon;
    curve = &tuning->scavenge_distance;
    if (inputs->opportunity_vehicle) {
        value = tuning->scavenge_vehicle;
        curve = &tuning->scavenge_vehicle_distance;
    }
    return value * engine_ai_utility_curve_evaluate(curve, inputs->opportunity_distance);
}

void bot_goal_policy_ce_outputs(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs, float outputs[BOT_GOAL_OUTPUT_COUNT])
{
    uint32_t index;
    float pickup;
    bool carrier;
    assert(tuning != NULL);
    assert(inputs != NULL);
    assert(outputs != NULL);
    for (index = 0; index < BOT_GOAL_OUTPUT_COUNT; index++) {
        outputs[index] = 0.f;
    }
    outputs[BOT_GOAL_TRAVERSAL] = tuning->roam;
    outputs[BOT_GOAL_PASSENGER_RIDE] = tuning->vehicle;
    if (inputs->objective_valid) {
        switch (inputs->objective_kind) {
        case _bot_ambition_take:
        case _bot_ambition_return: outputs[BOT_GOAL_PICKUP] = inputs->objective_value; break;
        case _bot_ambition_deliver: outputs[BOT_GOAL_DELIVER] = inputs->objective_value; break;
        case _bot_ambition_guard: outputs[BOT_GOAL_GUARD] = inputs->objective_value; break;
        case _bot_ambition_hunt: outputs[BOT_GOAL_HUNT] = inputs->objective_value; break;
        case _bot_ambition_hide: outputs[BOT_GOAL_HIDE] = inputs->objective_value; break;
        default: outputs[BOT_GOAL_TRAVERSAL] = inputs->objective_value; break;
        }
    }
    carrier = inputs->objective_valid && inputs->objective_kind == _bot_ambition_deliver;
    pickup = bot_goal_policy_ce_pickup(tuning, inputs);
    if (inputs->opportunity_vehicle && !carrier) {
        if (inputs->offered_ride) {
            pickup = 1.f;
        } else if (inputs->objective_valid && inputs->objective_kind != _bot_ambition_guard) {
            pickup = inputs->objective_value;
        }
    }
    /* CE inventory/carrier constraints remain part of the temporary supplier;
     * they do not short-circuit native fight/avoid evaluation. */
    if (carrier || inputs->navigation_only) {
        pickup = 0.f;
    }
    outputs[inputs->opportunity_vehicle ? BOT_GOAL_DRIVER_ENTER :
        (inputs->opportunity_powerup ? BOT_GOAL_EQUIPMENT : BOT_GOAL_WEAPON)] = pickup;
}

void bot_decision_score(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs, float scores[BOT_BEHAVIOR_COUNT])
{
    float outputs[BOT_GOAL_OUTPUT_COUNT];
    bot_goal_policy_ce_outputs(tuning, inputs, outputs);
    bot_decision_evaluate(inputs, outputs, scores);
}
