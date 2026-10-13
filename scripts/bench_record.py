#!/usr/bin/env python3
"""Records the spec §25.2 benchmarks for one reference profile (plan S1-6.8, spec §25.1).

Usage:
  python3 -I scripts/bench_record.py [--profile A|C] [--build build/dev-release] [--wayland]
                                     [--wait-idle MINUTES | --force]

Runs, against a release build:
  - bench_main (warm pan/zoom, pixel update after a stroke, small undo, CPU and GPU composites);
  - the desktop application's brush latency benchmark (offscreen, 10 s);
  - five launches of the desktop application to its first painted frame (offscreen, or with --wayland on a
    temporary headless Hyprland output via scripts/experiments/wayland_run.py);
and writes benchmarks/results/<date>-bench-<profile>.json with the results and the spec §25.1 record
fields (OS, compositor, driver, compiler, power mode, thermal conditions, storage, monitor scale, binary
hashes). Existing result files are never overwritten: a second run the same day gets a numbered name.

Profile C (16 GiB RAM, CPU-only) is simulated with ARCHMAGE_FORCE_CPU=1 and an 8 GiB RAM budget. Profiles B and
D need machines that do not exist here; `--profile B` and `--profile D` write an "unavailable" record with
the commands that would be run.

Measurements are only meaningful on an otherwise idle machine: the script refuses to run while the
one-minute load average exceeds 2 or GPU utilization exceeds 10 %, unless --force is given, in which case
the record says so. With --wait-idle N it instead polls once a minute and starts after N consecutive quiet
minutes.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import importlib.util
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_brush_module():
    spec = importlib.util.spec_from_file_location("brush_latency", ROOT / "scripts/benchmarks/brush_latency.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def quiet_machine() -> tuple[bool, str]:
    load = float(Path("/proc/loadavg").read_text().split()[0])
    try:
        utilization = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"],
                                     capture_output=True, text=True, timeout=20).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        utilization = "0"
    gpu = int(utilization.splitlines()[0]) if utilization and utilization.splitlines()[0].isdigit() else 0
    reasons = []
    if load > 2.0:
        reasons.append(f"load average {load:.2f} > 2")
    if gpu > 10:
        reasons.append(f"GPU utilization {gpu} % > 10 %")
    return (not reasons, "; ".join(reasons) if reasons else f"load average {load:.2f}, GPU utilization {gpu} %")


def wait_until_quiet(minutes: int) -> str:
    """Polls once a minute until the machine has been quiet for `minutes` consecutive checks."""
    streak = 0
    while True:
        quiet, conditions = quiet_machine()
        streak = streak + 1 if quiet else 0
        if streak >= minutes:
            return conditions
        time.sleep(60)


def evidence(output: str, tag: str) -> dict | None:
    lines = [line for line in output.splitlines() if line.startswith(f"EVIDENCE {tag} ")]
    return json.loads(lines[-1].split(" ", 2)[2]) if lines else None


def qt_env(extra: dict[str, str] | None = None) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if k not in ("QT_QPA_PLATFORMTHEME", "QT_STYLE_OVERRIDE")}
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.update(extra or {})
    return env


def unavailable(profile: str) -> dict:
    reasons = {
        "B": "No 4-8 core, 16 GiB machine with integrated graphics only is available; this machine (24 threads, "
             "32 GiB, discrete GPU) cannot stand in for it without misrepresenting CPU and memory bandwidth.",
        "D": "No Apple Silicon machine is available, and Stage 1 builds for Linux only (ADR-0021).",
    }
    return {
        "profile": profile,
        "status": "unavailable",
        "reason": reasons[profile],
        "would_run": [
            "cmake --preset dev-release && cmake --build --preset dev-release",
            f"python3 -I scripts/bench_record.py --profile {profile}  # on the profile {profile} machine",
        ],
        "date": datetime.date.today().isoformat(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--profile", default="A", choices=["A", "B", "C", "D"])
    parser.add_argument("--build", type=Path, default=ROOT / "build/dev-release")
    parser.add_argument("--wayland", action="store_true", help="launch on a temporary headless Hyprland output")
    parser.add_argument("--force", action="store_true", help="record even on a busy machine (marked in the record)")
    parser.add_argument("--wait-idle", type=int, default=0, metavar="MINUTES",
                        help="wait for this many consecutive quiet minutes instead of refusing")
    parser.add_argument("--launches", type=int, default=5)
    args = parser.parse_args()

    today = datetime.date.today().isoformat()
    results = ROOT / "benchmarks/results"
    out = results / f"{today}-bench-{args.profile}.json"
    n = 2
    while out.exists():
        out = results / f"{today}-bench-{args.profile}-{n}.json"
        n += 1
    if args.profile in ("B", "D"):
        out.write_text(json.dumps(unavailable(args.profile), indent=2) + "\n")
        print(f"profile {args.profile} unavailable; wrote {out}")
        return 0

    if args.wait_idle > 0:
        print(f"waiting for {args.wait_idle} consecutive quiet minutes", flush=True)
        wait_until_quiet(args.wait_idle)
    quiet, conditions = quiet_machine()
    if not quiet and not args.force:
        print(f"refusing to record on a busy machine ({conditions}); rerun when idle or pass --force", file=sys.stderr)
        return 3

    bench_main = args.build / "benchmarks/bench_main"
    app = args.build / "apps/archmage/archmage"
    for binary in (bench_main, app):
        if not binary.exists():
            print(f"missing {binary}; build the dev-release preset first", file=sys.stderr)
            return 2
    brush = load_brush_module()
    extra = {"ARCHMAGE_FORCE_CPU": "1"} if args.profile == "C" else {}
    started = datetime.datetime.now(datetime.timezone.utc)
    thermal_before = brush.cpu_temperature()

    run = subprocess.run([str(bench_main), "--profile", args.profile], capture_output=True, text=True,
                         env=qt_env(extra), timeout=900)
    bench = evidence(run.stdout, "bench")
    if run.returncode != 0 or bench is None:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
        return 1

    run = subprocess.run([str(app), "--benchmark=brush-latency", "--seconds=10", "--canvas=4096"], capture_output=True,
                         text=True, env=qt_env(extra), timeout=300)
    brush_result = evidence(run.stdout, "brush-latency")

    launches = []
    for _ in range(args.launches):
        timer = [sys.executable, "-I", str(ROOT / "scripts/benchmarks/launch_timer.py"), "--", str(app), "--benchmark=launch"]
        command = ([sys.executable, "-I", str(ROOT / "scripts/experiments/wayland_run.py"), "--class", "^archmage$", "--"]
                   + timer) if args.wayland else timer
        run = subprocess.run(command, capture_output=True, text=True, env=qt_env(extra) if not args.wayland else None,
                             timeout=120)
        result = evidence(run.stdout, "launch-wall")
        if result:
            launches.append(result)
    thermal_after = brush.cpu_temperature()

    walls = [launch["wall_ms"] for launch in launches if "wall_ms" in launch]
    record = {
        "benchmark": "bench",
        "profile": args.profile,
        "simulated": bench.get("simulated"),
        "date": started.isoformat(timespec="seconds"),
        "machine_conditions": conditions + ("" if quiet else " (recorded with --force: results are not representative)"),
        # The revision compiled into bench_main (the record may run long after the checkout has moved on);
        # environment.git_revision is the checkout at record time.
        "build": bench.get("build"),
        "fixture": bench["fixture"],
        "records": {
            **bench["records"],
            "brush_preview": {**(brush_result or {"error": "no result"}), "budget_p95_ms": 25.0,
                              "boundary": "event to submission, offscreen (S1-3.7)"},
            "launch": {"runs": launches, "budget_warm_ms": 2000, "budget_cold_ms": 5000,
                       "warm_median_ms": statistics.median(walls[1:]) if len(walls) > 1 else None,
                       "first_run_ms": walls[0] if walls else None,
                       "cold": "not measured: a cold launch needs the page cache dropped (root)",
                       "platform": "wayland on a temporary headless output" if args.wayland else "offscreen",
                       "boundary": "process spawn to the main window's first paint plus one event-loop turn"},
        },
        "ram_peak_mib": bench.get("ram_peak_mib"),
        "evaluator_threads": bench.get("evaluator_threads"),
        "environment": {**brush.environment(app),
                        "bench_main_sha256": hashlib.sha256(bench_main.read_bytes()).hexdigest(),
                        "monitor_scale": "1.0 (offscreen frame source at 2560x1600 device pixels)",
                        "thermal": {"cpu_package_c_before": thermal_before, "cpu_package_c_after": thermal_after}},
    }
    out.write_text(json.dumps(record, indent=2) + "\n")
    summary = {name: (value.get("p95_ms"), value.get("pass")) for name, value in record["records"].items()
               if isinstance(value, dict) and "p95_ms" in value}
    print(f"profile {args.profile}: {summary}; launch warm median {record['records']['launch']['warm_median_ms']} ms; wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
