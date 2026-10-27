#include <assert.h>
#include "swat_rules.h"

int main(void)
{
	/* a shot to a player's head kills at once */
	assert(swat_rules_headshot_kills(1, 1));
	/* not a melee, grenade or vehicle hit (not a shot) */
	assert(!swat_rules_headshot_kills(0, 1));
	/* not a non-player biped (an AI, a dead body's) */
	assert(!swat_rules_headshot_kills(1, 0));
	assert(!swat_rules_headshot_kills(0, 0));
	assert(SWAT_SHOT_CATEGORY_COUNT == 5);
	return 0;
}
