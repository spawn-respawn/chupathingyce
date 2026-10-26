#!/usr/bin/env python3
"""Summarize opt-in Metal work counters from one log (never concatenate duplicate logs).

Enable HALO_METAL_WORK_PROFILE=1 for a diagnostic run. Reports cover 600-frame
windows, including loading if present; partial final windows are not emitted.
Wait durations are CPU wall time, not GPU execution time. Draw-stage/transfer
instrumentation must be disabled in separate production-style timing runs.
"""
import argparse
import json
import re
from pathlib import Path

PATTERNS = {
    "draws": (r"Metal work: draws (\d+) clipped (\d+)", ("draws", "clipped")),
    "textures": (r"Metal work: texture uploads (\d+) renames (\d+) preserved levels (\d+) bytes (\d+)",
                 ("texture_uploads", "texture_renames", "preserved_levels", "preserved_bytes")),
    "readbacks": (r"Metal work: readbacks (\d+) bytes (\d+) waits (\d+) wait_ms ([\d.]+)",
                  ("readbacks", "readback_bytes", "readback_waits", "readback_wait_ms")),
    "presentation": (r"Metal work: drawable requests (\d+) wait_ms ([\d.]+) slot requests (\d+) wait_ms ([\d.]+)",
                     ("drawable_requests", "drawable_wait_ms", "slot_requests", "slot_wait_ms")),
}


def summarize(text):
    if "work profiling enabled (diagnostic counters)" not in text:
        raise ValueError("work profiling not enabled")
    groups = {name: re.findall(pattern, text) for name, (pattern, _) in PATTERNS.items()}
    counts = {len(rows) for rows in groups.values()}
    if len(counts) != 1 or not next(iter(counts)):
        raise ValueError("missing or incomplete 600-frame work reports")
    windows = []
    for index in range(len(groups["draws"])):
        window = {}
        for name, (_, fields) in PATTERNS.items():
            for field, value in zip(fields, groups[name][index], strict=True):
                window[field] = float(value) if field.endswith("_ms") else int(value)
        if window["clipped"] > window["draws"] or window["readback_waits"] > window["readbacks"]:
            raise ValueError("invalid work counters")
        windows.append(window)
    totals = {field: sum(row[field] for row in windows) for field in windows[0]}
    return {"scope": "complete 600-frame diagnostic windows, including loading; not GPU time",
            "windows": len(windows), "totals": totals, "raw_windows": windows,
            "clipped_draw_fraction": totals["clipped"] / totals["draws"] if totals["draws"] else 0,
            "mean_drawable_wait_ms": totals["drawable_wait_ms"] / totals["drawable_requests"] if totals["drawable_requests"] else 0,
            "mean_slot_wait_ms": totals["slot_wait_ms"] / totals["slot_requests"] if totals["slot_requests"] else 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="one run.log or debug.txt, not both")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = summarize(args.log.read_text(errors="replace"))
    rendered = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered)
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
