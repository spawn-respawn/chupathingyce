/* TEMPORARY CE BSP solver and bounded search scheduling. The recovered
 * Infinite request consumer does not establish how Infinite searches routes.
 * Replace this module, not the follower, when that solver is recovered. */
#include "cseries.h"
#include "game.h"
#include "objects/objects.h"
#include "units/units.h"
#include "navigation_world.h"
#include "bot_route_solver_ce.h"

#ifdef HALO_FEATURE_BOTS

/* Only this host's simulation thread uses these. Client n is bot number n+1.
Tokens are never reused, even across map resets, so a delayed consumer cannot
take another request's work; no borrowed unit pointers survive a tick. */
static struct sn_jobs navigation_jobs;
static long navigation_units[SN_JOBS_CLIENTS_MAX]; /* the body its checks ignore */
static uint64_t navigation_tokens[SN_JOBS_CLIENTS_MAX]; /* its latest request */
static long navigation_polled[SN_JOBS_CLIENTS_MAX]; /* when its agent last asked */
static uint64_t navigation_token;

/* The scheduler's collision check for a client: the structure and scenery
(navigation_world_search_clear), ignoring the body its bot has now. */
static bool bot_nav_search_clear(void *context, uint32_t client, uint64_t token,
	struct sn_point a, uint32_t ap, struct sn_point b, uint32_t bp)
{
	struct unit_datum *unit;
	long unit_index;

	(void)context;
	if (client >= SN_JOBS_CLIENTS_MAX || token != navigation_tokens[client])
		return false;
	unit_index = navigation_units[client];
	unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return false;
	return navigation_world_search_clear(&unit_index, a, ap, b, bp);
}

void bot_route_solver_ce_reset(void)
{
	uint32_t client;

	sn_jobs_reset(&navigation_jobs);
	for (client = 0; client < SN_JOBS_CLIENTS_MAX; client++)
	{
		navigation_units[client] = NONE;
		navigation_tokens[client] = 0;
		navigation_polled[client] = NONE;
	}
}

void bot_route_solver_ce_update(void)
{
	struct sn_resource const *resource = navigation_world_resource();
	long now = game_time_get();
	uint32_t client;

	if (!resource)
	{
		bot_route_solver_ce_reset();
		return;
	}
	/* (at most 32 clients) A request its agent did not ask after last tick
	was abandoned: its leaf changed, or its bot left or was replaced. One whose
	body died or was deleted cannot be checked. Either gives its place back. */
	for (client = 0; client < SN_JOBS_CLIENTS_MAX; client++)
	{
		struct unit_datum *unit;

		if (!sn_jobs_active(&navigation_jobs, client, navigation_tokens[client]))
			continue;
		unit = navigation_units[client] != NONE ? unit_try_and_get(navigation_units[client]) : NULL;
		if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit) &&
			navigation_polled[client] != NONE && navigation_polled[client] <= now &&
			now - navigation_polled[client] <= 1)
		{
			continue;
		}
		sn_jobs_cancel(&navigation_jobs, client, navigation_tokens[client]);
		navigation_units[client] = NONE;
	}
	sn_jobs_step(&navigation_jobs, resource, 0,
		(uint64_t)now, bot_nav_search_clear, NULL);
}


void bot_route_solver_ce_cancel(uint32_t client, uint64_t token)
{
    if (client >= SN_JOBS_CLIENTS_MAX ||
        (token && token != navigation_tokens[client]))
        return;
    sn_jobs_cancel(&navigation_jobs, client, navigation_tokens[client]);
    navigation_units[client] = NONE;
}
bool bot_route_solver_ce_active(uint32_t client, uint64_t token)
{
    return client < SN_JOBS_CLIENTS_MAX &&
        sn_jobs_active(&navigation_jobs, client, token);
}
bool bot_route_solver_ce_refresh(uint32_t client, uint64_t token,
    struct sn_point start, struct sn_point goal)
{
    return client < SN_JOBS_CLIENTS_MAX &&
        sn_jobs_refresh(&navigation_jobs, client, token, start, goal);
}
uint64_t bot_route_solver_ce_begin(uint32_t client, long unit_index,
    struct sn_resource const *resource, struct sn_point start, struct sn_point goal,
    bool check_goal_segment)
{
    struct sn_jobs_request request;
    if (client >= SN_JOBS_CLIENTS_MAX || navigation_token == UINT64_MAX)
        return 0;
    bot_route_solver_ce_cancel(client, 0);
    csmemset(&request, 0, sizeof(request));
    request.start = start;
    request.goal = goal;
    request.ground_depth = 3.f; /* temporary CE endpoint projection policy */
    request.ground_horizontal = 1.f;
    request.obstacle_revision = 0;
    request.check_goal_segment = check_goal_segment;
    request.straighten = true;
    navigation_tokens[client] = ++navigation_token;
    navigation_units[client] = unit_index;
    navigation_polled[client] = game_time_get();
    if (!sn_jobs_submit(&navigation_jobs, client, navigation_tokens[client], resource, &request))
        return 0;
    return navigation_tokens[client];
}
enum sn_status bot_route_solver_ce_poll(uint32_t client, uint64_t token, long unit_index,
    struct sn_resource const *resource, struct sn_route *route,
    struct sn_jobs_diagnostics *diagnostics)
{
    if (client >= SN_JOBS_CLIENTS_MAX || token != navigation_tokens[client])
        return SN_STALE;
    navigation_units[client] = unit_index;
    navigation_polled[client] = game_time_get();
    if (sn_jobs_status(&navigation_jobs, client, token) == SN_RUNNING)
        return SN_RUNNING;
    return sn_jobs_take(&navigation_jobs, client, token, resource, 0, route, diagnostics);
}
#endif /* HALO_FEATURE_BOTS */
