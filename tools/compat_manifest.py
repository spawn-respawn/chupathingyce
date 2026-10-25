#!/usr/bin/env python3
"""Upstream contract tracker for gameplay features.

Each feature directory under source/features/ may hold a compatibility.json
(schema 1). It records, for a feature that has to play the same game as
another client (OpenCE's Gun Game, Infection, SWAT, Hardcore ...):

  source    upstream files the feature is built from, each pinned to a Git
            blob ("base_blob": the upstream version last reconciled) and
            optionally acknowledged ("seen_blob": changed, understood, not
            reconciled)
  watch     files the behaviour depends on without being copied (Slayer,
            damage, the game settings record). Same pins. "repo" is
            "opence" (default) or "local" (this repository's HEAD)
  contract  the gameplay and wire facts that must not change
  verification  status "not_yet_proven" or "verified", the tests required,
            and those that have passed

A pin's state is derived, never stored:
  current       blob == base_blob
  acknowledged  blob == seen_blob (understood, not reconciled)
  unreviewed    anything else: upstream moved and nobody has looked

Metal's manifest may treat acknowledged drift as acceptable. Here it never
is: a feature is "verified" only with every pin current and every required
test passed, so a build that compiles but has drifted cannot claim crossplay.

  check [FEATURE ...]   structure and drift; exit 1 on unreviewed drift, a
                        verified claim over drifted pins or missing passes
  report                impact report: changed files and the features each
                        one touches (never fails on drift; --strict does)
  accept FEATURE PATH [--seen]
                        record the current blob as reconciled (clears seen,
                        and withdraws a verified claim), or as acknowledged
  verify [FEATURE ...] [--record]
                        run the pytest-kind tests; --record stores what passed
                        and sets contract_verified when all of them did
  digest [FEATURE ...]  sha256 of the canonical manifests, for telemetry
                        (diagnostic metadata, not proof of compatibility)
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FEATURES = Path("source/features")
FILE = "compatibility.json"
# "worktree": this repository's files as they are on disk, so a change not yet committed is drift too
DEFAULT_REFS = {"opence": "opence/main", "local": "worktree"}
INTEGRATIONS = {"planned", "adapted", "shared", "dropped"}
# not_yet_proven: nothing claimed. contract_verified: every local test passed
# (wire vectors, rules), the crossplay matches not yet run: no compatibility
# claim. verified: every required test, crossplay included, passed.
STATUSES = {"not_yet_proven", "contract_verified", "verified", "intentionally_incompatible"}
# pytest: runs here (id, optional k). harness: an automated rules test not
# written yet (needs the game's harness); counts as local, so it blocks
# contract_verified. manual: a crossplay match, recorded by hand.
TEST_KINDS = {"pytest", "harness", "manual"}
LOCAL_KINDS = {"pytest", "harness"}


class CompatError(Exception):
    def __init__(self, problems):
        super().__init__("\n".join(problems))
        self.problems = problems


def manifest_path(feature, root=ROOT):
    return Path(root) / FEATURES / feature / FILE


def feature_names(root=ROOT):
    base = Path(root) / FEATURES
    return sorted(p.parent.name for p in base.glob(f"*/{FILE}"))


def load(feature, root=ROOT):
    path = manifest_path(feature, root)
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError) as error:
        raise CompatError([f"{feature}: cannot read {path}: {error}"]) from error
    return data


def save(feature, data, root=ROOT):
    manifest_path(feature, root).write_text(json.dumps(data, indent=2) + "\n")


def blob(repo, ref, path, root=ROOT):
    """the Git blob id of path at ref, None if absent"""
    if ref == "worktree":
        if not (Path(root) / path).is_file():
            return None
        result = subprocess.run(["git", "hash-object", path], cwd=root, capture_output=True, text=True)
        return result.stdout.strip() or None if result.returncode == 0 else None
    result = subprocess.run(["git", "rev-parse", "-q", "--verify", f"{ref}:{path}"],
                            cwd=root, capture_output=True, text=True)
    return result.stdout.strip() or None if result.returncode == 0 else None


def pins(data):
    """every pinned file of a manifest: (kind, repo, path, entry)"""
    default_repo = data.get("source", {}).get("repository_alias", "opence")
    for path, entry in data.get("source", {}).get("files", {}).items():
        yield "source", entry.get("repo", default_repo), path, entry
    for entry in data.get("watch", []):
        yield "watch", entry.get("repo", "opence"), entry.get("path", ""), entry


def pin_state(entry, current):
    if current is None:
        return "missing"
    if current == entry.get("base_blob"):
        return "current"
    if current == entry.get("seen_blob"):
        return "acknowledged"
    return "unreviewed"


def refs_for(args):
    return {"opence": args.opence_ref, "local": args.local_ref}


def evaluate(feature, data, refs, root=ROOT):
    """(problems, rows): rows are (kind, path, state, current blob)"""
    problems = []
    rows = []
    if data.get("schema") != 1:
        problems.append(f"{feature}: schema must be 1")
    if data.get("feature") != feature:
        problems.append(f"{feature}: 'feature' must be {feature!r}")
    for key in ("source", "contract", "verification"):
        if not isinstance(data.get(key), dict):
            problems.append(f"{feature}: missing object {key!r}")
    if problems:
        return problems, rows
    for kind, repo, path, entry in pins(data):
        if repo not in refs:
            problems.append(f"{feature}: {path}: unknown repo {repo!r}")
            continue
        if not path:
            problems.append(f"{feature}: a {kind} entry has no path")
            continue
        if not entry.get("base_blob"):
            problems.append(f"{feature}: {path}: no base_blob")
        if kind == "source":
            integration = entry.get("integration")
            if integration not in INTEGRATIONS:
                problems.append(f"{feature}: {path}: integration must be one of {sorted(INTEGRATIONS)}")
            local = entry.get("local")
            if integration == "adapted" and (not local or not (Path(root) / local).exists()):
                problems.append(f"{feature}: {path}: adapted but local file {local!r} is missing")
        current = blob(repo, refs[repo], path, root)
        state = pin_state(entry, current)
        rows.append((kind, path, state, current))
        if state == "missing":
            problems.append(f"{feature}: {path}: not found at {refs[repo]}")
    verification = data["verification"]
    status = verification.get("status")
    if status not in STATUSES:
        problems.append(f"{feature}: verification.status must be one of {sorted(STATUSES)}")
    required = verification.get("required", [])
    passed = verification.get("passed", [])
    tests = verification.get("tests", {})
    for name in required:
        kind = tests.get(name, {}).get("kind")
        if kind not in TEST_KINDS:
            problems.append(f"{feature}: required test {name!r} has no kind (one of {sorted(TEST_KINDS)}) in verification.tests")
        elif kind == "pytest" and not tests[name].get("id"):
            problems.append(f"{feature}: pytest test {name!r} has no id")
    for name in passed:
        if name not in required:
            problems.append(f"{feature}: passed test {name!r} is not required")
    drifted = [path for _, path, state, _ in rows if state != "current"]
    local = [n for n in required if tests.get(n, {}).get("kind") in LOCAL_KINDS]
    if status in ("contract_verified", "verified"):
        if drifted:
            problems.append(f"{feature}: {status} over drifted pins: {', '.join(drifted)}")
        wanted = local if status == "contract_verified" else required
        missing = [name for name in wanted if name not in passed]
        if missing:
            problems.append(f"{feature}: {status} but tests not passed: {', '.join(missing)}")
    if status == "verified" and any(tests.get(n, {}).get("kind") == "manual" and n not in passed for n in required):
        problems.append(f"{feature}: verified needs its manual (crossplay) tests recorded as passed")
    return problems, rows


def cmd_check(args):
    names = args.features or feature_names()
    problems = []
    for name in names:
        found, rows = evaluate(name, load(name), refs_for(args))
        problems += found
        for kind, path, state, _ in rows:
            if state == "unreviewed":
                problems.append(f"{name}: {kind} {path} changed upstream and is unreviewed")
            elif state == "acknowledged":
                print(f"{name}: {kind} {path} acknowledged, not reconciled (no compatibility claims)")
    for line in problems:
        print(line, file=sys.stderr)
    if not problems:
        print(f"ok: {len(names)} feature manifest(s)")
    return 1 if problems else 0


def cmd_report(args):
    refs = refs_for(args)
    changed = {}
    for name in feature_names():
        data = load(name)
        _, rows = evaluate(name, data, refs)
        for kind, path, state, _ in rows:
            if state != "current":
                changed.setdefault(path, []).append((name, kind, state))
    print("upstream impact report")
    if not changed:
        print("  every pinned file is current")
    for path in sorted(changed):
        users = ", ".join(f"{n} ({k}, {s})" for n, k, s in changed[path])
        print(f"  CHANGED {path}\n    impacts: {users}")
    print("verification")
    for name in feature_names():
        verification = load(name)["verification"]
        left = [t for t in verification.get("required", []) if t not in verification.get("passed", [])]
        print(f"  {name}: {verification.get('status')}; outstanding tests: {', '.join(left) or 'none'}")
    if any(load(n)["verification"].get("status") != "verified" for n in feature_names()):
        print("do not expand compatibility claims for unverified features")
    return 1 if args.strict and changed else 0


def cmd_accept(args):
    data = load(args.feature)
    refs = refs_for(args)
    for _, repo, path, entry in pins(data):
        if path != args.path:
            continue
        current = blob(repo, refs[repo], path)
        if current is None:
            print(f"{path}: not found at {refs[repo]}", file=sys.stderr)
            return 1
        if args.seen:
            entry["seen_blob"] = current
        else:
            entry["base_blob"] = current
            entry.pop("seen_blob", None)
            verification = data["verification"]
            if verification.get("status") == "verified":
                verification["status"] = "not_yet_proven"
            verification.pop("passed", None)
        save(args.feature, data)
        print(f"{args.feature}: {path} {'acknowledged' if args.seen else 'reconciled'} at {current}")
        return 0
    print(f"{args.feature}: no pinned file {args.path}", file=sys.stderr)
    return 1


def cmd_verify(args):
    """run each feature's pytest-kind tests; --record stores the results (and the
    status: contract_verified once all of them pass and no pin has drifted)"""
    code = 0
    for name in args.features or feature_names():
        data = load(name)
        verification = data["verification"]
        tests = verification.get("tests", {})
        _, rows = evaluate(name, data, refs_for(args))
        drifted = [p for _, p, s, _ in rows if s != "current"]
        passed = [t for t in verification.get("passed", []) if tests.get(t, {}).get("kind") == "manual"]
        for test, spec in tests.items():
            if spec.get("kind") != "pytest" or test not in verification.get("required", []):
                continue
            command = [sys.executable, "-m", "pytest", "-q", spec["id"]]
            if spec.get("k"):
                command += ["-k", spec["k"]]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            ok = result.returncode == 0
            print(f"{name}: {test}: {'passed' if ok else 'FAILED'}")
            if ok:
                passed.append(test)
            else:
                code = 1
        outstanding = [t for t in verification.get("required", []) if t not in passed]
        local_left = [t for t in outstanding if tests.get(t, {}).get("kind") in LOCAL_KINDS]
        print(f"{name}: outstanding: {', '.join(outstanding) or 'none'}")
        if args.record:
            verification["passed"] = [t for t in verification.get("required", []) if t in passed]
            if verification.get("status") in ("not_yet_proven", "contract_verified"):
                verification["status"] = "contract_verified" if not local_left and not drifted else "not_yet_proven"
            save(name, data)
    return code


def digest(names, root=ROOT):
    sha = hashlib.sha256()
    for name in names:
        sha.update(name.encode() + b"\0")
        sha.update(json.dumps(load(name, root), sort_keys=True, separators=(",", ":")).encode() + b"\0")
    return sha.hexdigest()


def cmd_digest(args):
    print(digest(args.features or feature_names()))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--opence-ref", default=DEFAULT_REFS["opence"])
    parser.add_argument("--local-ref", default=DEFAULT_REFS["local"])
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check")
    check.add_argument("features", nargs="*")
    check.set_defaults(run=cmd_check)
    report = sub.add_parser("report")
    report.add_argument("--strict", action="store_true")
    report.set_defaults(run=cmd_report)
    accept = sub.add_parser("accept")
    accept.add_argument("feature")
    accept.add_argument("path")
    accept.add_argument("--seen", action="store_true")
    accept.set_defaults(run=cmd_accept)
    ver = sub.add_parser("verify")
    ver.add_argument("features", nargs="*")
    ver.add_argument("--record", action="store_true")
    ver.set_defaults(run=cmd_verify)
    dig = sub.add_parser("digest")
    dig.add_argument("features", nargs="*")
    dig.set_defaults(run=cmd_digest)
    args = parser.parse_args(argv)
    try:
        return args.run(args)
    except CompatError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
