/* Recovered typed ambition lookup and explicit slot ownership.
 * Publication/world eligibility are supplied by CE; no leases or goal policy. */
#ifndef HALO_BOT_AMBITIONS_H
#define HALO_BOT_AMBITIONS_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    BOT_AMBITION_CAPACITY = 16,
    BOT_AMBITION_BOTS_MAX = 31
};
#define BOT_AMBITION_UNLIMITED UINT32_MAX
#define BOT_AMBITION_ANY_PLAYER UINT32_MAX

/* what a bot may do with an ambition */
enum bot_ambition_kind
{
	_bot_ambition_none = 0,
	_bot_ambition_take, /* pick the object up (a flag, a ball) */
	_bot_ambition_deliver, /* bring a carried object here (a flag stand) */
	_bot_ambition_guard, /* stay near it against enemies (a flag at home) */
	_bot_ambition_hold, /* stand inside it (a hill) */
	_bot_ambition_hunt, /* kill whoever carries it (an enemy flag carrier) */
	_bot_ambition_reach, /* touch it (a race flag) */
	_bot_ambition_return, /* touch one's dropped flag; no inventory acquisition */
	_bot_ambition_hide, /* reach an authored concealment point and crouch */
	NUMBER_OF_BOT_AMBITION_KINDS
};

/* Stable authored identity, never a publication-array index or position hash.
 * Object/carrier are full salted handles (UINT32_MAX for none). source_id
 * distinguishes authored areas with no object; kind is in 1..31. */
struct bot_ambition_identity
{
    uint32_t kind;
    uint32_t acting_team;
    uint32_t object_key;
    uint32_t carrier_key;
    uint32_t source_id;
};

enum bot_ambition_type
{
    BOT_AMBITION_PICKUP = 0, BOT_AMBITION_DELIVER, BOT_AMBITION_INTERACT,
    BOT_AMBITION_GUARD, BOT_AMBITION_POI, BOT_AMBITION_HUNT,
    BOT_AMBITION_GUARD_RANKED, BOT_AMBITION_CONTEST, BOT_AMBITION_WEAPON,
    BOT_AMBITION_EQUIPMENT, BOT_AMBITION_EXPLORE, BOT_AMBITION_HIDE,
    BOT_AMBITION_OBJECTIVE_PATH, BOT_AMBITION_TYPE_COUNT
};

enum bot_ambition_type bot_ambition_type_for_kind(enum bot_ambition_kind kind);

struct bot_ambition_rule
{
    enum bot_ambition_type type;
    struct bot_ambition_identity identity;
    uint32_t team_mask;
    uint32_t include_bots; /* bit (bot_number - 1) */
    uint32_t exclude_bots;
    uint32_t specific_player;
    uint32_t slot_limit; /* 0 disables; UINT32_MAX is unlimited */
    bool all_bots;
};

struct bot_ambition_actor
{
    uint32_t bot_number; /* 1..31 */
    uint32_t player_key;
    uint32_t team_index;
    bool has_teams;
    bool ranks_guards; /* active difficulty flag; rank values are supplied */
};

struct bot_ambition_score
{
    float utility; /* >0 = world-eligible; used as rank ONLY for ranked guards */
    float distance; /* squared Euclidean distance, NOT path cost */
};

struct bot_ambition_claim
{
    enum bot_ambition_type type;
    struct bot_ambition_identity identity;
    struct bot_ambition_actor actor;
    bool active;
};

struct bot_ambitions
{
    struct bot_ambition_rule rules[BOT_AMBITION_CAPACITY];
    struct bot_ambition_claim claims[BOT_AMBITION_BOTS_MAX]; /* at most one per bot */
    uint32_t count;
    int64_t last_tick;
    bool initialized;
};

void bot_ambitions_reset(struct bot_ambitions *state);
bool bot_ambition_identity_equal(struct bot_ambition_identity const *a,
    struct bot_ambition_identity const *b);

/* Atomic validated replacement. Claims survive reordering/position changes,
 * not removal, changed identity/type, eligibility changes or time rewind.
 * Reducing a slot limit keeps lower bot numbers deterministically. */
bool bot_ambitions_publish(struct bot_ambitions *state,
    struct bot_ambition_rule const *rules, uint32_t count, int64_t tick);

/* Read-only candidate eligibility, including available slots. */
bool bot_ambitions_can_claim(struct bot_ambitions const *state,
    struct bot_ambition_actor const *actor, uint32_t index, int64_t tick);

/* Claim only the chosen candidate; failure preserves the prior claim. Success
 * atomically replaces it. Renewing one's own claim consumes no extra slot. */
bool bot_ambitions_try_claim(struct bot_ambitions *state,
    struct bot_ambition_actor const *actor, uint32_t index, int64_t tick);

/* FindBestAmbition: nearest eligible candidate of one type. Ranked guards
 * prefer GuardRanked records and maximize supplied rank instead. Equal
 * distances/ranks retain publication order. Peek is read-only and consumes no
 * slots; claim replaces one limited assignment only after finding a winner.
 * Unlimited records do not consume slots or overwrite a limited assignment.
 * No winner or invalid input preserves prior ownership and *selected. */
bool bot_ambitions_find(struct bot_ambitions *state,
    struct bot_ambition_actor const *actor, enum bot_ambition_type type,
    struct bot_ambition_score const *scores, int64_t tick, bool claim, uint32_t *selected);

/* expected=NULL releases any claim; otherwise only that exact identity. A
 * cancelled old behavior must not release a newly selected ambition. */
void bot_ambitions_release(struct bot_ambitions *state, uint32_t bot_number,
    struct bot_ambition_identity const *expected);
bool bot_ambitions_has_claim(struct bot_ambitions const *state, uint32_t bot_number,
    struct bot_ambition_identity const *identity, int64_t tick);
uint32_t bot_ambitions_claimant_count(struct bot_ambitions const *state,
    uint32_t index, int64_t tick);

#endif
