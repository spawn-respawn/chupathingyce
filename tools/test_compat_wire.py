"""Golden wire vectors for OpenCE's extended game options (network version
35), run through this build's port/linux/game/network_rules.c, plus the
structure of the compatibility manifests (tools/compat_manifest.py).

A mismatch here means this build and OpenCE would disagree about which game
is being played, whatever else compiles."""
import json
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from tools import compat_manifest

ROOT = Path(__file__).resolve().parent.parent
VECTORS = json.loads((ROOT / "tools/tests/compat/opence_options_v35.json").read_text())["vectors"]
# OpenCE's byte offsets within game_variant_options (game_engine.h)
OFFSETS = {"mode": 0x1C, "hardcore": 0x1D, "gun_game_order": 0x24, "gun_game_melee_demotes": 0x25}


@pytest.fixture(scope="module")
def probe(tmp_path_factory):
    cc = next((shutil.which(n) for n in ("clang", "cc", "gcc") if shutil.which(n)), None)
    if not cc:
        pytest.skip("no C compiler")
    out = tmp_path_factory.mktemp("compat") / "options_probe"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "port/linux/game"),
                    str(ROOT / "tools/tests/compat/options_probe.c"),
                    str(ROOT / "port/linux/game/network_rules.c"), "-o", str(out)], check=True)
    return out


def run(probe, vector):
    args = vector["bytes"].split() + [str(int(vector["slayer"])), str(int(vector["teams"]))]
    return subprocess.run([str(probe), *args], capture_output=True, text=True)


@pytest.mark.parametrize("vector", VECTORS, ids=[v["name"] for v in VECTORS])
def test_vector(probe, vector):
    result = run(probe, vector)
    assert result.returncode == 0, "options moved in the record: the 0x1C layout is wrong"
    fields = result.stdout.split()
    assert int(fields[0]) == vector["mode"]
    assert int(fields[1]) == vector["hardcore"]
    expected = vector["bytes"] if vector["sanitized"] == "same" else vector["sanitized"]
    assert " ".join(fields[2:]) == " ".join(expected.split())


def test_offsets():
    """Gun Game's fields sit where the contract says"""
    if "gun_game" not in compat_manifest.feature_names():
        pytest.skip("the gun_game feature is not in this tree")
    assert OFFSETS["mode"] + 8 == OFFSETS["gun_game_order"] == 0x24
    assert OFFSETS["gun_game_melee_demotes"] == 0x25
    contract = compat_manifest.load("gun_game")["contract"]
    assert contract["mode"] == 3
    assert int(contract["mode_byte_offset"], 16) == OFFSETS["mode"]
    assert int(contract["weapon_order_offset"], 16) == OFFSETS["gun_game_order"]
    assert int(contract["melee_demotes_offset"], 16) == OFFSETS["gun_game_melee_demotes"]


def test_manifests_well_formed():
    names = compat_manifest.feature_names()
    for name in names:
        data = compat_manifest.load(name)
        refs = {"opence": "opence/main", "local": "HEAD"}
        problems, _ = compat_manifest.evaluate(name, data, refs)
        # pins need the opence remote; without it only structure is checked
        problems = [p for p in problems if "not found at" not in p]
        assert not problems


def test_verified_needs_proof(tmp_path):
    data = {"schema": 1, "feature": "x", "source": {"files": {}}, "contract": {},
            "verification": {"status": "verified", "required": ["a"], "passed": []}}
    problems, _ = compat_manifest.evaluate("x", data, {"opence": "HEAD", "local": "HEAD"})
    assert any("not passed" in p for p in problems)


def test_acknowledged_is_not_current():
    entry = {"base_blob": "a", "seen_blob": "b"}
    assert compat_manifest.pin_state(entry, "a") == "current"
    assert compat_manifest.pin_state(entry, "b") == "acknowledged"
    assert compat_manifest.pin_state(entry, "c") == "unreviewed"


@pytest.mark.parametrize("path, needle", [
    ("source/game/game_engine.c", "halo_extensions_select_game_engine(game_engine, &global_variant)"),
    ("source/game/game_engine.c", "halo_extensions_end_game_session();"),
    ("source/game/players.c", "halo_extensions_postspawn_player(iterator.datum_index);"),
    ("source/objects/damage.c", "game_modes_kill_by_melee(damage_effect->damage.category == _damage_category_melee);"),
    ("source/networking/network_game_manager.c", "game_modes_commit_pending(&game->variant);"),
    ("source/networking/network_server_message_handler.c", "game_modes_pending_get(further);"),
    ("source/networking/network_server_message_handler.c", "refusing a machine that takes the older game settings record"),
    ("source/networking/network_client_message_handler.c", "game_modes_pending_set(network_game_client_extended_options);"),
    ("source/networking/network_client_message_handler.c", "halo_extensions_plays_opence_mode(3)"),
    ("source/networking/network_server_manager.c", "halo_extensions_host_game_options(variant, options, extended)"),
])
def test_integration_points(path, needle):
    """the engine calls the contract's hooks (a removed call silently turns Gun Game into Slayer)"""
    assert needle in (ROOT / path).read_text()


def test_gun_game_keeps_slayer_kill_path():
    """kills go through Slayer's callback wrapper, never halo_ruleset.player_killed"""
    if not (ROOT / "source/features/gun_game").is_dir():
        pytest.skip("the gun_game feature is not in this tree")
    source = (ROOT / "source/features/gun_game/gun_game.c").read_text()
    assert ".player_killed =" not in source
    assert "gun_game.engine.player_killed_player = gun_game_player_killed_player" in source
    assert "gun_game.slayer.player_killed_player(" in source


@pytest.mark.parametrize("path, needle", [
    ("source/game/game_engine.c", "halo_extensions_player_killed_player(killing_player_index, dead_player_index, friendly_fire);"),
    ("source/game/game_engine.c", "halo_extensions_write_network_state(buffer, size, &written)"),
    ("source/game/game_engine.c", "halo_extensions_read_network_state(buffer, size, first, &read)"),
    ("source/game/game_engine.c", "halo_extensions_hide_motion_sensor()"),
    ("source/game/game_engine.c", "halo_extensions_time_remaining(&left)"),
    ("source/game/game_engine.c", "halo_extensions_respawn_ticks()"),
    ("source/game/game_engine.c", "halo_extensions_manages_teams()"),
    ("source/game/game_engine.c", "halo_extensions_uses_ball_weapon()"),
    ("source/game/game_engine.c", "halo_extensions_should_end_game(&should_end_game)"),
    ("source/game/aim_assist.c", "halo_extensions_suppress_aim_assist()"),
    ("source/game/players.c", "halo_extensions_player_unit_attached(player_index);"),
    ("source/game/players.c", "halo_extensions_allow_weapon_drop(player->unit_index, weapon_index)"),
    ("source/objects/damage.c", "halo_extensions_headshot_kills(object_index,"),
    ("source/units/units.c", "halo_extensions_melee_multiplier(unit_index)"),
    ("source/units/units.c", "halo_extensions_unit_restricted(unit_index)"),
    ("source/items/weapons.c", "halo_extensions_can_delete_objective_weapon(weapon_index)"),
    ("source/objects/objects.c", "halo_extensions_can_delete_objective_weapon(object_index)"),
    ("port/linux/game/network_damage.c", "halo_extensions_unit_restricted"),
    ("port/linux/game/network_damage.c", "halo_extensions_melee_rider_visible"),
    ("source/networking/network_server_manager.c", "halo_extensions_host_game_options(variant, options, extended)"),
])
def test_modes_integration_points(path, needle):
    """SWAT, Infection and Hardcore's hooks are in the engine"""
    assert needle in (ROOT / path).read_text()


def test_engine_does_not_name_modes():
    """the engine reaches Infection, SWAT and Hardcore only through extension_api.h"""
    for path in ("source/game/game_engine.c", "source/game/players.c", "source/units/units.c",
                 "source/items/weapons.c", "source/objects/objects.c", "source/objects/damage.c",
                 "source/game/aim_assist.c", "port/linux/game/network_damage.c"):
        text = (ROOT / path).read_text()
        assert not re.search(r"(?<![A-Za-z_])infection_(?!form)", text), path
        assert "game_modes_engine" not in text and "game_modes_headshot" not in text, path


def test_infection_state_validator(tmp_path):
    """the host's Infection state as a client takes it: full, incremental, stale, duplicate, malformed"""
    if not (ROOT / "source/features/infection").is_dir():
        pytest.skip("the infection feature is not in this tree")
    cc = next((shutil.which(n) for n in ("clang", "cc", "gcc") if shutil.which(n)), None)
    if not cc:
        pytest.skip("no C compiler")
    feature = ROOT / "source/features/infection"
    out = tmp_path / "infection_state_test"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(feature),
                    str(ROOT / "tools/tests/compat/infection_state_test.c"), "-o", str(out)], check=True)
    subprocess.run([str(out)], check=True)


def test_kind_is_advertised_and_listed():
    """the host's kind reaches the LAN advertisement (not while Delta's major is in that byte) and the listing's stamp"""
    handler = (ROOT / "source/networking/network_server_message_handler.c").read_text()
    assert "advertisement.reserved[HALO_PORT_ADVERTISED_KIND_OFFSET] = game_modes_kind(&game->variant);" in handler
    assert "if (!delta_peer_advertised_flags())" in handler
    lobby = (ROOT / "port/linux/src/p2p_lobby.c").read_text()
    assert "bytes[size] = lobby.kind;" in lobby
    assert "game_modes_kind(&game->variant)" in (ROOT / "source/networking/network_server_manager.c").read_text()


@pytest.mark.parametrize("path, needle", [
    ("source/saved games/playlist_profile.c", "game_modes_gametype((short)(profile_index - NUMBER_OF_DEFAULT_PLAYLIST_PROFILES -"),
    ("source/saved games/playlist_profile.c", "playlist_profile_extended_to_block(block, default_extended);"),
    ("source/saved games/playlist_profile.c", "csmemcpy(playlist_profile_write_extended, extended, sizeof(playlist_profile_write_extended));"),
    ("source/interface/ui_widget_event_handler_functions.c", "player_ui_set_game_variant_extended_options(extended);"),
    ("source/networking/network_server_manager.c", "player_ui_get_game_variant_extended_options()"),
    ("port/linux/game/menu_functions.c", "GAMETYPE_BANK_OPENCE"),
    ("port/linux/game/menu_functions.c", "game_modes_variant_kind_name(&game->variant, extended)"),
    ("port/assets/menus/ce/strings.xml", '<string text="OPENCE"/>'),
    ("port/linux/game/menu_tags.c", '"mp profile init opence options", "mp profile set opence options"'),
    ("port/linux/game/menu_tags.c", '"port playlist edit desc"'),
    ("port/linux/game/menu_functions.c", '"oce_mode_spinner", _option_extended_byte'),
    ("port/linux/game/menu_functions.c", 'playlist_edit_description(widget);'),
    ("port/assets/menus/menus.json", 'opence_options_edit.port.xml'),
    ("port/assets/menus/ce/main_menu.settings_select.multiplayer_setup.playlist_edit.xml", 'playlist_edit_opence_item'),
    ("source/interface/player_ui.c", 'playlist_profile_save_with_extended_options('),
])
def test_menu_integration_points(path, needle):
    """OpenCE's gametypes are listed (own bank), chosen, hosted and saved with their options"""
    assert needle in (ROOT / path).read_text()


def test_stock_default_profiles_unchanged():
    """only OpenCE's own profiles carry an options block; ChupathingyCE's defaults stay byte for byte"""
    source = (ROOT / "source/saved games/playlist_profile.c").read_text()
    assert "if (has_options)" in source
    assert "boolean has_options = FALSE;" in source


def test_options_screen_rows_match_the_option_bytes():
    """each OPENCE OPTIONS spinner edits the byte of OpenCE's layout it names"""
    source = (ROOT / "port/linux/game/menu_functions.c").read_text()
    expected = {"oce_mode_spinner": "mode", "oce_hardcore_spinner": "hardcore",
                "inf_zombies_spinner": "infection_initial_zombies", "inf_speed_spinner": "infection_zombie_speed",
                "inf_damage_spinner": "infection_skull_damage", "inf_respawn_spinner": "infection_respawn_time",
                "inf_countdown_spinner": "infection_countdown", "inf_shields_spinner": "infection_zombie_shields",
                "gg_order_spinner": "gun_game_order", "gg_melee_spinner": "gun_game_melee_demotes"}
    for spinner, field in expected.items():
        assert f'"{spinner}", _option_extended_byte, EXTENDED_FIELD({field})' in source
    xml = (ROOT / "port/assets/menus/ce/main_menu.settings_select.multiplayer_setup.opence_options_edit.port.xml").read_text()
    for spinner in expected:
        assert f'/{spinner}"' in xml


def test_contract_verified_needs_every_local_test():
    refs = {"opence": "worktree", "local": "worktree"}
    base = {"schema": 1, "feature": "x", "source": {"files": {}}, "contract": {},
            "verification": {"status": "contract_verified", "required": ["a", "r", "c"], "passed": ["a"],
                             "tests": {"a": {"kind": "pytest", "id": "t"}, "r": {"kind": "harness"},
                                       "c": {"kind": "manual"}}}}
    problems, _ = compat_manifest.evaluate("x", base, refs)
    assert any("tests not passed: r" in p for p in problems)  # an unwritten rules test blocks it; crossplay does not
    base["verification"]["passed"] = ["a", "r"]
    assert not compat_manifest.evaluate("x", base, refs)[0]
    base["verification"]["status"] = "verified"
    assert any("tests not passed: c" in p for p in compat_manifest.evaluate("x", base, refs)[0])


def _need(feature):
    if not (ROOT / "source/features" / feature).is_dir():
        pytest.skip(f"the {feature} feature is not in this tree")


def _compile_and_run(tmp_path, feature, test):
    cc = next((shutil.which(n) for n in ("clang", "cc", "gcc") if shutil.which(n)), None)
    if not cc:
        pytest.skip("no C compiler")
    out = tmp_path / test
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "source/features" / feature),
                    str(ROOT / f"tools/tests/compat/{test}.c"), "-o", str(out)], check=True)
    subprocess.run([str(out)], check=True)


@pytest.mark.parametrize("feature", ["gun_game", "infection", "swat", "hardcore"])
def test_mode_rules(tmp_path, feature):
    """each mode's rules, as decisions on plain numbers, against OpenCE's contract"""
    _need(feature)
    _compile_and_run(tmp_path, feature, f"{feature}_rules_test")


def test_gun_game_weapon_list_is_opences():
    """OpenCE's default strongest-first list: Rocket Launcher, Shotgun, Sniper Rifle, Plasma Rifle, Assault Rifle,
    Needler, Plasma Pistol, Pistol (a weapon the map lacks is omitted: gun_game_rules_build_levels)"""
    _need("gun_game")
    source = (ROOT / "source/features/gun_game/gun_game.c").read_text()
    block = source[source.index("gun_game_strongest_first[] ="):]
    block = block[:block.index("};")]
    names = re.findall(r"_loadout_weapon_(\w+),", block)
    assert names == ["rocket_launcher", "shotgun", "sniper_rifle", "plasma_rifle", "assault_rifle", "needler",
                     "plasma_pistol", "pistol"]


def test_swat_classification_and_rules():
    """the shots that count (damage.c) and what SWAT sets (no shields) are OpenCE's"""
    _need("swat")
    damage = (ROOT / "source/objects/damage.c").read_text()
    for category in ("bullet", "sniper", "plasma", "needle", "shotgun"):
        assert f"damage_definition->category == _damage_category_{category}" in damage
    swat = (ROOT / "source/features/swat/swat.c").read_text()
    assert "SET_FLAG(variant->universal_variant.flags, _game_variant_no_shields_bit, TRUE);" in swat
    assert "options->primary_weapon = _loadout_weapon_pistol;" in swat
    assert "options->secondary_weapon = _loadout_weapon_assault_rifle;" in swat
    assert "options->no_map_weapons = TRUE;" in swat


def test_hardcore_hides_sensor_and_aim_assist():
    _need("hardcore")
    source = (ROOT / "source/features/hardcore/hardcore.c").read_text()
    assert ".hide_motion_sensor = hardcore_hide_motion_sensor" in source
    assert ".suppress_aim_assist = hardcore_suppress_aim_assist" in source


def test_every_builtin_gametype_has_a_card_description():
    """the picker's cards read the map's description list by profile number, which has none past 26"""
    source = (ROOT / "port/linux/game/game_modes.c").read_text()
    names = re.search(r"game_modes_gametypes\[NUMBER_OF_GAMETYPES\] =\n\{(.*?)\n\};", source, re.S).group(1)
    descriptions = re.search(r"game_modes_descriptions\[NUMBER_OF_GAMETYPES\] =\n\{(.*?)\n\};", source, re.S).group(1)
    assert len(re.findall(r"^\t\{ L", names, re.M)) == len(re.findall(r"^\tL\"", descriptions, re.M)) == 7
    cards = (ROOT / "source/interface/ui_widget_game_data_input_functions.c").read_text()
    assert "playlist_profile_default_description(" in cards
