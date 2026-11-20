"""Check recognizable form marks in real client pixels, not the icon asset.

The ROI comes from each visible widget's public Qt subcontrol geometry.
Coordinates and stroke thickness are not required to match a specific SVG.
"""
import argparse
from collections import Counter, deque
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image


def region(image, box, dpr, check):
    x, y, w, h = box
    if min(w, h) <= 0 or dpr <= 0:
        raise ValueError(f"invalid visible region: {box}, DPR {dpr}")
    rect = tuple(round(value * dpr) for value in (x, y, x + w, y + h))
    if rect[0] < 0 or rect[1] < 0 or rect[2] > image.width or rect[3] > image.height:
        raise ValueError(f"control is outside the captured client: {rect}")
    raw = np.asarray(image.crop(rect).convert("RGB"), dtype=np.int16)
    # Drop the outline; do not allow a button edge to masquerade as its mark.
    h, w = raw.shape[:2]
    mx = max(1, round(w * (0.14 if check else 0.11)))
    my = max(1, round(h * (0.14 if check else 0.12)))
    raw = raw[my:h-my, mx:w-mx]
    if not raw.size:
        raise ValueError("subcontrol interior has no pixels")
    # Use the actual panel/selection fill rather than a hardcoded theme.
    bg = np.array(Counter(map(tuple, raw.reshape(-1, 3))).most_common(1)[0][0])
    return np.max(np.abs(raw - bg), axis=2) >= 52


def largest_stroke(mask):
    pending = set(zip(*np.nonzero(mask)))
    biggest = []
    while pending:
        start = pending.pop()
        part, queue = [start], deque([start])
        while queue:
            y, x = queue.popleft()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    neighbour = (y + dy, x + dx)
                    if neighbour in pending:
                        pending.remove(neighbour)
                        part.append(neighbour)
                        queue.append(neighbour)
        if len(part) > len(biggest):
            biggest = part
    return biggest


def shape(mask, dpr, kind):
    stroke = largest_stroke(mask)
    result = {"inkPixels": int(mask.sum()), "largestStroke": len(stroke)}
    if len(stroke) < 4 * dpr * dpr:
        return False, result
    ys, xs = np.asarray(stroke).T
    width, height = int(np.ptp(xs)) + 1, int(np.ptp(ys)) + 1
    if width < 4 * dpr or height < 2 * dpr:
        return False, result | {"width": width, "height": height}
    # A down arrow has a low middle and two raised ends; the short arm of
    # a check meets its longer rising arm to the left of the centre.
    columns = sorted(set(xs))
    px = np.array([(x - min(columns)) / max(1, width - 1) for x in columns])
    py = np.array([np.mean(ys[xs == x]) for x in columns])
    centre = (px >= (0.21 if kind == "check" else 0.33)) & (px <= (0.51 if kind == "check" else 0.67))
    if not centre.any():
        return False, result
    left, middle, right = (float(np.mean(py[band])) for band in (px <= .2, centre, px >= .8))
    sign = -1 if kind == "up" else 1
    a, b = sign * (middle - left) / dpr, sign * (middle - right) / dpr
    # A box, blob or fallback character contains extra strokes or lacks
    # the two distinguishable changes in direction. Allow antialiased edges.
    single = np.mean([not np.any(np.diff(sorted(ys[xs == x])) > 1 + dpr) for x in columns])
    dominance = len(stroke) / max(1, int(mask.sum()))
    verdict = (a >= (.32 if kind == "check" else .55) and
               b >= (1.15 if kind == "check" else .55) and
               float(single) >= .78 and dominance >= .7)
    return bool(verdict), result | {"width": width, "height": height, "leftArm": round(a, 2),
                                    "rightArm": round(b, 2), "singleStrokeColumns": round(float(single), 3)}


def compare(root, commit):
    cases, errors = [], []
    for factor in ("1", "1.25", "1.5", "1.75", "2"):
        for theme in ("light", "dark"):
            for lang in ("zh_CN", "en_US", "ja_JP"):
                name = f"{factor}-{theme}/{lang}"
                case = {"profile": name, "checks": {}}
                cases.append(case)
                try:
                    loc = root / name
                    f = json.loads((loc / "facts.json").read_text(encoding="utf-8"))
                    dpr = float(f["dpr"])
                    if (f["source"] != commit or f["theme"] != theme or f["locale"] != lang or
                            not math.isclose(dpr, float(factor), abs_tol=.01)):
                        raise ValueError("wrong source, locale, theme or measured DPR")
                    checks = case["checks"]
                    with Image.open(loc / "grid-on.png") as on, Image.open(loc / "grid-off.png") as off:
                        if on.size != off.size or any(abs(a - b*dpr) > 1 for a, b in zip(on.size, f["size"])):
                            raise ValueError("not the native client pixel extent")
                        case["marks"] = {}
                        for key, kind in (("grid", "check"), ("axes", "check"), ("mode", "down"),
                                          ("subdivisions", "down"), ("unit", "down"), ("up", "up"), ("down", "down")):
                            checks[key], case["marks"][key] = shape(region(on, f["regions"][key], dpr, kind=="check"), dpr, kind)
                        unmarked = region(off, f["regions"]["grid"], dpr, True)
                        checks["uncheckedInterior"] = int(unmarked.sum()) <= max(2*dpr*dpr, .06*unmarked.size)
                    expected_keys = {"spaceOff", "spaceOn", "tabToAxes", "tabToMode", "openPopup",
                                     "escapePopup", "endFixed", "homeAuto", "stepUp", "stepDown"}
                    if set(f["keyboard"]) != expected_keys:
                        raise ValueError("missing keyboard evidence")
                    checks.update({k: v is True for k, v in f["keyboard"].items()})
                    checks["cancelPreservedState"] = f["cancelPreservedState"] is True
                    checks["detailsTextIntact"] = f["detailsTextIntact"] is True
                    body, details, log = (f[k] for k in ("bodyFont", "detailsFont", "rawLogFont"))
                    checks["detailsUseBodyFont"] = (
                        details["family"].casefold() == body["family"].casefold() and
                        abs(details["pt"]-body["pt"]) <= .1 and
                        all(abs(details[k]-body[k]) <= 1 for k in ("narrowLatin", "wideLatin")))
                    checks["rawLogMonospaced"] = abs(log["narrowLatin"]-log["wideLatin"]) <= 1
                    case["failed"] = [k for k, ok in checks.items() if not ok]
                    if case["failed"]:
                        errors.append(f"{name}: {', '.join(case['failed'])}")
                except (OSError, ValueError, TypeError, KeyError) as e:
                    errors.append(f"{name}: invalid or missing capture: {e}")
    return {"schema": "gsw.grid_control_ink/1", "source": commit, "profiles": cases, "failures": errors,
            "osMixedDpi": "NOT_RUN", "nvidia": "NOT_RUN", "status": "FAIL" if errors else "PASS"}


if __name__ == "__main__":
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--root", type=Path, required=True)
    args.add_argument("--source", required=True)
    flags = args.parse_args()
    report = compare(flags.root, flags.source)
    with (flags.root / "grid-controls.json").open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    for msg in report["failures"]:
        print(msg)
    print(f"GRID_CONTROL_INK {report['status']}: {len(report['profiles'])} profiles; "
          f"{len(report['failures'])} failed; hosted Windows/controlled Qt DPR only.")
    sys.exit(1 if report["failures"] else 0)
