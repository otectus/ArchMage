#!/usr/bin/env python3
"""Run a command's windows on a temporary headless Hyprland output (native Wayland, no physical monitor).

Usage: python3 -I scripts/experiments/wayland_run.py --class <app-id regex> [--scale 1.0] [--focus]
                                                      -- <command...>

Creates one headless output, adds a window rule that floats windows of the given class onto it, runs the
command with QT_QPA_PLATFORM=wayland, then disables the rule and removes the output. Without --focus the
windows open silently and never become active, so the user's focus is untouched. With --focus they take
keyboard focus, which tests that wait for an active window need; run that only while nobody is using the
desktop. Focus and cursor are restored afterwards only if they ended up on the headless output.
Exit code: the command's.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time

OUTPUT = "ARCHMAGE-RUN"
RULE = "archmage_run_rule"


def hypr(*args: str) -> str:
    return subprocess.run(["hyprctl", *args], check=True, capture_output=True, text=True).stdout.strip()


def quiet(*args: str) -> None:
    subprocess.run(["hyprctl", *args], capture_output=True, text=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--class", dest="window_class", required=True)
    parser.add_argument("--scale", default="1")
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--focus", action="store_true", help="let the windows take keyboard focus")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a command is required after --")

    cursor = hypr("cursorpos")
    workspace = json.loads(hypr("-j", "activeworkspace"))["id"]
    hypr("output", "create", "headless", OUTPUT)
    try:
        time.sleep(0.5)
        hypr("eval", f'hl.monitor({{ output = "{OUTPUT}", mode = "1920x1080@60", position = "auto", scale = {args.scale} }})')
        time.sleep(0.8)
        target = next(m for m in json.loads(hypr("-j", "monitors")) if m["name"] == OUTPUT)["activeWorkspace"]["id"]
        hypr("eval", f'{RULE} = hl.window_rule({{ name = "archmage-run", match = {{ class = "{args.window_class}" }}, '
                     f'float = true, workspace = "{target}{"" if args.focus else " silent"}" }})')
        env = {k: v for k, v in os.environ.items() if k not in ("QT_QPA_PLATFORMTHEME", "QT_STYLE_OVERRIDE")}
        env["QT_QPA_PLATFORM"] = "wayland"
        mode = "focused" if args.focus else "silent"
        print(f"+ running on {OUTPUT} (workspace {target}, scale {args.scale}, {mode}): {' '.join(command)}", flush=True)
        result = subprocess.run(command, env=env, timeout=args.timeout)
        return result.returncode
    finally:
        quiet("eval", f"if {RULE} then {RULE}:set_enabled(false) end")
        # Put focus and cursor back only if the run took them; a user who moved on meanwhile keeps their place.
        try:
            took_focus = json.loads(hypr("-j", "activeworkspace")).get("monitor") == OUTPUT
        except (subprocess.CalledProcessError, ValueError):
            took_focus = True
        quiet("output", "remove", OUTPUT)
        time.sleep(0.6)
        if took_focus:
            quiet("dispatch", f'hl.dsp.focus({{ workspace = "{workspace}" }})')
            x, y = (int(v) for v in cursor.split(","))
            quiet("dispatch", f"hl.dsp.cursor.move({{ x = {x}, y = {y} }})")


if __name__ == "__main__":
    sys.exit(main())
