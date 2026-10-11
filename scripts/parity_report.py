#!/usr/bin/env python3
"""Validate the ArchMage feature ledger and render its report (spec §3.2-3.3, plan D-30).

    python3 -I scripts/parity_report.py           validate, then write docs/parity/report.md
    python3 -I scripts/parity_report.py --check   validate, regenerate in memory, fail on any diff

Validation covers the entry schema (spec §3.2, App. A.1; fields in docs/parity/README.md) and
completeness against the specification: every requirement row in the spec tables of the
ledger's domains must have a parent entry whose outcome repeats the row verbatim.

Exit status: 0 success; 1 validation errors or a stale report; 2 usage or environment errors.
"""

from __future__ import annotations

import argparse
import datetime
import difflib
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("parity_report.py: PyYAML is required (Arch package python-yaml)", file=sys.stderr)
    sys.exit(2)

ROOT = Path(__file__).resolve().parent.parent

STATES = ("unstarted", "researching", "partial", "implemented", "verified", "blocked")
FIDELITIES = ("equivalent", "approximate", "raster_fallback", "preserved_uneditable", "unsupported")
SCOPES = ("current_baseline", "extended_capability", "legacy_compatibility", "external_service")
EVIDENCE = ("public_documentation_only", "hands_on")
MILESTONES = tuple(f"M{n}" for n in range(10))
REQUIRED_KEYS = (
    "id", "parent", "title", "outcome", "scope", "milestone", "prerequisites",
    "implementation_state", "fidelity_target", "fidelity_verified", "benchmark",
    "planned_capabilities", "command_id", "acceptance", "test_scenarios",
    "platform_results", "limitations",
)
OPTIONAL_KEYS = (
    "spec_section", "milestone_basis", "discovered_on", "entry_points", "options",
    "document_modes", "precision", "alpha", "layer_types", "animation",
    "persistence_version", "test_ids", "demonstration_fixture", "requirements",
)
BENCHMARK_KEYS = ("product", "edition", "observed_version", "source_url", "observed_date", "evidence")
ID_PATTERN = re.compile(r"^[A-Z]{2,3}-\d{2}(-[a-z0-9]+)*$")
ROW_ID_PATTERN = re.compile(r"^[A-Z]{2,3}-\d{2}$")
COMMAND_PATTERN = re.compile(r"^archmage(\.[A-Za-z][A-Za-z0-9]*)+$")
SCENARIO_PATTERN = re.compile(r"^AT-\d{3}$")


def normalize(text: str) -> str:
    return " ".join(text.split())


def spec_rows(spec_text: str, prefixes: list[str]) -> dict[str, list[str]]:
    """Requirement rows `| XXX-NN | cell | ... |` of the given domains, keyed by ID."""
    pattern = re.compile(r"^\| ((?:%s)-\d{2}) \| (.*) \|\s*$" % "|".join(map(re.escape, prefixes)))
    rows: dict[str, list[str]] = {}
    for line in spec_text.splitlines():
        match = pattern.match(line)
        if match:
            rows[match.group(1)] = [cell.strip() for cell in match.group(2).split(" | ")]
    return rows


def is_text(value: object) -> bool:
    return isinstance(value, str) and value.strip() != ""


def validate_entry(entry: dict, where: str, prefixes: set[str]) -> list[str]:
    errors = []
    missing = [key for key in REQUIRED_KEYS if key not in entry]
    unknown = sorted(set(entry) - set(REQUIRED_KEYS) - set(OPTIONAL_KEYS))
    if missing:
        errors.append(f"{where}: missing keys {missing}")
    if unknown:
        errors.append(f"{where}: unknown keys {unknown}")
    if missing:
        return errors

    entry_id = entry["id"]
    if not isinstance(entry_id, str) or not ID_PATTERN.match(entry_id):
        errors.append(f"{where}: id {entry_id!r} must look like NAV-01 or NAV-01-short-slug")
    elif entry_id.split("-")[0] not in prefixes:
        errors.append(f"{where}: id prefix of {entry_id!r} is not a ledger domain")
    for key in ("title", "outcome"):
        if not is_text(entry[key]):
            errors.append(f"{where}: {key} must be non-empty text")

    state = entry["implementation_state"]
    if state not in STATES:
        errors.append(f"{where}: implementation_state {state!r} not in {STATES}")
    if entry["scope"] is None:
        if state not in ("unstarted", "researching"):
            errors.append(f"{where}: scope must be classified once work starts (state {state!r})")
    elif entry["scope"] not in SCOPES:
        errors.append(f"{where}: scope {entry['scope']!r} not in {SCOPES}")
    if entry["milestone"] not in MILESTONES:
        errors.append(f"{where}: milestone {entry['milestone']!r} not in M0..M9")
    for key in ("fidelity_target", "fidelity_verified"):
        if entry[key] is not None and entry[key] not in FIDELITIES:
            errors.append(f"{where}: {key} {entry[key]!r} not in {FIDELITIES}")
    if state == "verified":
        if entry["fidelity_verified"] is None:
            errors.append(f"{where}: a verified entry needs fidelity_verified")
        if not entry["test_scenarios"] and not entry.get("test_ids"):
            errors.append(f"{where}: a verified entry needs test_ids or test_scenarios")

    for key in ("prerequisites", "acceptance", "test_scenarios", "limitations"):
        if not isinstance(entry[key], list):
            errors.append(f"{where}: {key} must be a list")
    for key in ("planned_capabilities", "platform_results"):
        if not isinstance(entry[key], dict):
            errors.append(f"{where}: {key} must be a mapping")
    if isinstance(entry["test_scenarios"], list):
        for scenario in entry["test_scenarios"]:
            if not isinstance(scenario, str) or not SCENARIO_PATTERN.match(scenario):
                errors.append(f"{where}: test scenario {scenario!r} must look like AT-001")
    command = entry["command_id"]
    if command is not None and (not isinstance(command, str) or not COMMAND_PATTERN.match(command)):
        errors.append(f"{where}: command_id {command!r} must look like archmage.layer.setOpacity")

    benchmark = entry["benchmark"]
    if not isinstance(benchmark, dict) or sorted(benchmark) != sorted(BENCHMARK_KEYS):
        errors.append(f"{where}: benchmark needs exactly the keys {BENCHMARK_KEYS}")
    else:
        if benchmark["evidence"] not in EVIDENCE:
            errors.append(f"{where}: benchmark.evidence {benchmark['evidence']!r} not in {EVIDENCE}")
        if benchmark["evidence"] == "hands_on" and benchmark["observed_version"] is None:
            errors.append(f"{where}: hands_on evidence needs the observed_version that was tested")
        observed = benchmark["observed_date"]
        if observed is not None and not isinstance(observed, (str, datetime.date)):
            errors.append(f"{where}: benchmark.observed_date must be a date")
    return errors


def validate(ledger: object, spec_text: str) -> list[str]:
    if not isinstance(ledger, dict):
        return ["ledger: top level must be a mapping"]
    errors = []
    if ledger.get("schema_version") != 1:
        errors.append("ledger: schema_version must be 1")
    meta = ledger.get("ledger")
    if not isinstance(meta, dict) or not all(k in meta for k in ("spec_document", "spec_version", "baseline", "domains")):
        return errors + ["ledger: the ledger block needs spec_document, spec_version, baseline and domains"]
    domains = meta["domains"]
    if not isinstance(domains, list) or not all(
        isinstance(d, dict) and is_text(d.get("prefix")) and is_text(d.get("title")) and is_text(d.get("spec_section"))
        for d in domains
    ):
        return errors + ["ledger: every domain needs prefix, title and spec_section"]
    features = ledger.get("features")
    if not isinstance(features, list):
        return errors + ["ledger: features must be a list"]

    prefixes = {d["prefix"] for d in domains}
    sections = {d["prefix"]: d["spec_section"] for d in domains}
    seen: dict[str, dict] = {}
    for index, entry in enumerate(features):
        where = f"features[{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{where}: entry must be a mapping")
            continue
        where = f"{where} ({entry.get('id', '?')})"
        errors.extend(validate_entry(entry, where, prefixes))
        entry_id = entry.get("id")
        if isinstance(entry_id, str):
            if entry_id in seen:
                errors.append(f"{where}: duplicate id")
            seen[entry_id] = entry

    rows = spec_rows(spec_text, sorted(prefixes))
    for entry_id, entry in seen.items():
        parent = entry.get("parent")
        if parent is None:
            if not ROW_ID_PATTERN.match(entry_id):
                errors.append(f"{entry_id}: a top-level requirement id looks like NAV-01")
            if entry.get("spec_section") != sections.get(entry_id.split("-")[0]):
                errors.append(f"{entry_id}: spec_section must be {sections.get(entry_id.split('-')[0])!r}")
            if entry_id not in rows and "discovered_on" not in entry:
                errors.append(f"{entry_id}: not a spec requirement row; record discovered_on for a new requirement")
        elif parent not in seen:
            errors.append(f"{entry_id}: parent {parent!r} does not exist")
        elif not entry_id.startswith(parent + "-"):
            errors.append(f"{entry_id}: a child id starts with its parent id, e.g. {parent}-short-slug")

    for row_id, cells in rows.items():
        entry = seen.get(row_id)
        if entry is None:
            errors.append(f"{row_id}: spec requirement row has no ledger entry")
        elif entry.get("parent") is not None:
            errors.append(f"{row_id}: spec requirement rows are top-level entries (parent: null)")
        elif not isinstance(entry.get("outcome"), str) or normalize(entry["outcome"]) != normalize(cells[-1]):
            errors.append(f"{row_id}: outcome differs from the specification row")
    return errors


def fidelity(value: object) -> str:
    return "-" if value is None else str(value)


def table(header: list[str], rows: list[list[object]]) -> list[str]:
    lines = ["| " + " | ".join(header) + " |", "| " + " | ".join("---" for _ in header) + " |"]
    lines += ["| " + " | ".join(str(cell) for cell in row) + " |" for row in rows]
    return lines


def render(ledger: dict) -> str:
    meta = ledger["ledger"]
    domains = meta["domains"]
    features = ledger["features"]
    parents = [f for f in features if f["parent"] is None]
    children = [f for f in features if f["parent"] is not None]

    def domain_of(entry: dict) -> str:
        return entry["id"].split("-")[0]

    def unsupported(entry: dict) -> bool:
        return "unsupported" in (entry["fidelity_target"], entry["fidelity_verified"])

    out = [
        "# ArchMage feature ledger report",
        "",
        "Generated by `scripts/parity_report.py` from `docs/parity/features.yaml`; edit the ledger and",
        "regenerate instead of editing this file. Field meanings: `docs/parity/README.md`.",
        "",
        f"- Specification: `{meta['spec_document']}`, version {meta['spec_version']}",
        f"- Baseline: {meta['baseline']}",
        f"- Requirement rows: {len(parents)}; testable behaviors: {len(children)}",
        "",
        "## Behaviors by domain",
        "",
        "Counts of child entries, the testable behaviors spec §3.3 reports on.",
        "",
    ]
    header = ["Domain", "Behaviors", *STATES, "Unsupported"]
    rows = []
    for domain in domains:
        mine = [c for c in children if domain_of(c) == domain["prefix"]]
        counts = [sum(c["implementation_state"] == s for c in mine) for s in STATES]
        rows.append([domain["prefix"], len(mine), *counts, sum(unsupported(c) for c in mine)])
    totals = [sum(row[i] for row in rows) for i in range(1, len(header))]
    out += table(header, rows + [["Total", *totals]])

    out += ["", "## Requirement rows by domain", ""]
    header = ["Domain", "Title", "Spec", "Rows", *STATES]
    rows = []
    for domain in domains:
        mine = [p for p in parents if domain_of(p) == domain["prefix"]]
        counts = [sum(p["implementation_state"] == s for p in mine) for s in STATES]
        rows.append([domain["prefix"], domain["title"], "§" + domain["spec_section"], len(mine), *counts])
    totals = [sum(row[i] for row in rows) for i in range(3, len(header))]
    out += table(header, rows + [["Total", "", "", *totals]])

    out += ["", "## Requirement rows by milestone", ""]
    rows = []
    for milestone in MILESTONES:
        mine = [p for p in parents if p["milestone"] == milestone]
        if mine:
            per_domain = ", ".join(
                f"{d['prefix']} {n}" for d in domains if (n := sum(domain_of(p) == d["prefix"] for p in mine))
            )
            rows.append([milestone, len(mine), per_domain])
    out += table(["Milestone", "Rows", "Domains"], rows)

    discovered = [f for f in features if "discovered_on" in f]
    out += ["", "## Newly discovered requirements", ""]
    if discovered:
        out += table(["ID", "Title", "Discovered"], [[f["id"], f["title"], f["discovered_on"]] for f in discovered])
    else:
        out.append("None recorded.")

    blocked = [f for f in features if f["implementation_state"] == "blocked"]
    out += ["", "## Blocked entries", ""]
    if blocked:
        out += table(["ID", "Title", "Limitations"], [[f["id"], f["title"], "; ".join(f["limitations"])] for f in blocked])
    else:
        out.append("None recorded.")

    out += ["", "## Requirements", ""]
    for domain in domains:
        out += [f"### {domain['prefix']}: {domain['title']} (spec §{domain['spec_section']})", ""]
        rows = []
        for parent in (p for p in parents if domain_of(p) == domain["prefix"]):
            mine = [c for c in children if c["parent"] == parent["id"]]
            rows.append([parent["id"], parent["title"], parent["milestone"], parent["implementation_state"],
                         fidelity(parent["fidelity_target"]), fidelity(parent["fidelity_verified"]), len(mine)])
            for child in mine:
                rows.append([child["id"], child["title"], child["milestone"], child["implementation_state"],
                             fidelity(child["fidelity_target"]), fidelity(child["fidelity_verified"]), "-"])
        out += table(["ID", "Title", "Milestone", "State", "Fidelity target", "Fidelity verified", "Behaviors"], rows)
        out.append("")
    return "\n".join(out).rstrip("\n") + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if the committed report is stale")
    parser.add_argument("--ledger", type=Path, default=ROOT / "docs/parity/features.yaml")
    parser.add_argument("--report", type=Path, default=ROOT / "docs/parity/report.md")
    parser.add_argument("--spec", type=Path, default=None, help="defaults to the ledger's spec_document")
    args = parser.parse_args()

    try:
        ledger = yaml.safe_load(args.ledger.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        print(f"parity_report.py: cannot read {args.ledger}: {error}", file=sys.stderr)
        return 2
    spec_path = args.spec
    if spec_path is None and isinstance(ledger, dict) and isinstance(ledger.get("ledger"), dict):
        spec_path = ROOT / str(ledger["ledger"].get("spec_document", ""))
    try:
        spec_text = spec_path.read_text(encoding="utf-8") if spec_path else ""
    except OSError as error:
        print(f"parity_report.py: cannot read the specification: {error}", file=sys.stderr)
        return 2

    errors = validate(ledger, spec_text)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print(f"parity_report.py: {len(errors)} validation error(s) in {args.ledger}", file=sys.stderr)
        return 1

    rendered = render(ledger)
    entries = len(ledger["features"])
    if args.check:
        current = args.report.read_text(encoding="utf-8") if args.report.exists() else ""
        if current != rendered:
            diff = difflib.unified_diff(current.splitlines(), rendered.splitlines(),
                                        str(args.report), "regenerated", lineterm="")
            print("\n".join(list(diff)[:80]), file=sys.stderr)
            print(f"parity_report.py: {args.report} is stale; run python3 -I scripts/parity_report.py",
                  file=sys.stderr)
            return 1
        print(f"parity_report.py: ledger valid ({entries} entries); report up to date")
        return 0

    args.report.write_text(rendered, encoding="utf-8")
    print(f"parity_report.py: ledger valid ({entries} entries); wrote {args.report}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
