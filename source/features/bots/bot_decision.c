/* Recovered evaluator split (DecisionLoop.cpp): goals read supplied outputs;
 * fight is native. No CE scoring curves, world reads, or claim side effects.
 * Unsupported Infinite actions are not invented as CE controls. */
#include "bot_decision.h"

#include <assert.h>
#include <stddef.h>
#include <math.h>

uint32_t bot_decision_objective_leaf(enum bot_ambition_kind kind)
{
    switch (kind) {
    case _bot_ambition_take: return _bot_leaf_take;
    case _bot_ambition_deliver: return _bot_leaf_deliver;
    case _bot_ambition_return: return _bot_leaf_return;
    case _bot_ambition_guard: return _bot_leaf_guard;
    case _bot_ambition_hunt: return _bot_leaf_hunt;
    case _bot_ambition_hold: return _bot_leaf_hold;
    case _bot_ambition_reach: return _bot_leaf_reach;
    case _bot_ambition_hide: return _bot_leaf_hide;
    default: return _bot_leaf_none;
    }
}

uint32_t bot_decision_pickup_leaf(bool vehicle, bool equipment)
{
    return vehicle ? _bot_leaf_enter_vehicle :
        (equipment ? _bot_leaf_acquire_equipment : _bot_leaf_acquire_weapon);
}

bool bot_decision_is_objective_leaf(uint32_t leaf)
{
    return leaf == _bot_leaf_take || (leaf >= _bot_leaf_deliver && leaf <= _bot_leaf_hide);
}

bool bot_decision_is_pickup_leaf(uint32_t leaf)
{
    return leaf == _bot_leaf_acquire_weapon || leaf == _bot_leaf_acquire_equipment ||
        leaf == _bot_leaf_enter_vehicle;
}

static enum bot_goal_output bot_decision_objective_output(enum bot_ambition_kind kind)
{
    switch (kind) {
    case _bot_ambition_take:
    case _bot_ambition_return: return BOT_GOAL_PICKUP;
    case _bot_ambition_deliver: return BOT_GOAL_DELIVER;
    case _bot_ambition_guard: return BOT_GOAL_GUARD;
    case _bot_ambition_hunt: return BOT_GOAL_HUNT;
    case _bot_ambition_hide: return BOT_GOAL_HIDE;
    default: return BOT_GOAL_TRAVERSAL; /* CE hold/reach map to a traversal goal. */
    }
}

void bot_decision_evaluate(struct bot_decision_inputs const *inputs,
    float const outputs[BOT_GOAL_OUTPUT_COUNT], float scores[BOT_BEHAVIOR_COUNT])
{
    uint32_t index;
    uint32_t objective;
    assert(inputs != NULL);
    assert(outputs != NULL);
    assert(scores != NULL);
    for (index = 0; index < BOT_BEHAVIOR_COUNT; index++) {
        scores[index] = 0.f;
    }
    scores[_bot_leaf_retreat - 1] = -2.f; /* legacy CE root leaf, not native disengage */
    if (inputs->seated) {
        scores[_bot_leaf_vehicle - 1] = outputs[BOT_GOAL_PASSENGER_RIDE];
        return;
    }
    scores[_bot_leaf_roam - 1] = outputs[BOT_GOAL_TRAVERSAL];
    objective = bot_decision_objective_leaf(inputs->objective_kind);
    if (inputs->objective_valid && objective != _bot_leaf_none) {
        scores[objective - 1] = outputs[bot_decision_objective_output(inputs->objective_kind)];
        if (inputs->objective_kind == _bot_ambition_hold || inputs->objective_kind == _bot_ambition_reach) {
            scores[_bot_leaf_roam - 1] = 0.f; /* same traversal evaluator, authored destination */
        }
    }
    if (inputs->opportunity_relevant) {
        uint32_t pickup = bot_decision_pickup_leaf(inputs->opportunity_vehicle,
            inputs->opportunity_powerup);
        scores[pickup - 1] = outputs[inputs->opportunity_vehicle ? BOT_GOAL_DRIVER_ENTER :
            (inputs->opportunity_powerup ? BOT_GOAL_EQUIPMENT : BOT_GOAL_WEAPON)];
    }
    if (inputs->navigation_only) {
        return;
    }
    scores[_bot_leaf_fight - 1] = inputs->fight_disabled ? -2.f :
        (inputs->target_known ? (inputs->playfighting ? 2.f : 0.5f) : 0.f);
    scores[_bot_leaf_avoid - 1] = inputs->danger ? 0.9f : 0.f;
    /* Disengage is a child combat action, not the old shield-weighted root
     * retreat policy. bot_combat.c owns its recovered 0.7 evaluator. */
}

engine_ai_utility_decision bot_decision_select(engine_ai_utility_selector *selector,
    engine_ai_utility_selector_config const *config, float const *scores,
    uint64_t tick, bool force, bot_decision_gate gate, void *context, uint32_t *chosen)
{
    uint32_t pass;
    uint32_t index;
    uint32_t best = ENGINE_AI_UTILITY_NONE;
    uint32_t tried = 0;
    uint32_t previous;
    float hysteresis;
    if (!selector || !selector->initialized || !scores || !chosen || !gate ||
        !engine_ai_utility_config_valid(config) || config->option_count > 32) {
        return ENGINE_AI_UTILITY_INVALID;
    }
    for (index = 0; index < config->option_count; index++) {
        if (!isfinite(scores[index])) {
            return ENGINE_AI_UTILITY_INVALID;
        }
    }
    previous = selector->current;
    if (!force && previous < config->option_count && scores[previous] > 0.f &&
        tick >= selector->chosen_tick && tick < selector->next_decision_tick) {
        *chosen = previous;
        return ENGINE_AI_UTILITY_WAITING;
    }
    hysteresis = engine_ai_utility_hysteresis(selector, config, tick);
    for (pass = 0; pass < config->option_count; pass++) {
        float best_score = -1.f;
        best = ENGINE_AI_UTILITY_NONE;
        for (index = 0; index < config->option_count; index++) {
            float score = scores[index] + (index == previous && scores[index] > 0.f ? hysteresis : 0.f);
            if (!(tried & (1u << index)) && score > best_score) {
                best = index;
                best_score = score;
            }
        }
        if (best == ENGINE_AI_UTILITY_NONE || gate(context, best + 1)) {
            break;
        }
        tried |= 1u << best;
        best = ENGINE_AI_UTILITY_NONE;
    }
    selector->next_decision_tick = config->decision_interval > UINT64_MAX - tick ?
        UINT64_MAX : tick + config->decision_interval;
    if (best != previous || tick < selector->chosen_tick) {
        selector->chosen_tick = tick;
    }
    selector->current = best;
    *chosen = best;
    return best == ENGINE_AI_UTILITY_NONE ? ENGINE_AI_UTILITY_IDLE :
        (best == previous ? ENGINE_AI_UTILITY_KEPT : ENGINE_AI_UTILITY_CHANGED);
}
