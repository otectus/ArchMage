#!/usr/bin/env python3
"""Brush latency benchmark record (plan S1-3.7, spec §25.1-§25.2).

Runs `archmage --benchmark=brush-latency` on Qt's offscreen platform (CPU event-to-submission, the spec's
measurement boundary for "simple brush preview") and writes benchmarks/results/<date>-brush.json with the
result and the spec §25.1 record fields: OS, compositor, driver, compiler, power mode, thermal
conditions, storage, monitor scale and the exact fixture hash.

Run with `python3 -I scripts/benchmarks/brush_latency.py [--binary PATH] [--seconds N]`.
"""

from __future__ import annotations

import argparse
import datetime
import glob
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(*command: str) -> str:
    try:
        return subprocess.run(command, capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def read(path: str) -> str:
    try:
        return Path(path).read_text().strip()
    except OSError:
        return ""


def cpu_temperature() -> float | None:
    for zone in glob.glob("/sys/class/thermal/thermal_zone*"):
        if read(f"{zone}/type") in ("x86_pkg_temp", "TCPU", "cpu-thermal"):
            value = read(f"{zone}/temp")
            if value.isdigit():
                return int(value) / 1000.0
    return None


def environment(binary: Path) -> dict:
    os_release = dict(line.split("=", 1) for line in read("/etc/os-release").splitlines() if "=" in line)
    cache = read(str(binary.parents[2] / "CMakeCache.txt"))
    compiler = re.search(r"^CMAKE_CXX_COMPILER:\w+=(.*)$", cache, re.M)
    build_type = re.search(r"^CMAKE_BUILD_TYPE:\w+=(.*)$", cache, re.M)
    cpu = re.search(r"^model name\s*:\s*(.*)$", read("/proc/cpuinfo"), re.M)
    memory = re.search(r"^MemTotal:\s*(\d+)", read("/proc/meminfo"), re.M)
    source = run("findmnt", "-n", "-o", "SOURCE", "--target", str(ROOT))
    disk = re.sub(r"p?\d+(\[.*\])?$", "", source.split("[")[0]) if source.startswith("/dev/") else source
    return {
        "os": f"{os_release.get('PRETTY_NAME', '').strip(chr(34))}, kernel {platform.release()}",
        "compositor": "none: Qt offscreen platform (no presentation); session compositor "
                      + (run("hyprctl", "version").splitlines() or ["unknown"])[0],
        "driver": {"nvidia": read("/sys/module/nvidia/version") or None,
                   "mesa": (run("pacman", "-Q", "mesa").split() + [None, None])[1]},
        "compiler": {"path": compiler.group(1) if compiler else None,
                     "version": (run(compiler.group(1), "--version").splitlines() or [""])[0] if compiler else None,
                     "build_type": build_type.group(1) if build_type else None},
        "power_mode": read("/sys/firmware/acpi/platform_profile") or run("powerprofilesctl", "get") or "unknown",
        "cpu": cpu.group(1) if cpu else platform.processor(),
        "logical_cpus": os.cpu_count(),
        "memory_gib": round(int(memory.group(1)) / 1024 / 1024, 1) if memory else None,
        "storage": {"source": source, "model": run("lsblk", "-dno", "MODEL", disk).strip() if disk.startswith("/dev/") else None},
        "monitor_scale": "1.0 (offscreen platform, no monitor)",
        "git_revision": run("git", "-C", str(ROOT), "rev-parse", "--short=12", "HEAD")
                        + ("-dirty" if run("git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no") else ""),
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dev-release/apps/archmage/archmage")
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--canvas", type=int, default=4096)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()
    if not args.binary.exists():
        print(f"missing {args.binary}; build the dev-release preset first", file=sys.stderr)
        return 2

    # The fixture is generated, so its "exact file hash" is the hash of the generating command.
    fixture = {"commandId": "archmage.document.create",
               "parameters": {"width": args.canvas, "height": args.canvas, "background": "white"}}
    fixture_hash = hashlib.sha256(json.dumps(fixture, sort_keys=True).encode()).hexdigest()

    env = {k: v for k, v in os.environ.items() if k not in ("QT_QPA_PLATFORMTHEME", "QT_STYLE_OVERRIDE")}
    env["QT_QPA_PLATFORM"] = "offscreen"
    thermal_before = cpu_temperature()
    started = datetime.datetime.now(datetime.timezone.utc)
    proc = subprocess.run([str(args.binary), "--benchmark=brush-latency", f"--seconds={args.seconds}",
                           f"--canvas={args.canvas}"], capture_output=True, text=True, env=env, timeout=args.seconds + 120)
    thermal_after = cpu_temperature()
    lines = [l for l in proc.stdout.splitlines() if l.startswith("EVIDENCE brush-latency ")]
    if proc.returncode != 0 or not lines:
        print(proc.stdout[-2000:], proc.stderr[-2000:], file=sys.stderr)
        return 1
    result = json.loads(lines[-1].split(" ", 2)[2])
    measured_rate = result["events"] / args.seconds
    record = {
        "benchmark": "brush-latency",
        "spec_budget": "simple brush preview p95 <= 25 ms CPU/event-to-submission (spec §25.2, profile A)",
        "profile": "A (measured on the reference development machine)",
        "date": started.isoformat(timespec="seconds"),
        "duration_s": args.seconds,
        "fixture": {**fixture, "sha256": fixture_hash},
        "input": {"kind": "synthetic QTabletEvent with a pressure ramp", "nominal_rate_hz": result["input_rate_hz"],
                  "measured_rate_hz": round(measured_rate, 1)},
        "result": result,
        "pass": result["p95_ms"] <= 25.0,
        "environment": {**environment(args.binary),
                        "thermal": {"cpu_package_c_before": thermal_before, "cpu_package_c_after": thermal_after}},
    }
    out = args.out or ROOT / "benchmarks/results" / f"{started.date().isoformat()}-brush.json"
    out.write_text(json.dumps(record, indent=2) + "\n")
    print(f"brush latency p50 {result['p50_ms']:.2f} ms, p95 {result['p95_ms']:.2f} ms, max {result['max_ms']:.2f} ms "
          f"over {result['frames']} frames -> {'PASS' if record['pass'] else 'FAIL'} (budget p95 <= 25 ms); wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
