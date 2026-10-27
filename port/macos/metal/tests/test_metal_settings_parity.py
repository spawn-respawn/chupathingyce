"""The Metal build's settings registry has every setting upstream's has, as upstream has it.

The Metal build compiles its own copy of port_config.c (platform/port_config.c,
a fork of port/linux/src/port_config.c). The game's shared code asks for a
setting by name, so a setting that upstream registers and the fork does not is
"no such setting" on Metal: logged at every call (a frame's worth of lines),
read as 0 or off whatever its default is, and refused when the Settings screens
try to save it. manifest.json lets a fork's upstream drift be acknowledged
("seen") so a build goes on; this is the part of that drift that must not be:

- every upstream setting is registered, once, with its type, environment
  variable and default (a default is the player's experience of a setting that
  is not in config.toml: 2x multisampling, 256 shadow maps, a profile shown);
- a setting that differs on purpose is named in INTENTIONAL_DIFFERENCES, with
  why, and stops being allowed once it no longer differs;
- a setting only Metal registers is read by something (one nothing reads
  promises a control that does nothing).
"""
import re
import unittest

from metal_paths import METAL, ROOT

UPSTREAM = ROOT / "port/linux/src/port_config.c"
FORK = METAL / "platform/port_config.c"

# where a setting's name is read from, as a quoted string (not the registries)
READERS = (METAL, ROOT / "port/linux/src", ROOT / "port/linux/game")
READER_SUFFIXES = {".c", ".h", ".m", ".inc"}

# Settings whose registration may differ from upstream's: name -> why. Empty
# since the Metal build took upstream's defaults (2x multisampling, 256 shadow
# maps, anisotropic filtering off as on the Xbox).
INTENTIONAL_DIFFERENCES: dict[str, str] = {}

# a registry entry's first line: name, type, default (a string or a macro), environment variable or NULL
ENTRY = re.compile(r'^\t\{ "(?P<name>[a-z0-9_]+\.[a-z0-9_]+)",\s*(?P<type>_config_\w+),\s*'
                   r'(?P<default>"(?:[^"\\]|\\.)*"|[A-Za-z_]\w*),\s*(?P<env>"[A-Z0-9_]+"|NULL)', re.M)
ANY_ENTRY = re.compile(r'^\t\{ "[a-z0-9_]+\.[a-z0-9_]+", _config_\w+', re.M)
DEFAULT_MACRO = re.compile(r"^#define (DEFAULT_\w+) (.+)$", re.M)
QUOTED_NAME = re.compile(r'"([a-z0-9_]+\.[a-z0-9_]+)"')
# a console's macros (halo_console_features.h, the consoles' compile lines): never defined on the desktop
CONSOLE_MACRO = re.compile(r"^HALO_(?:CONSOLE_\w+|\w+_CONSOLE)$")
CONDITIONAL = re.compile(r"^#\s*(?P<directive>ifdef|ifndef|if|else|elif|endif)\b\s*(?P<macro>\w*)")


def desktop_lines(text: str, desktop_only: bool = False) -> str:
    """the text with each #ifdef/#ifndef on a console macro that has an #else down to the desktop's
    branch: an entry with a console default and a desktop one is one setting, not two. A console
    block without an #else keeps its lines too, unless desktop_only (then only what a desktop
    build compiles is left: a console's own settings are not the Mac's to carry). Every other
    conditional keeps all its lines"""
    lines = text.splitlines(keepends=True)
    # the conditionals that have an #else or #elif, by the line that opens them
    alternatives: set[int] = set()
    opened: list[int] = []
    for number, line in enumerate(lines):
        conditional = CONDITIONAL.match(line)
        if not conditional:
            continue
        if conditional["directive"] in ("ifdef", "ifndef", "if"):
            opened.append(number)
        elif conditional["directive"] in ("else", "elif") and opened:
            alternatives.add(opened[-1])
        elif conditional["directive"] == "endif" and opened:
            opened.pop()
    kept = []
    # each open conditional: whether it is decided for the desktop, and whether its current branch is kept
    stack: list[tuple[bool, bool]] = []
    for number, line in enumerate(lines):
        conditional = CONDITIONAL.match(line)
        if conditional:
            directive, macro = conditional["directive"], conditional["macro"]
            if directive in ("ifdef", "ifndef", "if"):
                console = (directive != "if" and (desktop_only or number in alternatives)
                           and bool(CONSOLE_MACRO.match(macro)))
                stack.append((console, directive == "ifndef" if console else True))
            elif directive in ("else", "elif") and stack:
                console, active = stack[-1]
                stack[-1] = (console, not active if console else True)
            elif directive == "endif" and stack:
                stack.pop()
            kept.append(line)
            continue
        if all(active for _, active in stack):
            kept.append(line)
    return "".join(kept)


def registry(text: str, desktop_only: bool = False) -> tuple[dict[str, tuple[str, str, str]], list[str]]:
    """the settings a port_config.c registers (name -> type, default, environment variable), with
    the desktop's default where a console has its own, and the names it registers more than once;
    desktop_only, only those a desktop build registers"""
    found: dict[str, tuple[str, str, str]] = {}
    twice = []
    for entry in ENTRY.finditer(desktop_lines(text, desktop_only)):
        if entry["name"] in found:
            twice.append(entry["name"])
        environment = entry["env"].strip('"')
        found[entry["name"]] = (entry["type"], entry["default"], environment)
    return found, twice


def default_macros(text: str) -> dict[str, list[str]]:
    """each DEFAULT_* macro's definitions, in order (one for each platform it is chosen for)"""
    macros: dict[str, list[str]] = {}
    for name, value in DEFAULT_MACRO.findall(text):
        macros.setdefault(name, []).append(value.strip())
    return macros


def problems(upstream: str, fork: str, names_read: set[str],
             intentional: dict[str, str] | None = None) -> list[str]:
    """what makes the fork's registry not carry upstream's: each a sentence that says what to do"""
    intentional = INTENTIONAL_DIFFERENCES if intentional is None else intentional
    upstream_entries, upstream_twice = registry(upstream)
    metal_entries, twice = registry(fork)
    # NULL environment variables belong to console-only settings; Metal is a desktop build.
    upstreams = {name: entry for name, entry in upstream_entries.items() if entry[2] != "NULL"}
    have = {name: entry for name, entry in metal_entries.items() if entry[2] != "NULL"}
    # what Metal must register: what upstream registers on the desktop (a setting inside a
    # console's own block, #ifdef HALO_SWITCH_CONSOLE, is that console's: its readers are too)
    wanted = set(upstreams) & set(registry(upstream, desktop_only=True)[0])
    found = []
    for name in sorted(wanted - set(have)):
        found.append(f"{name} is registered upstream but not by the Metal port_config.c: code that asks for it "
                     "gets 'no such setting' (logged at every call) and 0. Reconcile platform/port_config.c "
                     "with port/linux/src/port_config.c (tools/metal_manifest.py check names the commits)")
    for name in sorted(set(twice)):
        found.append(f"{name} is registered more than once by the Metal port_config.c: keep one entry")
    for name in sorted(set(upstream_twice)):
        found.append(f"{name} is registered more than once by upstream's port_config.c")
    differing = set()
    for name in sorted(set(upstreams) & set(have)):
        for what, up, mac in zip(("type", "default", "environment variable"), upstreams[name], have[name],
                                 strict=True):
            if up != mac:
                differing.add(name)
                if name not in intentional:
                    found.append(f"{name}: its {what} is {mac} on Metal and {up} upstream: take upstream's, "
                                 "or name the setting in INTENTIONAL_DIFFERENCES with why")
    up_macros, mac_macros = default_macros(upstream), default_macros(fork)
    for name in sorted(set(up_macros) & set(mac_macros)):
        if up_macros[name] != mac_macros[name]:
            found.append(f"{name} is defined differently on Metal: the settings that take it as their "
                         "default would differ without their own entries showing it")
    for name in sorted(set(intentional) - differing):
        found.append(f"{name} is in INTENTIONAL_DIFFERENCES but no longer differs: remove it")
    for name in sorted(set(have) - set(upstreams)):
        if name not in names_read:
            found.append(f"{name} is registered only by the Metal port_config.c and nothing reads it: a "
                         "setting that does nothing; remove it or read it")
    return found


def names_read_by_the_sources() -> set[str]:
    """every setting-shaped name quoted in the sources that read settings, but for the registries"""
    names: set[str] = set()
    for root in READERS:
        for path in root.rglob("*"):
            if (path.suffix in READER_SUFFIXES and path.is_file() and path.name != "port_config.c"
                    and "tests" not in path.relative_to(root).parts):
                names.update(QUOTED_NAME.findall(path.read_text(errors="replace")))
    return names


class SettingsParityTests(unittest.TestCase):
    def test_registries_are_in_step(self):
        self.assertEqual(problems(UPSTREAM.read_text(), FORK.read_text(), names_read_by_the_sources()), [])

    def test_the_reader_sees_every_entry(self):
        # (an entry it skipped would be a setting every other check lets through)
        for path in (UPSTREAM, FORK):
            text = path.read_text()
            self.assertGreater(len(ANY_ENTRY.findall(text)), 100)
            found, duplicates = registry(text)
            self.assertEqual(len(found) + len(duplicates), len(ANY_ENTRY.findall(desktop_lines(text))), path.name)

    def test_macos_64_bit_accepts_the_new_desktop_defaults(self):
        # HALO_64BIT, without HALO_SERVER or HALO_ANDROID, is how tools/lp64_build.py
        # compiles these registry files for macOS: the defaults a new config gets.
        text = FORK.read_text()
        macros = default_macros(text)
        self.assertTrue(any(r'\"msaa2x\"' in value for value in macros["DEFAULT_ANTI_ALIASING"]))
        self.assertIn('"256"', macros["DEFAULT_SHADOW_RESOLUTION"])
        values, _ = registry(text)
        self.assertEqual(values["display.anisotropic_filtering"],
                         ("_config_integer", '"0"', "HALO_ANISOTROPIC_FILTERING"))
        self.assertEqual(values["display.profile_names"][1], '"true"')
        self.assertEqual(values["display.delta_profile"][1], '"true"')
        self.assertEqual(values["display.pause_controls"][1], '"true"')

    # (the checks, on registries made to be wrong: a check that cannot fail checks nothing)
    ENTRY_A = '\t{ "display.a", _config_boolean, "true", "HALO_A", _environment_value, _platform_all,\n\t\t"A." },\n'
    ENTRY_B = '\t{ "display.b", _config_integer, DEFAULT_B,\n\t\t"HALO_B", _environment_value, _platform_all,\n\t\t"B." },\n'
    MACRO_B = '#define DEFAULT_B "256"\n'

    def test_a_missing_setting_is_found(self):
        found = problems(self.ENTRY_A + self.ENTRY_B + self.MACRO_B, self.ENTRY_B + self.MACRO_B, set())
        self.assertEqual(len(found), 1)
        self.assertIn("display.a is registered upstream but not by the Metal", found[0])

    CONSOLE_A = ('#ifdef HALO_CONSOLE_A_FEATURE\n' + ENTRY_A.split('\n')[0] + '\n#else\n'
                 + ENTRY_A.split('\n')[0].replace('"true"', '"false"') + '\n#endif\n\t\t"A." },\n')

    def test_a_console_alternative_is_one_setting_with_the_desktop_default(self):
        values, duplicates = registry(self.CONSOLE_A)
        self.assertEqual(duplicates, [])
        self.assertEqual(values["display.a"][1], '"false"')
        values, _ = registry(self.CONSOLE_A.replace("#ifdef", "#ifndef"))
        self.assertEqual(values["display.a"][1], '"true"')
        self.assertEqual(problems(self.CONSOLE_A, self.CONSOLE_A, set()), [])

    def test_a_consoles_own_setting_is_not_wanted_on_metal(self):
        switch_only = "#ifdef HALO_SWITCH_CONSOLE\n" + self.ENTRY_A + "#endif\n"
        self.assertEqual(problems(switch_only + self.ENTRY_B + self.MACRO_B, self.ENTRY_B + self.MACRO_B, set()), [])
        # (one a desktop compiles is still wanted: #ifndef, or the console's #else)
        found = problems(switch_only.replace("#ifdef", "#ifndef"), "", set())
        self.assertEqual(len(found), 1)
        self.assertIn("display.a is registered upstream but not by the Metal", found[0])
        # (and one Metal carries anyway is checked against upstream's, not called Metal's own)
        self.assertEqual(problems(switch_only, self.ENTRY_A, set()), [])
        self.assertEqual(len(problems(switch_only, self.ENTRY_A.replace('"true"', '"false"'), set())), 1)

    def test_a_console_block_without_an_else_keeps_its_entries(self):
        values, duplicates = registry("#ifdef HALO_SWITCH_CONSOLE\n" + self.ENTRY_A + "#endif\n")
        self.assertEqual((list(values), duplicates), (["display.a"], []))

    def test_a_non_console_conditional_keeps_every_entry(self):
        text = "#ifdef HALO_GAME_BROWSER\n" + self.ENTRY_A + "#else\n" + self.ENTRY_A + "#endif\n"
        self.assertEqual(registry(text)[1], ["display.a"])

    def test_a_duplicate_is_found(self):
        found = problems(self.ENTRY_A, self.ENTRY_A + self.ENTRY_A, set())
        self.assertEqual(len(found), 1)
        self.assertIn("display.a is registered more than once", found[0])

    def test_a_changed_default_is_found_unless_named(self):
        mine = self.ENTRY_A.replace('"true"', '"false"')
        found = problems(self.ENTRY_A, mine, set())
        self.assertEqual(len(found), 1)
        self.assertIn("display.a: its default is", found[0])
        self.assertEqual(problems(self.ENTRY_A, mine, set(), {"display.a": "on purpose"}), [])

    def test_a_changed_macro_default_is_found(self):
        # (the entry is the same text: it is the macro that changed what a player gets)
        found = problems(self.ENTRY_B + self.MACRO_B, self.ENTRY_B + '#define DEFAULT_B "128"\n', set())
        self.assertEqual(len(found), 1)
        self.assertIn("DEFAULT_B is defined differently", found[0])

    def test_an_allowance_that_no_longer_applies_is_found(self):
        found = problems(self.ENTRY_A, self.ENTRY_A, set(), {"display.a": "once differed"})
        self.assertEqual(len(found), 1)
        self.assertIn("no longer differs", found[0])

    def test_a_setting_nothing_reads_is_found(self):
        self.assertEqual(len(problems(self.ENTRY_A, self.ENTRY_A + self.ENTRY_B, set())), 1)
        self.assertEqual(problems(self.ENTRY_A, self.ENTRY_A + self.ENTRY_B, {"display.b"}), [])


if __name__ == "__main__":
    unittest.main()
