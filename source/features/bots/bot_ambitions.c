/* Rank without side effects, then replace exactly one bot's assignment.
 * Limits: 16 published rules, 31 owners, no allocation or object reads. */
#include "bot_ambitions.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

static bool bot_ambition_actor_valid(struct bot_ambition_actor const *actor)
{
    return actor && actor->bot_number > 0 && actor->bot_number <= BOT_AMBITION_BOTS_MAX &&
        actor->player_key != UINT32_MAX && (!actor->has_teams || actor->team_index < 32);
}

static bool bot_ambition_allowed(struct bot_ambition_rule const *rule,
    struct bot_ambition_actor const *actor)
{
    uint32_t bit;
    if (!bot_ambition_actor_valid(actor) || rule->slot_limit == 0) {
        return false;
    }
    bit = 1u << (actor->bot_number - 1);
    if ((rule->exclude_bots & bit) || (!rule->all_bots && !(rule->include_bots & bit))) {
        return false;
    }
    if (rule->specific_player != BOT_AMBITION_ANY_PLAYER && rule->specific_player != actor->player_key) {
        return false;
    }
    return !actor->has_teams || (rule->team_mask & (1u << actor->team_index)) != 0;
}

bool bot_ambition_identity_equal(struct bot_ambition_identity const *a,
    struct bot_ambition_identity const *b)
{
    assert(a != NULL);
    assert(b != NULL);
    return a->kind == b->kind && a->acting_team == b->acting_team &&
        a->object_key == b->object_key && a->carrier_key == b->carrier_key &&
        a->source_id == b->source_id;
}

void bot_ambitions_reset(struct bot_ambitions *state)
{
    assert(state != NULL);
    memset(state, 0, sizeof(*state));
}

static void bot_ambitions_clock(struct bot_ambitions *state, int64_t tick)
{
    assert(tick >= 0);
    if (state->initialized && tick < state->last_tick) {
        memset(state->claims, 0, sizeof(state->claims));
    }
    state->last_tick = tick;
    state->initialized = true;
}

static uint32_t bot_ambitions_index(struct bot_ambitions const *state,
    struct bot_ambition_identity const *identity)
{
    uint32_t index;
    for (index = 0; index < state->count; index++) {
        if (bot_ambition_identity_equal(&state->rules[index].identity, identity)) {
            return index;
        }
    }
    return BOT_AMBITION_CAPACITY;
}

static void bot_ambitions_prune(struct bot_ambitions *state)
{
    uint32_t owner;
    for (owner = 0; owner < BOT_AMBITION_BOTS_MAX; owner++) {
        struct bot_ambition_claim *claim = &state->claims[owner];
        uint32_t index;
        uint32_t previous;
        uint32_t held = 0;
        if (!claim->active) {
            continue;
        }
        index = bot_ambitions_index(state, &claim->identity);
        if (index == BOT_AMBITION_CAPACITY || claim->type != state->rules[index].type ||
            !bot_ambition_allowed(&state->rules[index], &claim->actor)) {
            claim->active = false;
            continue;
        }
        for (previous = 0; previous < owner; previous++) {
            if (state->claims[previous].active && bot_ambition_identity_equal(
                &state->claims[previous].identity, &claim->identity)) {
                held++;
            }
        }
        if (held >= state->rules[index].slot_limit) {
            claim->active = false;
        }
    }
}

bool bot_ambitions_publish(struct bot_ambitions *state,
    struct bot_ambition_rule const *rules, uint32_t count, int64_t tick)
{
    uint32_t index;
    uint32_t previous;
    assert(state != NULL);
    if (count > BOT_AMBITION_CAPACITY || (count != 0 && rules == NULL) || tick < 0) {
        return false;
    }
    for (index = 0; index < count; index++) {
        if (rules[index].identity.kind == 0 || rules[index].identity.kind >= 32 ||
            rules[index].type < 0 || rules[index].type >= BOT_AMBITION_TYPE_COUNT ||
            (rules[index].slot_limit > BOT_AMBITION_BOTS_MAX &&
             rules[index].slot_limit != BOT_AMBITION_UNLIMITED) ||
            ((rules[index].include_bots | rules[index].exclude_bots) & 0x80000000u)) {
            return false;
        }
        for (previous = 0; previous < index; previous++) {
            if (bot_ambition_identity_equal(&rules[previous].identity, &rules[index].identity)) {
                return false;
            }
        }
    }
    bot_ambitions_clock(state, tick);
    for (index = 0; index < count; index++) {
        state->rules[index] = rules[index];
    }
    state->count = count;
    bot_ambitions_prune(state);
    return true;
}

uint32_t bot_ambitions_claimant_count(struct bot_ambitions const *state,
    uint32_t index, int64_t tick)
{
    uint32_t count = 0;
    uint32_t owner;
    assert(state != NULL);
    if (!state->initialized || index >= state->count || tick < state->last_tick) {
        return 0;
    }
    for (owner = 0; owner < BOT_AMBITION_BOTS_MAX; owner++) {
        struct bot_ambition_claim const *claim = &state->claims[owner];
        if (claim->active &&
            bot_ambition_identity_equal(&claim->identity, &state->rules[index].identity)) {
            count++;
        }
    }
    return count;
}

bool bot_ambitions_can_claim(struct bot_ambitions const *state,
    struct bot_ambition_actor const *actor, uint32_t index, int64_t tick)
{
    struct bot_ambition_claim const *own;
    assert(state != NULL);
    if (!state->initialized || index >= state->count || tick < state->last_tick ||
        !bot_ambition_allowed(&state->rules[index], actor)) {
        return false;
    }
    own = &state->claims[actor->bot_number - 1];
    if (own->active &&
        bot_ambition_identity_equal(&own->identity, &state->rules[index].identity)) {
        return true;
    }
    return bot_ambitions_claimant_count(state, index, tick) < state->rules[index].slot_limit;
}

bool bot_ambitions_try_claim(struct bot_ambitions *state,
    struct bot_ambition_actor const *actor, uint32_t index, int64_t tick)
{
    struct bot_ambition_claim *own;
    assert(state != NULL);
    if (!state->initialized || !bot_ambition_actor_valid(actor) || tick < state->last_tick) {
        return false;
    }
    bot_ambitions_clock(state, tick);
    if (!bot_ambitions_can_claim(state, actor, index, tick)) {
        return false;
    }
    /* Native unlimited ambitions neither decrement a slot nor replace a
     * limited claim. The executing behavior can explicitly release its old one. */
    if (state->rules[index].slot_limit == BOT_AMBITION_UNLIMITED) {
        return true;
    }
    own = &state->claims[actor->bot_number - 1];
    own->type = state->rules[index].type;
    own->identity = state->rules[index].identity;
    own->actor = *actor;
    own->active = true;
    return true;
}

void bot_ambitions_release(struct bot_ambitions *state, uint32_t bot_number,
    struct bot_ambition_identity const *expected)
{
    struct bot_ambition_claim *claim;
    assert(state != NULL);
    if (bot_number == 0 || bot_number > BOT_AMBITION_BOTS_MAX) {
        return;
    }
    claim = &state->claims[bot_number - 1];
    if (claim->active && (!expected || bot_ambition_identity_equal(&claim->identity, expected))) {
        claim->active = false;
    }
}

bool bot_ambitions_has_claim(struct bot_ambitions const *state, uint32_t bot_number,
    struct bot_ambition_identity const *identity, int64_t tick)
{
    struct bot_ambition_claim const *claim;
    uint32_t index;
    assert(state != NULL);
    if (!identity || bot_number == 0 || bot_number > BOT_AMBITION_BOTS_MAX ||
        !state->initialized || tick < state->last_tick) {
        return false;
    }
    claim = &state->claims[bot_number - 1];
    index = bot_ambitions_index(state, identity);
    return claim->active && index < state->count &&
        bot_ambition_identity_equal(&claim->identity, identity) &&
        bot_ambition_allowed(&state->rules[index], &claim->actor);
}

enum bot_ambition_type bot_ambition_type_for_kind(enum bot_ambition_kind kind)
{
    switch (kind) {
    case _bot_ambition_take:
    case _bot_ambition_return: return BOT_AMBITION_PICKUP;
    case _bot_ambition_deliver: return BOT_AMBITION_DELIVER;
    case _bot_ambition_guard: return BOT_AMBITION_GUARD;
    case _bot_ambition_hunt: return BOT_AMBITION_HUNT;
    case _bot_ambition_hide: return BOT_AMBITION_HIDE;
    default: return BOT_AMBITION_POI;
    }
}

bool bot_ambitions_find(struct bot_ambitions *state,
    struct bot_ambition_actor const *actor, enum bot_ambition_type type,
    struct bot_ambition_score const *scores, int64_t tick, bool claim, uint32_t *selected)
{
    uint32_t best = BOT_AMBITION_CAPACITY;
    uint32_t index;
    assert(state != NULL);
    if (!state->initialized || !bot_ambition_actor_valid(actor) || !scores || !selected ||
        tick < state->last_tick || type < 0 || type >= BOT_AMBITION_TYPE_COUNT) {
        return false;
    }
    for (index = 0; index < state->count; index++) {
        if (!isfinite(scores[index].utility) || !isfinite(scores[index].distance) ||
            scores[index].distance < 0.f) {
            return false;
        }
    }
    if (type == BOT_AMBITION_GUARD && actor->ranks_guards) {
        for (index = 0; index < state->count; index++) {
            if (state->rules[index].type == BOT_AMBITION_GUARD_RANKED) {
                type = BOT_AMBITION_GUARD_RANKED;
                break;
            }
        }
    }
    for (index = 0; index < state->count; index++) {
        if (state->rules[index].type != type || scores[index].utility <= 0.f ||
            !bot_ambition_allowed(&state->rules[index], actor) ||
            (claim && !bot_ambitions_can_claim(state, actor, index, tick))) {
            continue;
        }
        if (best == BOT_AMBITION_CAPACITY ||
            (type == BOT_AMBITION_GUARD_RANKED ?
                scores[index].utility > scores[best].utility :
                scores[index].distance < scores[best].distance)) {
            best = index;
        }
    }
    if (best == BOT_AMBITION_CAPACITY ||
        (claim && !bot_ambitions_try_claim(state, actor, best, tick))) {
        return false;
    }
    *selected = best;
    return true;
}
