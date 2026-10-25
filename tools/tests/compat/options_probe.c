/* Prints what network_rules does with 16 option bytes (hex args 1..16), the
engine (argv[17]: 1 slayer) and teams (argv[18]): "mode hardcore sanitized-hex".
Also folds a full 13,136-byte record carrying them at 0x1C and checks they
come back out unchanged. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_rules.h"

int main(int argc, char **argv)
{
	uint8_t options[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	static uint8_t record[NETWORK_RULES_SETTINGS_EXTENDED_SIZE];
	uint8_t extended[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	int hardcore = 0, mode, i;

	if (argc != 19)
		return 2;
	for (i = 0; i < NETWORK_RULES_EXTENDED_OPTIONS_SIZE; i++)
		options[i] = (uint8_t)strtoul(argv[1 + i], NULL, 16);
	/* the wire: the head (0x3320 bytes), the gametype options (the 16 further
	ones from 0x1C of them), then 4 bytes of local data */
	memset(record, 0x5A, sizeof(record));
	memcpy(record + NETWORK_RULES_SETTINGS_HEAD_SIZE + NETWORK_RULES_OPTIONS_SIZE, options, sizeof(options));
	network_rules_settings_fold_options(record, extended);
	if (memcmp(extended, options, sizeof(options)))
		return 3;
	network_rules_extended_options_sanitize(options);
	mode = network_rules_extended_options_mode(options, atoi(argv[17]), atoi(argv[18]), &hardcore);
	printf("%d %d", mode, hardcore);
	for (i = 0; i < NETWORK_RULES_EXTENDED_OPTIONS_SIZE; i++)
		printf(" %02x", options[i]);
	printf("\n");
	return 0;
}
