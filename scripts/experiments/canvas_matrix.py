#!/usr/bin/env python3
"""Canvas adapter evidence matrix (plan S1-1.3, M0-E1) on Hyprland.

Runs `archmage --experiment=canvas:<kind>` for each surface kind and Qt platform on two temporary
headless Hyprland outputs (scale 1 and scale 1.5), so the user's physical monitors are never rescaled.
Each SNAP point is captured with grim and checked against the frame the application reports having
presented; popups and tooltips are checked for their probe colors over the canvas.

Run with `python3 -I`. Requires hyprctl, grim, numpy and PIL. The original focused workspace and cursor
position are restored afterwards, and the outputs and window rule are removed even on failure.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

import numpy as np
from PIL import Image

OUTPUT_S1 = "ARCHMAGE-S10"
OUTPUT_S15 = "ARCHMAGE-S15"
RULE = "archmage_experiment_rule"

SCRIPT = [
    "actual", "report:s1-start", "popup:s1-popup", "tooltip:s1-tooltip",
    "dock-float:s1-float", "dock-redock:s1-redock", "resize-storm:s1-storm",
    "focus-me", "dialog:s1-focus", "unfocus-me", "drag:3000",
    "move:s15", "actual", "report:s15-start", "popup:s15-popup", "tooltip:s15-tooltip",
    "move:s10", "actual", "report:s10-back", "quit",
]


def hypr(*args: str) -> str:
    return subprocess.run(["hyprctl", *args], check=True, capture_output=True, text=True).stdout.strip()


def hypr_json(what: str):
    return json.loads(hypr("-j", what))


def dispatch(lua: str) -> str:
    return hypr("dispatch", lua)


def try_dispatch(lua: str) -> None:
    """Best-effort dispatch for cleanup paths: never raises."""
    subprocess.run(["hyprctl", "dispatch", lua], capture_output=True, text=True)


def monitors() -> dict[str, dict]:
    return {m["name"]: m for m in hypr_json("monitors")}


def clients_of(pid: int) -> list[dict]:
    return [c for c in hypr_json("clients") if c.get("pid") == pid]


_MAIN_ADDRESS: dict[int, str] = {}


def main_client(pid: int) -> dict | None:
    """The experiment's main window, tracked by address once found (dock windows share the pid)."""
    clients = clients_of(pid)
    address = _MAIN_ADDRESS.get(pid)
    if address:
        return next((c for c in clients if c["address"] == address), None)
    for c in clients:
        if c.get("title", "").startswith("ArchMage canvas experiment"):
            _MAIN_ADDRESS[pid] = c["address"]
            return c
    return None


TOP_CLEARANCE = 80  # logical pixels kept free at the top of an output, where the user's bar lives


def center_main(pid: int) -> None:
    """Centers the main window on its output, below the top clearance (bars show personal data)."""
    client = main_client(pid)
    if client is None:
        return
    mon = next((m for m in hypr_json("monitors") if m["id"] == client["monitor"]), None)
    if mon is None:
        return
    width = mon["width"] / mon["scale"]
    height = mon["height"] / mon["scale"]
    x = mon["x"] + max(0, (width - client["size"][0]) / 2)
    y = mon["y"] + max(TOP_CLEARANCE, (height - client["size"][1]) / 2)
    dispatch(f'hl.dsp.window.move({{ x = {int(x)}, y = {int(y)}, window = "address:{client["address"]}" }})')
    time.sleep(0.4)


def park_auxiliary(pid: int, titles: tuple[str, ...]) -> None:
    """Moves dock windows to the top-left of their output so they never cover the canvas."""
    mons = {m["id"]: m for m in hypr_json("monitors")}
    for c in clients_of(pid):
        if c.get("title") in titles:
            mon = mons.get(c["monitor"])
            if mon:
                dispatch(f'hl.dsp.window.move({{ x = {mon["x"] + 10}, y = {mon["y"] + 100}, '
                         f'window = "address:{c["address"]}" }})')


class Desktop:
    """Creates the headless outputs and window rule; restores the user's desktop state on exit."""

    def __enter__(self) -> "Desktop":
        self.cursor = hypr("cursorpos")
        self.workspace = hypr_json("activeworkspace")["id"]
        self.window = hypr_json("activewindow").get("address")
        for name in (OUTPUT_S1, OUTPUT_S15):
            hypr("output", "create", "headless", name)
        time.sleep(0.5)
        for name, scale in ((OUTPUT_S1, 1), (OUTPUT_S15, 1.5)):
            hypr("eval", f'hl.monitor({{ output = "{name}", mode = "1920x1080@60", position = "auto", scale = {scale} }})')
        time.sleep(0.8)
        mons = monitors()
        self.ws = {name: mons[name]["activeWorkspace"]["id"] for name in (OUTPUT_S1, OUTPUT_S15)}
        # Every experiment window floats on the scale-1 output. Titles cannot be matched (XWayland windows
        # map before their title is set), so the driver moves the dock windows clear of the canvas itself.
        rule = (f'{RULE} = hl.window_rule({{ name = "archmage-experiment", '
                f'match = {{ class = "^archmage-experiment$" }}, float = true, '
                f'workspace = "{self.ws[OUTPUT_S1]} silent" }})')
        self.rule_result = hypr("eval", rule)
        self.restore_focus()
        return self

    def restore_focus(self) -> None:
        try_dispatch(f'hl.dsp.focus({{ workspace = "{self.workspace}" }})')
        if self.window:
            try_dispatch(f'hl.dsp.focus({{ window = "address:{self.window}" }})')
        x, y = (int(v) for v in self.cursor.split(","))
        try_dispatch(f"hl.dsp.cursor.move({{ x = {x}, y = {y} }})")

    def __exit__(self, *exc) -> None:
        # Rule handles can only be disabled (HL.WindowRule has is_enabled and set_enabled).
        subprocess.run(["hyprctl", "eval", f"if {RULE} then {RULE}:set_enabled(false) end"], capture_output=True)
        for name in (OUTPUT_S1, OUTPUT_S15):
            subprocess.run(["hyprctl", "output", "remove", name], capture_output=True)
        time.sleep(0.6)
        self.restore_focus()


def capture(monitor: str, path: Path) -> np.ndarray:
    subprocess.run(["grim", "-o", monitor, str(path)], check=True)
    return np.asarray(Image.open(path).convert("RGBA"))


def qt_to_device(client: dict, mon: dict, window_logical: list[int] | None) -> float:
    """Device pixels per Qt coordinate unit inside the window.

    Wayland clients see logical pixels (factor = output scale). XWayland clients with
    xwayland.force_zero_scaling see device pixels (factor 1) on every output.
    """
    if not window_logical or not window_logical[0]:
        return mon["scale"]
    return client["size"][0] * mon["scale"] / window_logical[0]


def canvas_origin(client: dict, mon: dict, canvas_logical: list[int], k: float) -> tuple[float, float]:
    return ((client["at"][0] - mon["x"]) * mon["scale"] + canvas_logical[0] * k,
            (client["at"][1] - mon["y"]) * mon["scale"] + canvas_logical[1] * k)


def compare_frame(screen: np.ndarray, frame: np.ndarray, origin: tuple[float, float]) -> dict:
    """Best match of the frame inside the screenshot near the expected origin (±3 px)."""
    h, w = frame.shape[:2]
    best = None
    ox, oy = round(origin[0]), round(origin[1])
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            x0, y0 = ox + dx, oy + dy
            if x0 < 0 or y0 < 0 or x0 + w > screen.shape[1] or y0 + h > screen.shape[0]:
                continue
            crop = screen[y0:y0 + h, x0:x0 + w, :3].astype(np.int16)
            diff = np.abs(crop - frame[:, :, :3].astype(np.int16)).max(axis=2)
            exact = float((diff <= 2).mean())
            if best is None or exact > best["exact_fraction"]:
                best = {"exact_fraction": round(exact, 5), "offset": [dx, dy],
                        "mean_abs_diff": round(float(diff.mean()), 3)}
    if best is None:
        return {"exact_fraction": 0.0, "offset": None, "error": "frame does not fit inside the screenshot"}
    # A half-pixel expected origin (fractional scale) may land on either neighbouring device pixel.
    tolerance = [1 if abs(v - round(v)) > 0.25 else 0 for v in origin]
    aligned = all(abs(o) <= t for o, t in zip(best["offset"], tolerance))
    best["pass"] = best["exact_fraction"] >= 0.995 and aligned
    return best


def color_fraction(screen: np.ndarray, rect: list[float], color: tuple[int, int, int]) -> float:
    x, y, w, h = (int(round(v)) for v in rect)
    x0, y0 = max(0, x), max(0, y)
    crop = screen[y0:y + h, x0:x + w, :3].astype(np.int16)
    if crop.size == 0:
        return 0.0
    return round(float((np.abs(crop - np.array(color)).max(axis=2) <= 40).mean()), 4)


def run_one(binary: Path, platform: str, kind: str, out: Path, desktop: Desktop, timeout: float) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    frames_dir = Path(f"/tmp/archmage-canvas-matrix/{platform}-{kind}")
    shutil.rmtree(frames_dir, ignore_errors=True)
    env = {k: v for k, v in os.environ.items() if k not in ("QT_QPA_PLATFORMTHEME", "QT_STYLE_OVERRIDE")}
    env.update({"QT_QPA_PLATFORM": platform, "ARCHMAGE_VK_VALIDATION": "1", "ARCHMAGE_TRACE": "latency"})
    script = ",".join(s for s in SCRIPT if s not in ("focus-me", "unfocus-me"))
    proc = subprocess.Popen([str(binary), f"--experiment=canvas:{kind}", f"--script={script}",
                             f"--evidence-dir={frames_dir}", "--snap-wait=1500"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env, bufsize=1)
    result: dict = {"platform": platform, "kind": kind, "evidence": {}, "snaps": {}, "latency_lines": [],
                    "validation_lines": [], "log_tail": []}
    log_lines: list[str] = []
    deadline = time.time() + timeout
    geometry: dict[str, dict] = {}
    pending_popup: dict[str, dict] = {}

    def reader():
        for line in proc.stdout:
            log_lines.append(line.rstrip("\n"))
    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    try:
        _drive(proc, thread, log_lines, deadline, result, geometry, pending_popup, out, desktop)
    except Exception as error:  # record and clean up; the matrix continues with the next combination
        result["driver_error"] = repr(error)
    if proc.poll() is None:
        proc.kill()
        result["killed"] = True
    proc.wait(timeout=10)
    thread.join(timeout=5)
    desktop.restore_focus()
    result["exit_code"] = proc.returncode
    result["log_tail"] = log_lines[-40:]
    (out / "log.txt").write_text("\n".join(log_lines) + "\n")
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def _drive(proc, thread, log_lines, deadline, result, geometry, pending_popup, out, desktop) -> None:
    focus_steal = None
    last_window_logical: list = [None]
    index = 0
    while time.time() < deadline:
        if index >= len(log_lines):
            if proc.poll() is not None and not thread.is_alive():
                break
            time.sleep(0.02)
            continue
        line = log_lines[index]
        index += 1
        if line.startswith("EVIDENCE "):
            _, kind_name, payload = line.split(" ", 2)
            data = json.loads(payload)
            result["evidence"].setdefault(kind_name, []).append(data)
            if kind_name == "started":
                time.sleep(0.5)
                main_client(proc.pid)
                center_main(proc.pid)
                park_auxiliary(proc.pid, ("Navigator",))
                desktop.restore_focus()
            if kind_name == "geometry":
                geometry[data["tag"]] = data
                last_window_logical[0] = data.get("window_logical")
            if kind_name in ("popup", "tooltip"):
                pending_popup[data["tag"]] = {"type": kind_name, **data}
        elif line.startswith("STEP "):
            # Popups need a parent that has received input and the focus probe needs an active window, so
            # those steps focus the experiment window briefly; every other step hands focus back.
            if line.startswith("STEP dock-float:"):
                time.sleep(0.5)
                park_auxiliary(proc.pid, ("Layers",))
            if line.startswith("STEP quit"):
                result["teardown_validation_from"] = len(result["validation_lines"])
            if line.startswith(("STEP popup:", "STEP tooltip:", "STEP dialog:")):
                client = main_client(proc.pid)
                if client and focus_steal is None:
                    dispatch(f'hl.dsp.focus({{ window = "address:{client["address"]}" }})')
                    focus_steal = time.time()
                    time.sleep(0.3)
            elif focus_steal is not None:
                desktop.restore_focus()
                focus_steal = None
        elif line.startswith("SNAP "):
            tag = line.split(" ", 1)[1]
            time.sleep(0.25)
            client = main_client(proc.pid)
            if client is None:
                result["snaps"][tag] = {"error": "window not found in hyprctl clients"}
                continue
            mons = monitors()
            mon = next(m for m in mons.values() if m["id"] == client["monitor"])
            shot = Path("/tmp/archmage-canvas-matrix/screens") / f"{out.name}-{tag}.png"
            shot.parent.mkdir(parents=True, exist_ok=True)
            screen = capture(mon["name"], shot)
            snap = {"monitor": mon["name"], "scale": mon["scale"], "client_at": client["at"],
                    "client_size": client["size"], "screenshot": str(shot)}
            geo = geometry.get(tag) or (list(geometry.values())[-1] if geometry else None)
            if tag in pending_popup:
                probe = pending_popup.pop(tag)
                if probe.get("rect_logical"):
                    k = qt_to_device(client, mon, last_window_logical[0])
                    r = probe["rect_logical"]
                    rect = [(client["at"][0] - mon["x"]) * mon["scale"] + r[0] * k,
                            (client["at"][1] - mon["y"]) * mon["scale"] + r[1] * k, r[2] * k, r[3] * k]
                    s = k
                    color = (255, 0, 255) if probe["type"] == "popup" else (0, 255, 255)
                    snap["probe_color_fraction"] = color_fraction(screen, rect, color)
                    snap["probe_rect_screen"] = [round(v, 1) for v in rect]
                    snap["_probe_type"] = probe["type"]
                    snap["_probe_area"] = r[2] * r[3] * s * s
                snap["probe_visible_in_qt"] = probe.get("visible")
            elif geo is not None and geo["tag"] == tag:
                frame = np.asarray(Image.open(geo["frame"]).convert("RGBA"))
                k = qt_to_device(client, mon, geo.get("window_logical"))
                origin = canvas_origin(client, mon, geo["canvas_logical"], k)
                snap["qt_to_device"] = round(k, 4)
                snap["frame_compare"] = compare_frame(screen, frame, origin)
                snap["expected_origin"] = [round(origin[0], 2), round(origin[1], 2)]
                snap["frame_device"] = [frame.shape[1], frame.shape[0]]
                shutil.copy(geo["frame"], out / f"{tag}-frame.png")
            # Evidence keeps only the experiment window (plus any probe popup): full output captures show the
            # user's own bar, wallpaper and window titles and stay outside the repository.
            sc = mon["scale"]
            region = [(client["at"][0] - mon["x"]) * sc, (client["at"][1] - mon["y"]) * sc,
                      client["size"][0] * sc, client["size"][1] * sc]
            if "probe_rect_screen" in snap:
                pr = snap["probe_rect_screen"]
                x0, y0 = min(region[0], pr[0]), min(region[1], pr[1])
                x1 = max(region[0] + region[2], pr[0] + pr[2])
                y1 = max(region[1] + region[3], pr[1] + pr[3])
                region = [x0, y0, x1 - x0, y1 - y0]
            x0, y0 = max(0, int(region[0])), max(0, int(region[1]))
            x1 = min(screen.shape[1], int(region[0] + region[2]))
            y1 = min(screen.shape[0], int(region[1] + region[3]))
            crop = screen[y0:y1, x0:x1]
            if probe_type := snap.pop("_probe_type", None):
                # Probe-colored area inside the window, relative to the probe's expected device area: does not
                # depend on mapping popup coordinates, which differs under XWayland scaling.
                color = np.array((255, 0, 255) if probe_type == "popup" else (0, 255, 255))
                hits = int((np.abs(crop[:, :, :3].astype(np.int16) - color).max(axis=2) <= 40).sum())
                expected = snap.pop("_probe_area", 0)
                snap["probe_area_ratio"] = round(hits / expected, 3) if expected else None
            if y0 < TOP_CLEARANCE * sc:
                snap["screenshot"] = "withheld: the window region reaches the output's top bar"
            else:
                Image.fromarray(crop).save(out / f"{tag}-window.png")
                snap["screenshot"] = f"{tag}-window.png (cropped to the window; full capture not kept)"
            result["snaps"][tag] = snap
        elif line.startswith("MOVE "):
            target = line.split(" ", 1)[1]
            output = OUTPUT_S15 if target == "s15" else OUTPUT_S1
            client = main_client(proc.pid)
            if client:
                dispatch(f'hl.dsp.window.move({{ workspace = {desktop.ws[output]}, follow = false, '
                         f'window = "address:{client["address"]}" }})')
                time.sleep(0.4)
                center_main(proc.pid)
                desktop.restore_focus()
        elif line.startswith("LATENCY "):
            result["latency_lines"].append(line)
        elif "VULKAN-VALIDATION" in line or "Validation Error" in line or "VUID-" in line:
            result["validation_lines"].append(line[:500])
    if focus_steal is not None:
        desktop.restore_focus()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, default=Path("build/dev-release/apps/archmage/archmage"))
    parser.add_argument("--out", type=Path, default=Path("docs/decisions/evidence/S1-1"))
    parser.add_argument("--kinds", default="raster,rhi,vulkan")
    parser.add_argument("--platforms", default="wayland,xcb")
    parser.add_argument("--timeout", type=float, default=150.0)
    args = parser.parse_args()
    if not args.binary.exists():
        print(f"missing {args.binary}", file=sys.stderr)
        return 2
    summary = []
    with Desktop() as desktop:
        print(f"window rule: {desktop.rule_result}; workspaces: {desktop.ws}", flush=True)
        for platform in args.platforms.split(","):
            for kind in args.kinds.split(","):
                print(f"== {platform} / {kind}", flush=True)
                res = run_one(args.binary, platform, kind, args.out / f"{platform}-{kind}", desktop, args.timeout)
                compact = {tag: {k: v for k, v in snap.items() if k in ("frame_compare", "probe_color_fraction",
                                                                       "monitor", "scale", "error")}
                           for tag, snap in res["snaps"].items()}
                print(json.dumps({"exit": res["exit_code"], "snaps": compact,
                                  "validation": len(res["validation_lines"])}, indent=None), flush=True)
                summary.append(res)
    summary_path = args.out / "summary.json"
    merged = {(r["platform"], r["kind"]): r for r in (json.loads(summary_path.read_text()) if summary_path.exists() else [])}
    merged.update({(r["platform"], r["kind"]): r for r in summary})
    summary_path.write_text(json.dumps(list(merged.values()), indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
