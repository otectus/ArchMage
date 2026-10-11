#!/usr/bin/env python3
"""Dependency manifest check (spec §5.3, plan D-04, S1-0.8).

    python3 -I tests/docs/manifest_test.py                    structure and coverage (scripts/check.sh)
    python3 -I tests/docs/manifest_test.py --check-installed  also compare pinned versions with pacman

Fails when a dependency lookup in a CMake file (find_package or pkg_check_modules) has no entry in
docs/dependencies/manifest.yaml, when an entry claims a lookup no CMake file performs, or when an
entry lacks a spec §5.3 field. --check-installed reports drift between pinned and installed versions.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("manifest_test.py: PyYAML is required (Arch package python-yaml)", file=sys.stderr)
    sys.exit(2)

ROOT = Path(__file__).resolve().parents[2]
REQUIRED = ("id", "name", "category", "usage", "source", "version", "checksum", "build_options",
            "license", "redistribution", "maintenance", "affects")
CATEGORIES = ("library", "header_only_library", "toolchain", "build_tool", "test_framework",
              "runtime_component", "development_tool", "font", "fixture", "model", "service")
USAGES = ("linked", "found_not_linked", "build_time", "test_time", "runtime", "not_used")
ASIDE_LISTS = ("fonts", "brushes", "fixtures", "models", "services")
CHECKSUM = re.compile(r"^sha256:[0-9a-f]{64}$")
FIND_PACKAGE = re.compile(r"\bfind_package\(\s*([A-Za-z0-9_]+)")
PKG_CHECK = re.compile(r"\bpkg_check_modules\(\s*\w+\s+([^)]*)\)")
PKG_KEYWORDS = {"REQUIRED", "QUIET", "IMPORTED_TARGET", "GLOBAL", "NO_CMAKE_PATH", "NO_CMAKE_ENVIRONMENT_PATH"}


def cmake_files() -> list[Path]:
    skipped = {"build", ".cache", ".git"}
    files = [p for p in ROOT.rglob("CMakeLists.txt") if not skipped & set(p.relative_to(ROOT).parts)]
    files += [p for p in ROOT.rglob("*.cmake") if not skipped & set(p.relative_to(ROOT).parts)]
    return sorted(files)


def cmake_lookups() -> dict[str, list[str]]:
    """Map each lookup ("find_package:Qt6", "pkg_config:libxxhash") to the files performing it."""
    found: dict[str, list[str]] = {}
    for path in cmake_files():
        text = "\n".join(line.split("#", 1)[0] for line in path.read_text(encoding="utf-8").splitlines())
        names = [f"find_package:{m}" for m in FIND_PACKAGE.findall(text)]
        for args in PKG_CHECK.findall(text):
            for token in args.split():
                if token not in PKG_KEYWORDS:
                    names.append("pkg_config:" + re.split(r"[<>=]", token, maxsplit=1)[0])
        for name in names:
            found.setdefault(name, []).append(str(path.relative_to(ROOT)))
    return found


def check(manifest: object) -> list[str]:
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        return ["manifest: top level must be a mapping with schema_version: 1"]
    errors = []
    for key in ASIDE_LISTS:
        if not isinstance(manifest.get(key), list):
            errors.append(f"manifest: {key} must be a list (empty when there are none)")
    entries = manifest.get("dependencies")
    if not isinstance(entries, list) or not entries:
        return errors + ["manifest: dependencies must be a non-empty list"]

    claimed: dict[str, str] = {}
    ids = set()
    for index, entry in enumerate(entries):
        where = f"dependencies[{index}] ({entry.get('id', '?') if isinstance(entry, dict) else '?'})"
        if not isinstance(entry, dict):
            errors.append(f"{where}: entry must be a mapping")
            continue
        for key in REQUIRED:
            value = entry.get(key)
            if value is None or value == "" or (key != "affects" and value in ([], {})):
                errors.append(f"{where}: missing {key}")
        if entry.get("id") in ids:
            errors.append(f"{where}: duplicate id")
        ids.add(entry.get("id"))
        if entry.get("category") not in CATEGORIES:
            errors.append(f"{where}: category {entry.get('category')!r} not in {CATEGORIES}")
        if entry.get("usage") not in USAGES:
            errors.append(f"{where}: usage {entry.get('usage')!r} not in {USAGES}")
        affects = entry.get("affects")
        if not isinstance(affects, list) or (not affects and entry.get("usage") != "not_used"):
            errors.append(f"{where}: affects must list the affected features")
        checksum = entry.get("checksum")
        if checksum is not None and not CHECKSUM.match(str(checksum)):
            errors.append(f"{where}: checksum must look like sha256:<64 lowercase hex digits>")
        source = entry.get("source")
        if isinstance(source, dict) and source.get("type") == "system_package":
            if not source.get("package") or not source.get("repository"):
                errors.append(f"{where}: a system_package source needs package and repository")
        elif source is not None:
            errors.append(f"{where}: source.type must be system_package (the only kind in Stage 1)")
        for lookup in entry.get("lookups", []):
            if lookup in claimed:
                errors.append(f"{where}: lookup {lookup} already claimed by {claimed[lookup]}")
            claimed[lookup] = str(entry.get("id"))

    performed = cmake_lookups()
    for lookup, files in sorted(performed.items()):
        if lookup not in claimed:
            errors.append(f"{lookup} (in {', '.join(sorted(set(files)))}) has no manifest entry")
    for lookup, owner in sorted(claimed.items()):
        if lookup not in performed:
            errors.append(f"{owner}: claims {lookup}, which no CMake file performs")
    return errors


def installed_drift(manifest: dict) -> list[str]:
    errors = []
    for entry in manifest["dependencies"]:
        package = entry["source"]["package"]
        result = subprocess.run(["pacman", "-Q", package], capture_output=True, text=True)
        if result.returncode != 0:
            errors.append(f"{entry['id']}: package {package} is not installed")
            continue
        installed = result.stdout.split()[1]
        if installed != str(entry["version"]):
            errors.append(f"{entry['id']}: pinned {entry['version']}, installed {installed}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check-installed", action="store_true",
                        help="also compare pinned versions with the installed pacman packages")
    args = parser.parse_args()

    path = ROOT / "docs/dependencies/manifest.yaml"
    try:
        manifest = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        print(f"manifest_test.py: cannot read {path}: {error}", file=sys.stderr)
        return 2

    errors = check(manifest)
    if not errors and args.check_installed:
        errors = installed_drift(manifest)
    for error in errors:
        print(f"error: {error}", file=sys.stderr)
    if errors:
        print(f"manifest_test.py: {len(errors)} problem(s)", file=sys.stderr)
        return 1
    suffix = "; pinned versions match the installed packages" if args.check_installed else ""
    print(f"manifest_test.py: {len(manifest['dependencies'])} entries cover "
          f"{len(cmake_lookups())} CMake lookups{suffix}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
