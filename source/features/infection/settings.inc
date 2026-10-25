/* Infection's config.toml settings (rows of port_config.c's table). The
options are OpenCE's, with its ranges (an out-of-range value is its default). */

	{ "infection.host", _config_boolean, "false", "HALO_INFECTION_HOST", _environment_value, _platform_all,
		"Host the next team Slayer game as OpenCE-compatible Infection: the\n"
		"zombies are blue, a human they kill joins them. A free for all game is\n"
		"unchanged." },
	{ "infection.initial_zombies", _config_integer, "1", "HALO_INFECTION_ZOMBIES", _environment_value, _platform_all,
		"Infection: the zombies chosen at the start (1 to 16)." },
	{ "infection.zombie_speed", _config_integer, "150", "HALO_INFECTION_SPEED", _environment_value, _platform_all,
		"Infection: a zombie's speed, percent (100 to 200)." },
	{ "infection.skull_damage", _config_integer, "200", "HALO_INFECTION_SKULL", _environment_value, _platform_all,
		"Infection: a zombie's melee damage, percent (100 to 250)." },
	{ "infection.respawn_time", _config_integer, "5", "HALO_INFECTION_RESPAWN", _environment_value, _platform_all,
		"Infection: seconds before a killed player respawns (1 to 30)." },
	{ "infection.countdown", _config_integer, "10", "HALO_INFECTION_COUNTDOWN", _environment_value, _platform_all,
		"Infection: seconds before the first zombies are chosen (0 to 30)." },
	{ "infection.zombie_shields", _config_boolean, "true", "HALO_INFECTION_SHIELDS", _environment_value, _platform_all,
		"Infection: whether the zombies have shields." },
