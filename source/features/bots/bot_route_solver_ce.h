/* TEMPORARY CE BSP route-search adapter. Infinite's upstream solver has not
 * been recovered. No behavior scoring, goal selection or route following here.
 * Host simulation thread only; bounded shared workspaces, owned token results. */
#ifndef HALO_BOT_ROUTE_SOLVER_CE_H
#define HALO_BOT_ROUTE_SOLVER_CE_H
#include "engine_ai/surface_navigation_jobs.h"

void bot_route_solver_ce_reset(void);
void bot_route_solver_ce_update(void);
void bot_route_solver_ce_cancel(uint32_t client, uint64_t token);
bool bot_route_solver_ce_active(uint32_t client, uint64_t token);
bool bot_route_solver_ce_refresh(uint32_t client, uint64_t token,
    struct sn_point start, struct sn_point goal);
/* token=0 means submission failed; tokens never repeat, including map resets. */
uint64_t bot_route_solver_ce_begin(uint32_t client, long unit_index,
    struct sn_resource const *resource, struct sn_point start, struct sn_point goal,
    bool check_goal_segment);
enum sn_status bot_route_solver_ce_poll(uint32_t client, uint64_t token, long unit_index,
    struct sn_resource const *resource, struct sn_route *route,
    struct sn_jobs_diagnostics *diagnostics);
#endif
