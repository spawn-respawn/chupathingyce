/* Gun Game's config.toml settings (rows of port_config.c's table, included
when the feature is built). */

	{ "gun_game.host", _config_boolean, "false", "HALO_GUN_GAME_HOST", _environment_value, _platform_all,
		"Host the next free for all Slayer game as OpenCE-compatible Gun Game:\n"
		"each kill gives the next weapon, the last wins. A team game is unchanged." },
	{ "gun_game.weapon_order", _config_integer, "0", "HALO_GUN_GAME_ORDER", _environment_value, _platform_all,
		"Gun Game's weapons: 0 strongest first, 1 weakest first, 2 random (drawn\n"
		"by the host for each game)." },
	{ "gun_game.melee_demotes", _config_boolean, "true", "HALO_GUN_GAME_DEMOTES", _environment_value, _platform_all,
		"Gun Game: a melee kill takes the victim back a weapon." },
