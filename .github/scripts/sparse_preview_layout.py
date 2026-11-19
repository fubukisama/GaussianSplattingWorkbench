"""Exercise the public F7 sparse-preview path. Never run on a user's desktop."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
import winreg

import cv2
import numpy as np
from pywinauto import Desktop


def rect(control):
    r = control.rectangle()
    return [r.left, r.top, r.right, r.bottom]


def axes(image):
    """Measure the long red and green grid axes, not individual points or HUD pixels."""
    rgb = np.asarray(image.convert("RGB"))
    h, w = rgb.shape[:2]
    y, x = int(.19*h), int(.06*w)
    a = rgb[y:int(.84*h), x:int(.94*w)]
    r, g, b = a.astype(np.int16).transpose(2, 0, 1)
    out = {}
    for name, mask in (("x", (r-g > 23) & (r-b > 20) & (g > 53)),
                       ("y", (g-r > 12) & (g-b > 24) & (r > 55))):
        lines = cv2.HoughLinesP(mask.astype("uint8"), 1, np.pi/360, 30,
                                minLineLength=int(.17*w), maxLineGap=12)
        candidates = []
        for x1, y1, x2, y2 in ([] if lines is None else lines[:, 0]):
            dx, dy = float(x2-x1), float(y2-y1)
            if abs(dx) < .14*w or not .08 < abs(dy/dx) < 3:
                continue
            slope = dy/dx
            intercept = y+y1+slope*(.5*w-x-x1)
            candidates.append((dx*dx+dy*dy, round(intercept, 2), round(slope, 4)))
        if candidates:
            out[name] = list(max(candidates)[1:])
    return out


def match(items, *, ident=None, name=None, role=None):
    for c in items:
        i = c.element_info
        if ((not ident or i.automation_id.rsplit(".", 1)[-1] == ident)
                and (not name or i.name.replace("&", "") == name)
                and (not role or i.control_type == role) and c.is_visible()):
            return c
    return None


def wait(get, label, seconds=25):
    until = time.monotonic()+seconds
    while time.monotonic() < until:
        item = get()
        if item: return item
        time.sleep(.15)
    raise RuntimeError("Could not witness "+label)


def log_text(control):
    try:
        return control.iface_text.DocumentRange.GetText(-1)
    except Exception:
        return control.get_value()


def run(root, evidence):
    # All settings, input, rendering and child processes belong to this ephemeral runner.
    if os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted" or os.environ.get("GITHUB_ACTIONS") != "true":
        raise RuntimeError("Only a GitHub-hosted disposable Windows runner may execute this probe")
    project = root/"project"
    dataset = project/"datasets"/"s"
    (dataset/"input").mkdir(parents=True, exist_ok=False)
    views = sorted((root/"fixture"/"photos").glob("view_*.jpg"))
    if len(views) != 16: raise ValueError("Expected 16 synthetic views")
    for jpg in views: shutil.copyfile(jpg, dataset/"input"/jpg.name)
    document = project/"diagnostic.gsw.json"
    document.write_text(json.dumps(dict(schemaVersion=1, rootPath=".", projectName="Sparse diagnostic",
                                        datasetPath="datasets/s", scenePath="")), encoding="utf-8")
    cm = root/"cm"/"4.1.0"/"bin"/"colmap.exe"
    if not cm.is_file(): raise FileNotFoundError(cm)
    key = r"Software\GaussianSceneWorkbench\Gaussian Scene Workbench\reconstruction"
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key) as settings:
        for k, v in (("useGpu", 0), ("cameraModel", "PINHOLE"), ("colmapExecutable", str(cm))):
            winreg.SetValueEx(settings, k, 0, winreg.REG_DWORD if isinstance(v, int) else winreg.REG_SZ, v)
    backend = root/"app"
    exe = backend/"bin"/"Gaussian Scene Workbench.exe"
    env = dict(os.environ, GSW_BACKEND_ROOT=str(backend), GS_EDITOR_WORKSPACE_ROOT=str(root/"workspaces"),
               GAUSSIAN_SPLATTING_CONDA_PREFIX=str(Path(sys.executable).parent),
               GS_CONDA_PREFIX=str(Path(sys.executable).parent), COLMAP_EXE=str(cm), COLMAP_PATH=str(cm))
    user = ctypes.windll.user32
    user.GetForegroundWindow.restype = wintypes.HWND
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    clock = time.monotonic
    start = clock()
    def ts(): return round(clock()-start, 3)
    app = subprocess.Popen([str(exe), "--language", "en_US", "--theme", "light", "--project", str(document)],
                           cwd=backend, env=env)
    def foreground():
        h = user.GetForegroundWindow()
        pid = wintypes.DWORD()
        if not h: return "none"
        user.GetWindowThreadProcessId(h, ctypes.byref(pid))
        return "owned" if pid.value == app.pid else "other"
    stop = threading.Event()
    focus = evidence["foregroundTransitions"] = []
    def watch_focus():
        while not stop.wait(.025):
            category = foreground()
            if not focus or category != focus[-1][1]: focus.append([ts(), category])
    observer = threading.Thread(target=watch_focus, daemon=True)
    observer.start()
    try:
        desktop = Desktop(backend="uia")
        def windows(): return desktop.windows(process=app.pid, visible_only=True)
        main = wait(lambda: match(windows(), ident="mainWindow"), "owned main window")
        main.maximize()
        main.set_focus()
        log_tab = wait(lambda: match(main.descendants(), name="Log", role="TabItem"), "public Log tab")
        log_tab.select()
        console = wait(lambda: match(main.descendants(), ident="taskConsole"), "visible task console")
        stable = {}
        for label, ids in (("project", ("projectDock", "projectTree")), ("inspector", ("inspectorDock", "inspectorPanel")),
                           ("task", ("taskDock", "taskConsole")), ("view", ("nativeViewport",))):
            elements = main.descendants()
            for ident in ids:
                c = match(elements, ident=ident)
                if c:
                    stable[label] = c
                    evidence.setdefault("landmarkIds", {})[label] = ident
                    break
            else: raise RuntimeError("No accessible "+label+" landmark")
        def geometry():
            outer = wintypes.RECT()
            if not user.GetWindowRect(main.handle, ctypes.byref(outer)): raise ctypes.WinError()
            return {"outer": [outer.left, outer.top, outer.right, outer.bottom],
                    **{k: rect(c) for k, c in stable.items()}}
        def row(): return match(main.descendants(), ident="renderToolbar") is not None
        baseline = geometry()
        time.sleep(.65)
        if baseline != geometry(): raise RuntimeError("Baseline did not settle before the task")
        evidence.update(baseline=baseline, toolbarBefore=row())
        if foreground() != "owned": raise RuntimeError("Refusing input to a foreign foreground")
        main.type_keys("{F7}", set_foreground=False)
        dialog = wait(lambda: match(windows(), name="COLMAP Sparse Reconstruction"), "reconstruction dialog")
        go = wait(lambda: match(dialog.descendants(), name="Start Reconstruction", role="Button"), "start button")
        if go.element_info.process_id != app.pid or not go.is_enabled(): raise RuntimeError("Cannot start owned task")
        go.invoke()
        rows = evidence["samples"] = []
        rx = re.compile(r"COLMAP sparse point cloud updated: snapshot (\d+) [·.] ([\d,]+) points")
        highest, completed = 0, None
        until = clock()+210
        while clock() < until and app.poll() is None:
            g = geometry()
            raw = log_text(console)
            shots = rx.findall(raw or "")
            generation, count = (int(shots[-1][0]), int(shots[-1][1].replace(",", ""))) if shots else (0, 0)
            a = axes(stable["view"].capture_as_image()) if generation and foreground() == "owned" else {}
            rows.append(dict(t=ts(), generation=generation, points=count, axes=a, geometry=g))
            if generation != highest:
                print("Displayed sparse observation", generation, count, g["view"], a, flush=True)
                highest = generation
            if "[worker] COLMAP reconstruction failed:" in raw or "[worker] Fatal error:" in raw:
                raise RuntimeError("Synthetic worker failed: "+raw[-1200:])
            if "[worker] COLMAP reconstruction completed successfully." in raw:
                if completed is None: completed = clock()
                if clock()-completed > 1: break
            time.sleep(.12)
        if not completed:
            print("Owned synthetic app log:", log_text(console)[-1200:], flush=True)
            raise RuntimeError("Did not witness successful real CPU reconstruction")
        config, = (project/".gsw"/"jobs").glob("colmap-*.json")
        cfg = json.loads(config.read_text(encoding="utf-8-sig"))
        if (cfg["task"] != "colmap" or cfg["colmapOptions"]["use_gpu"] is not False
                or Path(cfg["colmapExecutable"]).resolve() != cm.resolve()): raise ValueError("Unexpected worker route")
        report = json.loads((dataset/"sparse"/"0"/"reconstruction_quality.json").read_text(encoding="utf-8"))
        if not report["usable"] or report["validPoints"] <= 0 or highest < 2:
            raise ValueError("Not enough real sparse reconstruction evidence")
        evidence["reconstruction"] = {k: report[k] for k in ("registeredImages", "inputImages", "validPoints")}
        evidence["toolbarAfter"] = row()
        # All samples, including pre-observation samples, are tested; checking a final position alone hides a bounce.
        changed = [s for s in rows if s["geometry"] != baseline]
        evidence["layoutStable"] = not changed
        if changed:
            g = changed[0]["geometry"]
            evidence["firstChange"] = {"time":changed[0]["t"], "snapshot":changed[0]["generation"],
                "topDeltaPx":{k:g[k][1]-baseline[k][1] for k in baseline}}
        # Use observations that have actually settled, excluding transitions while the next file is loading.
        # A stable projected world axis cannot move between consecutive sparse-only observations without navigation.
        settled = {}
        for l, r in zip(rows, rows[1:]):
            if (l["generation"] and l["generation"] == r["generation"] and l["geometry"] == r["geometry"]
                    and l["axes"] and r["axes"]):
                for key in l["axes"].keys() & r["axes"].keys():
                    y, slope = l["axes"][key]
                    y2, s2 = r["axes"][key]
                    if abs(y-y2)<2 and abs(slope-s2)<.012:
                        settled.setdefault(l["generation"], {})[key] = [y2, s2]
        axes_by_gen = evidence["settledAxes"] = settled
        spans = {}
        for k in ("x", "y"):
            ys = [axes_by_gen[n][k][0] for n in axes_by_gen if k in axes_by_gen[n]]
            if len(ys) >= 2: spans[k] = round(max(ys)-min(ys), 2)
        evidence["axisSpanPx"] = spans
        evidence["gridStable"] = max(spans.values()) <= 3 if spans else None
        print("Observed layout change:", evidence.get("firstChange"), "grid span:", spans, flush=True)
        if evidence["gridStable"] is None: return "INCONCLUSIVE_GRID" if not changed else "REFLOW_GRID_UNMEASURED"
        return "PASS" if not changed and evidence["gridStable"] else "REPRODUCED"
    finally:
        stop.set()
        observer.join(1)
        # Exact Popen handle only. This test has no authority over other processes.
        if app.poll() is None: app.terminate()
        app.wait(timeout=10)


if __name__ == "__main__":
    if os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted": raise SystemExit("Hosted runner only")
    root = Path(os.environ["SJ_ROOT"])
    data = {"nativeSource":os.environ["NATIVE_SOURCE"], "nativeRun":os.environ["NATIVE_RUN"],
            "route":"16 synthetic views, public F7, real CPU COLMAP; shared sparse display path, not a GPU training run"}
    try:
        data["result"] = run(root, data)
    except Exception as exc:
        data["result"] = "INCONCLUSIVE"
        data["error"] = str(exc)
        raise
    finally:
        (root/"evidence"/"layout.json").write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
    print(data["result"])
    sys.exit(0 if data["result"] == "PASS" else 1)
