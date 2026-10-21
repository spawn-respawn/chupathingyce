"""Production route installation/following in an asset-free, sanitizer-backed world."""
import os
import subprocess
import tempfile
from pathlib import Path

from test_bot_ctf import body, header

ROOT = Path(__file__).resolve().parent.parent


def test_bot_movement():
    with tempfile.TemporaryDirectory(prefix="bot-movement-") as directory:
        executable = Path(directory) / "test"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                        "-Wpedantic", "-Wconversion", "-Wsign-conversion", "-Wshadow", "-Werror",
                        "-fsanitize=address,undefined", "-I", str(ROOT / "source"),
                        str(ROOT / "tools/test_bot_movement.c"),
                        str(ROOT / "source/features/bots/bot_movement.c"),
                        "-lm", "-o", str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


def test_bot_navigation():
    source = (ROOT / "source/features/bots/bot_navigation.c").read_text()
    functions = "\n".join(body(source, name) for name in (
        "bot_nav_abs", "bot_nav_horizontal_distance", "bot_nav_distance",
        "bot_nav_client", "bot_nav_install", "bot_nav_search", "bot_nav_follow", "bot_nav_route_step"))
    fixture = (ROOT / "tools/test_bot_navigation.c.in").read_text()
    fixture = fixture.replace("/* HEADER */", header("source/features/bots/bot_navigation.h"))
    fixture = fixture.replace("/* FUNCTIONS */", functions)
    with tempfile.TemporaryDirectory(prefix="bot-navigation-") as directory:
        path = Path(directory) / "test.c"
        executable = Path(directory) / "test"
        path.write_text(fixture)
        subprocess.run([os.environ.get("CC", "clang"), "-std=gnu89", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=address,undefined", "-I", str(ROOT / "source"),
                        str(path), str(ROOT / "source/features/bots/bot_movement.c"),
                        "-lm", "-o", str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    test_bot_movement()
    test_bot_navigation()
