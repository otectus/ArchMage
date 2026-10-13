#!/usr/bin/env python3
"""Times one application launch (plan S1-6.8, spec §25.2 "empty-app launch").

Usage: python3 -I scripts/benchmarks/launch_timer.py -- <archmage> --benchmark=launch

Starts the command, waits for its "EVIDENCE launch <json>" line, and prints
"EVIDENCE launch-wall <json>" with the wall time from spawning the process to that line, which spans
process start, dynamic loading, Qt start-up, the main window and its first paint.
"""

from __future__ import annotations

import json
import subprocess
import sys
import time


def main() -> int:
    command = sys.argv[1:]
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        print(__doc__, file=sys.stderr)
        return 2
    started = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    ready = None
    for line in process.stdout:
        if line.startswith("EVIDENCE launch "):
            ready = (time.monotonic() - started) * 1000.0
            inner = json.loads(line.split(" ", 2)[2])
            break
    process.wait(timeout=60)
    if ready is None:
        print("EVIDENCE launch-wall {\"error\": \"no ready line\"}", flush=True)
        return 1
    print("EVIDENCE launch-wall " + json.dumps({"wall_ms": round(ready, 1), **inner}), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
