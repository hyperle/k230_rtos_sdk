#!/usr/bin/env python3
"""Restore pinned Dart subproject adaptations without resetting local work."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def git(project, *args):
    return subprocess.run(
        ["git", "-C", str(project), *args],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without applying patches")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    board = root / "boards/k230d_rtos_dart_v1/micropython"
    lock = json.loads((board / "sources.lock.json").read_text())
    pending = []
    errors = []

    # Preflight every project before making any changes. The lock intentionally
    # excludes the SDK root, whose current branch contains this customization.
    for entry in lock["projects"]:
        project = root / entry["path"]
        if not project.is_dir():
            errors.append(f"{entry['path']}: missing; sync the pinned manifest first")
            continue
        result = git(project, "rev-parse", "--show-toplevel")
        if result.returncode or Path(result.stdout.strip()).resolve() != project.resolve():
            errors.append(f"{entry['path']}: not an independent Git checkout")
            continue
        result = git(project, "rev-parse", "HEAD")
        if result.returncode or result.stdout.strip() != entry["revision"]:
            errors.append(f"{entry['path']}: expected HEAD {entry['revision']}; "
                          "select the pinned revision while preserving local changes")
            continue
        if "patch" not in entry:
            continue
        patch = board / entry["patch"]
        if hashlib.sha256(patch.read_bytes()).hexdigest() != entry["sha256"]:
            errors.append(f"{patch.name}: patch checksum mismatch")
            continue
        result = git(project, "apply", "--reverse", "--check", str(patch))
        if result.returncode == 0:
            print(f"{entry['path']}: patch already present")
            continue
        result = git(project, "apply", "--check", str(patch))
        if result.returncode:
            errors.append(f"{entry['path']}: patch conflicts with local files\n{result.stderr.strip()}")
        else:
            pending.append((project, patch))

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    if args.check:
        for project, _ in pending:
            print(f"{project.relative_to(root)}: patch ready to apply")
        print("Pinned source versions and patches verified.")
        return 0
    for project, patch in pending:
        result = git(project, "apply", str(patch))
        if result.returncode:
            print(result.stderr, file=sys.stderr)
            return 1
        print(f"{project.relative_to(root)}: applied {patch.name}")
    print("Dart MicroPython sources ready.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
