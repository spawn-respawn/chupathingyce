/* TEMPORARY CE policy. Replace this supplier when Infinite's scoring Lua is
 * recovered. It supplies goal outputs only; no selection, claims or combat. */
#ifndef HALO_BOT_GOAL_POLICY_CE_H
#define HALO_BOT_GOAL_POLICY_CE_H
#include "bot_decision.h"

/* CTF ambition priorities are CE-authored too, not recovered Lua outputs. */
float bot_goal_policy_ce_objective_value(enum bot_ambition_kind kind);

void bot_goal_policy_ce_outputs(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs, float outputs[BOT_GOAL_OUTPUT_COUNT]);
#endif
