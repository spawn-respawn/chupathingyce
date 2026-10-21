/* Recovered evaluator/selector contract, independent of the temporary policy. */
#include "../source/features/bots/bot_decision.h"
#include "../source/features/bots/bot_goal_policy_ce.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.00001f);
}

static void evaluator_split(void)
{
    struct bot_decision_inputs in = {0};
    float outputs[BOT_GOAL_OUTPUT_COUNT] = {0};
    float scores[BOT_BEHAVIOR_COUNT];
    outputs[BOT_GOAL_TRAVERSAL] = 0.15f;
    outputs[BOT_GOAL_UNUSED_FIGHT] = 99.f;
    in.target_known = true;
    for (unsigned int source = 1; source <= 4; source++) {
        in.target_source = (enum bot_target_source)source;
        in.confidence = 1.f; in.target_distance = 100.f;
        bot_decision_evaluate(&in, outputs, scores);
        near(scores[_bot_leaf_fight - 1], 0.5f); /* NOT CE confidence/distance/source curves */
        near(scores[_bot_leaf_retreat - 1], -2.f); /* disengage belongs to combat child */
    }
    in.playfighting = true;
    bot_decision_evaluate(&in, outputs, scores);
    near(scores[_bot_leaf_fight - 1], 2.f); /* native utility may exceed one */
    in.fight_disabled = true;
    bot_decision_evaluate(&in, outputs, scores);
    near(scores[_bot_leaf_fight - 1], -2.f);
    in.fight_disabled = false; in.playfighting = false;
    in.objective_valid = true; in.objective_kind = _bot_ambition_take;
    outputs[BOT_GOAL_PICKUP] = 0.8f;
    in.danger = true;
    bot_decision_evaluate(&in, outputs, scores);
    near(scores[_bot_leaf_take - 1], 0.8f);
    near(scores[_bot_leaf_fight - 1], 0.5f); /* a goal does not suppress native evaluation */
    near(scores[_bot_leaf_avoid - 1], 0.9f);
    in.objective_kind = _bot_ambition_deliver;
    outputs[BOT_GOAL_DELIVER] = 1.2f;
    bot_decision_evaluate(&in, outputs, scores);
    near(scores[_bot_leaf_deliver - 1], 1.2f);
    near(scores[_bot_leaf_take - 1], 0.f);
    in.navigation_only = true;
    bot_decision_evaluate(&in, outputs, scores);
    near(scores[_bot_leaf_fight - 1], 0.f); near(scores[_bot_leaf_avoid - 1], 0.f);
}

struct gate_state { uint32_t rejected, calls[32], count; };
static bool gate(void *context, uint32_t leaf)
{
    struct gate_state *state = context;
    assert(state->count < 32);
    state->calls[state->count++] = leaf;
    return leaf != state->rejected;
}

static void selector_and_gate(void)
{
    engine_ai_utility_selector selector;
    engine_ai_utility_selector_config config = {BOT_BEHAVIOR_COUNT, 8, 30, 0.15f, 99.f};
    struct gate_state state = {0};
    uint32_t chosen = 99;
    float scores[BOT_BEHAVIOR_COUNT];
    for (uint32_t i = 0; i < BOT_BEHAVIOR_COUNT; i++) scores[i] = -2.f;
    scores[_bot_leaf_take - 1] = 0.98f;
    scores[_bot_leaf_guard - 1] = 0.8f;
    scores[_bot_leaf_fight - 1] = 0.5f;
    engine_ai_utility_selector_init(&selector, 0);
    state.rejected = _bot_leaf_take;
    assert(bot_decision_select(&selector, &config, scores, 10, true, gate, &state, &chosen)
        == ENGINE_AI_UTILITY_CHANGED);
    assert(chosen == _bot_leaf_guard - 1 && state.count == 2);
    assert(state.calls[0] == _bot_leaf_take && state.calls[1] == _bot_leaf_guard);
    assert(selector.chosen_tick == 10);
    state.count = 0;
    assert(bot_decision_select(&selector, &config, scores, 11, false, gate, &state, &chosen)
        == ENGINE_AI_UTILITY_WAITING && state.count == 0);
    assert(bot_decision_select(&selector, &config, scores, 20, true, gate, &state, &chosen)
        == ENGINE_AI_UTILITY_KEPT && selector.chosen_tick == 10);
    state.rejected = 0; state.count = 0;
    assert(bot_decision_select(&selector, &config, scores, 50, true, gate, &state, &chosen)
        == ENGINE_AI_UTILITY_CHANGED);
    assert(chosen == _bot_leaf_take - 1 && selector.chosen_tick == 50);
    /* No CE switch margin, no clamping of native 2.0. */
    scores[_bot_leaf_fight - 1] = 2.f;
    assert(bot_decision_select(&selector, &config, scores, 51, true, gate, &state, &chosen)
        == ENGINE_AI_UTILITY_CHANGED && chosen == _bot_leaf_fight - 1);
    scores[0] = NAN; state.count = 0;
    assert(bot_decision_select(&selector, &config, scores, 52, true, gate, &state, &chosen)
        == ENGINE_AI_UTILITY_INVALID && state.count == 0);
    assert(selector.chosen_tick == 51);
}

static void temporary_supplier(void)
{
    struct bot_decision_inputs in = {0};
    float scores[BOT_BEHAVIOR_COUNT];
    float outputs[BOT_GOAL_OUTPUT_COUNT];
    struct bot_utility_tuning const *t = bot_decision_default_tuning();
    in.objective_valid = true; in.objective_value = 0.97f; in.target_known = true;
    for (int kind = _bot_ambition_take; kind < NUMBER_OF_BOT_AMBITION_KINDS; kind++) {
        uint32_t leaf = bot_decision_objective_leaf((enum bot_ambition_kind)kind);
        in.objective_kind = (enum bot_ambition_kind)kind;
        bot_goal_policy_ce_outputs(t, &in, outputs);
        bot_decision_evaluate(&in, outputs, scores);
        near(scores[leaf - 1], 0.97f);
        near(scores[_bot_leaf_fight - 1], 0.5f);
    }
    in.opportunity_vehicle = in.opportunity_relevant = in.offered_ride = true;
    in.objective_kind = _bot_ambition_deliver;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_enter_vehicle - 1], 0.f); /* CE carrier constraint */
    near(scores[_bot_leaf_deliver - 1], 0.97f);
    assert(bot_decision_pickup_leaf(false, true) == _bot_leaf_acquire_equipment);
    assert(bot_decision_objective_leaf((enum bot_ambition_kind)99) == _bot_leaf_none);
}

int main(void)
{
    evaluator_split(); selector_and_gate(); temporary_supplier();
    puts("Recovered evaluator split, ambition-gated selection and temporary supplier passed");
    return 0;
}
