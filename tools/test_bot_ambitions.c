#include "../source/features/bots/bot_ambitions.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct bot_ambition_rule rule(uint32_t key, enum bot_ambition_type type, uint32_t slots)
{
    struct bot_ambition_rule r = {0};
    r.identity.kind = 1;
    r.identity.object_key = key;
    r.identity.carrier_key = UINT32_MAX;
    r.type = type;
    r.team_mask = 1;
    r.specific_player = BOT_AMBITION_ANY_PLAYER;
    r.slot_limit = slots;
    r.all_bots = true;
    return r;
}

static struct bot_ambition_actor actor(uint32_t number)
{
    struct bot_ambition_actor a = {0};
    a.bot_number = number;
    a.player_key = number + 100;
    a.has_teams = true;
    return a;
}

static void nearest_and_ownership(void)
{
    struct bot_ambitions state, before;
    struct bot_ambition_actor a = actor(1), b = actor(2);
    struct bot_ambition_rule rules[3] = {
        rule(10, BOT_AMBITION_PICKUP, 1), rule(20, BOT_AMBITION_PICKUP, 1),
        rule(30, BOT_AMBITION_GUARD, BOT_AMBITION_UNLIMITED)
    };
    struct bot_ambition_score scores[3] = {{0.9f, 100.f}, {0.1f, 4.f}, {1.f, 0.f}};
    uint32_t selected = 99;
    bot_ambitions_reset(&state);
    assert(bot_ambitions_publish(&state, rules, 3, 0));
    before = state;
    assert(bot_ambitions_find(&state, &a, BOT_AMBITION_PICKUP, scores, 0, false, &selected));
    assert(selected == 1 && !memcmp(&state, &before, sizeof(state))); /* peek is pure */
    assert(bot_ambitions_find(&state, &a, BOT_AMBITION_PICKUP, scores, 0, true, &selected));
    assert(selected == 1); /* nearest, NOT greatest utility or another type */
    assert(bot_ambitions_claimant_count(&state, 1, 0) == 1);
    assert(bot_ambitions_find(&state, &b, BOT_AMBITION_PICKUP, scores, 0, false, &selected));
    assert(selected == 1); /* native nonclaim lookup ignores occupied slots */
    assert(bot_ambitions_find(&state, &b, BOT_AMBITION_PICKUP, scores, 0, true, &selected));
    assert(selected == 0);
    assert(!bot_ambitions_try_claim(&state, &a, 0, 1));
    assert(bot_ambitions_has_claim(&state, 1, &rules[1].identity, 100000));
    assert(bot_ambitions_publish(&state, rules, 3, 100000));
    assert(bot_ambitions_has_claim(&state, 1, &rules[1].identity, 100000)); /* no lease */
    assert(bot_ambitions_try_claim(&state, &a, 1, 100000));
    assert(bot_ambitions_claimant_count(&state, 1, 100000) == 1);
    assert(bot_ambitions_find(&state, &a, BOT_AMBITION_GUARD, scores, 100000, true, &selected));
    assert(selected == 2 && bot_ambitions_claimant_count(&state, 2, 100000) == 0);
    assert(bot_ambitions_has_claim(&state, 1, &rules[1].identity, 100000));
    bot_ambitions_release(&state, 2, NULL);
    assert(bot_ambitions_try_claim(&state, &a, 0, 100000));
    assert(bot_ambitions_claimant_count(&state, 1, 100000) == 0);
    bot_ambitions_release(&state, 1, &rules[1].identity); /* stale cancellation */
    assert(bot_ambitions_has_claim(&state, 1, &rules[0].identity, 100000));
    scores[0].utility = scores[1].utility = 0.f;
    assert(!bot_ambitions_find(&state, &a, BOT_AMBITION_PICKUP, scores, 100000, true, &selected));
    assert(bot_ambitions_has_claim(&state, 1, &rules[0].identity, 100000));
    bot_ambitions_release(&state, 1, &rules[0].identity);
    assert(bot_ambitions_claimant_count(&state, 0, 100000) == 0);
}

static void ranking_and_filters(void)
{
    struct bot_ambitions state;
    struct bot_ambition_actor a = actor(1), invalid = actor(32);
    struct bot_ambition_rule rules[3] = {
        rule(10, BOT_AMBITION_GUARD, 1), rule(20, BOT_AMBITION_GUARD_RANKED, 1),
        rule(30, BOT_AMBITION_GUARD_RANKED, 1)
    };
    struct bot_ambition_score scores[3] = {{1.f, 1.f}, {0.8f, 4.f}, {0.9f, 100.f}};
    uint32_t selected = 99;
    bot_ambitions_reset(&state);
    assert(bot_ambitions_publish(&state, rules, 3, 0));
    assert(bot_ambitions_find(&state, &a, BOT_AMBITION_GUARD, scores, 0, true, &selected));
    assert(selected == 0);
    a.ranks_guards = true;
    assert(bot_ambitions_find(&state, &a, BOT_AMBITION_GUARD, scores, 0, true, &selected));
    assert(selected == 2); /* supplied rank, not nearest regular guard */
    rules[2].exclude_bots = 1;
    assert(bot_ambitions_publish(&state, rules, 3, 1));
    assert(!bot_ambitions_has_claim(&state, 1, &rules[2].identity, 1));
    assert(bot_ambitions_find(&state, &a, BOT_AMBITION_GUARD, scores, 1, true, &selected));
    assert(selected == 1);
    rules[1].all_bots = false; rules[1].include_bots = 1;
    rules[1].specific_player = a.player_key;
    assert(bot_ambitions_publish(&state, rules, 3, 2));
    assert(bot_ambitions_can_claim(&state, &a, 1, 2));
    assert(!bot_ambitions_can_claim(&state, &invalid, 1, 2));
    a.player_key++;
    assert(!bot_ambitions_can_claim(&state, &a, 1, 2));
    a.player_key--; a.team_index = 1;
    assert(!bot_ambitions_can_claim(&state, &a, 1, 2));
    a.has_teams = false;
    assert(bot_ambitions_can_claim(&state, &a, 1, 2));
    scores[0].utility = NAN;
    selected = 99;
    assert(!bot_ambitions_find(&state, &a, BOT_AMBITION_GUARD, scores, 2, true, &selected));
    assert(selected == 99);
}

static void publication_and_capacity(void)
{
    struct bot_ambitions state;
    struct bot_ambition_actor a = actor(1), b = actor(2);
    struct bot_ambition_rule rules[2] = {rule(0x80000005u, BOT_AMBITION_PICKUP, 2),
        rule(0x90000005u, BOT_AMBITION_PICKUP, 1)};
    struct bot_ambition_rule swapped[2] = {rules[1], rules[0]};
    bot_ambitions_reset(&state);
    assert(bot_ambitions_publish(&state, rules, 2, 0));
    assert(bot_ambitions_try_claim(&state, &a, 0, 0));
    assert(bot_ambitions_try_claim(&state, &b, 0, 0));
    assert(bot_ambitions_publish(&state, swapped, 2, 1));
    assert(bot_ambitions_claimant_count(&state, 1, 1) == 2); /* salted identity, not index */
    rules[0].slot_limit = 1;
    assert(bot_ambitions_publish(&state, rules, 2, 2));
    assert(bot_ambitions_has_claim(&state, 1, &rules[0].identity, 2));
    assert(!bot_ambitions_has_claim(&state, 2, &rules[0].identity, 2));
    rules[0].type = BOT_AMBITION_HUNT;
    assert(bot_ambitions_publish(&state, rules, 2, 3));
    assert(!bot_ambitions_has_claim(&state, 1, &rules[0].identity, 3));
    assert(bot_ambitions_try_claim(&state, &a, 0, 3));
    assert(bot_ambitions_publish(&state, rules, 2, 1)); /* rewind is an adapter reset */
    assert(bot_ambitions_claimant_count(&state, 0, 1) == 0);
    assert(!bot_ambitions_publish(&state, NULL, 1, 2));
    assert(!bot_ambitions_publish(&state, rules, BOT_AMBITION_CAPACITY + 1, 2));
    assert(bot_ambitions_publish(&state, NULL, 0, 2));
    assert(!bot_ambitions_can_claim(&state, &a, 0, 2));
    assert(bot_ambition_type_for_kind(_bot_ambition_return) == BOT_AMBITION_PICKUP);
}

int main(void)
{
    nearest_and_ownership();
    ranking_and_filters();
    publication_and_capacity();
    puts("ambitions: typed nearest/ranked lookup, explicit ownership, no leases passed");
    return 0;
}
