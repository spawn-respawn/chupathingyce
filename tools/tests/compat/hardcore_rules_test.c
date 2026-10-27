#include <assert.h>
#include "hardcore_rules.h"

static struct hardcore_vitals vitals(float body, float shield, float maximum, int depleted, int charging)
{
	struct hardcore_vitals v;

	v.body = body; v.shield = shield; v.maximum_shield = maximum; v.shield_depleted = depleted; v.shield_charging = charging;
	return v;
}

int main(void)
{
	struct hardcore_vitals v;

	/* the host restores after another player's clean kill, with the killer alive */
	assert(hardcore_rules_restores(1, 0, 0, 1, 0, 1, 1));
	assert(!hardcore_rules_restores(0, 0, 0, 1, 0, 1, 1)); /* not Hardcore */
	assert(!hardcore_rules_restores(1, 1, 0, 1, 0, 1, 1)); /* a client: the host's word reaches it */
	assert(!hardcore_rules_restores(1, 0, 1, 1, 0, 1, 1)); /* friendly fire */
	assert(!hardcore_rules_restores(1, 0, 0, 0, 0, 1, 1)); /* the world's kill */
	assert(!hardcore_rules_restores(1, 0, 0, 1, 1, 1, 1)); /* a suicide */
	assert(!hardcore_rules_restores(1, 0, 0, 1, 0, 0, 1)); /* the killer has no unit */
	assert(!hardcore_rules_restores(1, 0, 0, 1, 0, 1, 0)); /* the killer died too */

	/* health to full, a depleted charging shield to full and settled */
	v = vitals(0.2f, 0.0f, 1.0f, 1, 1);
	hardcore_rules_restore(&v);
	assert(v.body == 1.0f && v.shield == 1.0f && !v.shield_depleted && !v.shield_charging);
	/* a partial shield to full */
	v = vitals(0.5f, 0.4f, 1.0f, 0, 1);
	hardcore_rules_restore(&v);
	assert(v.shield == 1.0f && !v.shield_charging);
	/* an overshield past full is kept, and its flags are not touched */
	v = vitals(0.5f, 2.0f, 1.0f, 0, 1);
	hardcore_rules_restore(&v);
	assert(v.body == 1.0f && v.shield == 2.0f && v.shield_charging);
	/* no shields to have (SWAT-like): health only, nothing invented */
	v = vitals(0.3f, 0.0f, 0.0f, 0, 0);
	hardcore_rules_restore(&v);
	assert(v.body == 1.0f && v.shield == 0.0f);
	return 0;
}
