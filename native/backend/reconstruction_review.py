"""Bounded, read-only image review. Admission remains reconstruction_quality's job.

No compute/worker/CUDA import and no writes. Relative names, not basenames, bind
views. A report describes one inspected group; it never selects a training group.
"""
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import stat
import struct
import unicodedata

from native.backend import reconstruction_quality as quality

MAX_FILES = 20000
MAX_MODEL_BYTES = 256 * 1024 * 1024
MAX_HASH_BYTES = 2 * 1024 * 1024 * 1024
MAX_POINTS = 12000


def relative_name(value):
    if not isinstance(value, str) or not value or len(value) > 4096:
        raise ValueError("unsafe_relative_path")
    if "\\" in value or ":" in value or "\0" in value:
        raise ValueError("unsafe_relative_path")
    path = PurePosixPath(value)
    if path.is_absolute() or any(p in ("", ".", "..") or p.endswith((".", " ")) for p in value.split("/")):
        raise ValueError("unsafe_relative_path")
    return path.as_posix()


def _require_contained_path(resolved_path, resolved_root):
    # Some supported training environments still run Python 3.8, before
    # Path.is_relative_to(). Keep the same component-wise containment check.
    try:
        resolved_path.relative_to(resolved_root)
    except ValueError as exc:
        raise ValueError("unsafe_relative_path") from exc


def safe_path(root, name):
    name = relative_name(name)
    current = root
    for part in name.split("/"):
        current = current / part
        try:
            info = current.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("linked_path_refused")
    _require_contained_path(current.resolve(), root.resolve())
    return current


def folded(name):
    return unicodedata.normalize("NFC", name).casefold()


class Inspection:
    def __init__(self, root, cancelled=None):
        self.root, self.cancelled = root, cancelled
        self.files, self.directories, self.budget, self.entries = {}, {}, 0, 0

    def check(self):
        if self.cancelled and self.cancelled():
            raise InterruptedError("cancelled")

    def capture(self, path, maximum=MAX_MODEL_BYTES):
        self.check()
        name = path.relative_to(self.root).as_posix()
        path = safe_path(self.root, name)
        if not path.is_file():
            self.files[name] = {"path": name, "missing": True}
            return None
        before = path.stat()
        if before.st_size > maximum or self.budget + before.st_size > MAX_HASH_BYTES:
            raise ValueError("inspection_budget_exceeded")
        self.budget += before.st_size
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                self.check()
                digest.update(block)
        after = path.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise ValueError("source_changed")
        self.files[name] = dict(path=name, size=after.st_size, modifiedNs=str(after.st_mtime_ns),
                                sha256=digest.hexdigest())
        return path

    def images(self, folder):
        self.check()
        path = safe_path(self.root, folder)
        if not path.is_dir():
            self.files[folder] = dict(path=folder, missing=True)
            return None, []
        found, issues, pending = {}, [], [path]
        while pending:
            self.check()
            directory = pending.pop()
            relative = directory.relative_to(self.root).as_posix()
            self.directories[relative] = str(directory.stat().st_mtime_ns)
            # Streaming enumeration is bounded before sorting, not after a huge
            # directory has already been materialized in memory.
            entries = []
            with os.scandir(directory) as stream:
                for entry in stream:
                    self.check()
                    if len(entries) >= MAX_FILES * 2:
                        raise ValueError("inspection_budget_exceeded")
                    entries.append(Path(entry.path))
            for item in sorted(entries, key=lambda p: p.name):
                self.check()
                self.entries += 1
                name = item.relative_to(path).as_posix()
                if self.entries > MAX_FILES * 2:
                    raise ValueError("inspection_budget_exceeded")
                try:
                    safe_path(self.root, item.relative_to(self.root).as_posix())
                    if item.is_dir():
                        pending.append(item)
                    elif item.suffix.lower() in quality.IMAGE_EXTENSIONS:
                        self.capture(item, MAX_HASH_BYTES)
                        found[relative_name(name)] = item.relative_to(self.root).as_posix()
                except InterruptedError:
                    raise
                except (OSError, ValueError) as exc:
                    issues.append(dict(name=name, code=str(exc)))
        return found, issues

    def json(self, path):
        actual = self.capture(path, 16 * 1024 * 1024)
        return json.loads(actual.read_text(encoding="utf-8-sig")) if actual else None


def _read_model(inspection, model, *, include_points=True):
    """Parse recorded calibration/poses; reuse existing format/pose validators.

    Sparse stored ERROR is a group statistic, not a per-image reprojection error.
    Points here are only a bounded display subset, not new admission evidence.
    """
    from native.worker.training_reconstruction_summary import _CAMERA_MODELS, _read
    from native.backend.camera_dataset import _quaternion_pose, _colmap_calibration
    extension = "bin" if (model / "images.bin").exists() else "txt"
    for name in ("cameras", "images", "points3D") if include_points else ("cameras", "images"):
        inspection.capture(model / (name + "." + extension))
    cameras, records, points = {}, [], []
    if extension == "bin":
        with (model / "cameras.bin").open("rb") as stream:
            count, = _read(stream, "<Q")
            if count > MAX_FILES:
                raise ValueError("inspection_budget_exceeded")
            for _ in range(count):
                cid, mid, width, height = _read(stream, "<iiQQ")
                name, params = _CAMERA_MODELS[mid]
                if cid in cameras:
                    raise ValueError("duplicate_camera")
                cameras[cid] = (name, width, height, _read(stream, "<" + "d" * params))
            if stream.read(1):
                raise ValueError("invalid_model")
        with (model / "images.bin").open("rb") as stream:
            count, = _read(stream, "<Q")
            if count > MAX_FILES:
                raise ValueError("inspection_budget_exceeded")
            for _ in range(count):
                value = _read(stream, "<idddddddi")
                name = bytearray()
                while True:
                    char = stream.read(1)
                    if not char or len(name) > 4096:
                        raise ValueError("invalid_model")
                    if char == b"\0":
                        break
                    name.extend(char)
                observations, = _read(stream, "<Q")
                if stream.tell() + observations * 24 > (model / "images.bin").stat().st_size:
                    raise ValueError("invalid_model")
                stream.seek(observations * 24, 1)
                records.append((value[0], value[1:5], value[5:8], value[-1], name.decode("utf-8")))
            if stream.read(1):
                raise ValueError("invalid_model")
        if include_points and (model / "points3D.bin").is_file():
            with (model / "points3D.bin").open("rb") as stream:
                count, = _read(stream, "<Q")
                if count > MAX_MODEL_BYTES // 51:
                    raise ValueError("inspection_budget_exceeded")
                for index in range(count):
                    inspection.check()
                    p = _read(stream, "<QdddBBBdQ")
                    if stream.tell() + p[-1] * 8 > (model / "points3D.bin").stat().st_size:
                        raise ValueError("invalid_model")
                    stream.seek(p[-1] * 8, 1)
                    if index % max(1, math.ceil(count / MAX_POINTS)) == 0 and all(math.isfinite(x) for x in p[1:4]):
                        points.append(list(p[1:4]))
                if stream.read(1):
                    raise ValueError("invalid_model")
    else:
        with (model / "cameras.txt").open(encoding="utf-8-sig") as stream:
            for line in stream:
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                f = line.split()
                cid = int(f[0])
                if cid in cameras or len(cameras) >= MAX_FILES:
                    raise ValueError("duplicate_camera")
                if len(f) != 4 + dict(_CAMERA_MODELS.values())[f[1]]:
                    raise ValueError("invalid_model")
                cameras[cid] = (f[1], int(f[2]), int(f[3]), [float(x) for x in f[4:]])
        with (model / "images.txt").open(encoding="utf-8-sig") as stream:
            for line in stream:
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                f = line.strip().split(maxsplit=9)
                if len(f) != 10 or len(records) >= MAX_FILES or next(stream, None) is None:
                    raise ValueError("invalid_model")
                records.append((int(f[0]), [float(x) for x in f[1:5]], [float(x) for x in f[5:8]], int(f[8]), f[9]))
        if include_points and (model / "points3D.txt").is_file():
            with (model / "points3D.txt").open(encoding="utf-8-sig") as stream:
                for line in stream:
                    inspection.check()
                    if not line.strip() or line.lstrip().startswith("#"):
                        continue
                    f = line.split()
                    xyz = [float(x) for x in f[1:4]]
                    if len(points) < MAX_POINTS and len(xyz) == 3 and all(math.isfinite(x) for x in xyz):
                        points.append(xyz)
    rows, ids = [], set()
    for identifier, q, t, cid, name in records:
        if identifier in ids or cid not in cameras:
            raise ValueError("invalid_model")
        ids.add(identifier)
        pose = _quaternion_pose(q, t)
        row = dict(name=name, imageId=str(identifier), cameraId=str(cid), pose=None, poseReason="unsupported_projection")
        model_name, width, height, params = cameras[cid]
        if width <= 0 or height <= 0 or not all(math.isfinite(x) for x in params):
            raise ValueError("invalid_model")
        if model_name in ("PINHOLE", "SIMPLE_PINHOLE"):
            width, height, k = _colmap_calibration(model_name, width, height, params)
            row.update(pose=dict(view_id="colmap:" + str(identifier), image_path=name,
                                width=width, height=height, K=k, c2w_opencv=pose,
                                split="train", projection_model=model_name, pixel_coordinates="pixel_border"), poseReason=None)
        rows.append(row)
    return rows, points, len(cameras), extension


def _review_root(root):
    for item in (root, *root.parents):
        if item.is_symlink() or (item.exists() and getattr(item.lstat(), "st_file_attributes", 0) & 0x400):
            raise ValueError("linked_path_refused")
    return root.resolve()


def _finish_review(report, inspection):
    # The bytes are captured once, not reopened on the GUI thread. Refuse a
    # concurrent replacement during inspection; each viewer read checks its
    # original's captured digest independently before decoding.
    for item in inspection.files.values():
        if "modifiedNs" not in item:
            continue
        current = safe_path(inspection.root, item["path"]).stat()
        if current.st_size != item["size"] or str(current.st_mtime_ns) != item["modifiedNs"]:
            raise ValueError("source_changed")
    report["snapshots"] = list(inspection.files.values())
    report["directories"] = inspection.directories
    identity = dict(dataset=str(inspection.root), group=report.get("selectedGroup"), files=report["snapshots"],
                    directories=report["directories"])
    report["identity"] = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    return report


def review_training_originals(dataset, expected_identity=None, cancelled=None):
    """Verify only captured originals in the admitted training dataset.

    An old COLMAP workflow can reconstruct input/ by copying images/. Neither
    directory alone is proof of an original. Use the importer record and the
    exact full COLMAP name, or the explicit unchanged fixed-camera import.
    """
    if not str(dataset).strip():
        raise ValueError("dataset_unavailable")
    root = _review_root(Path(dataset).absolute())
    inspection = Inspection(root, cancelled)
    locked = bool(expected_identity)
    group = "locked" if locked else "sparse/0"
    report = dict(schema="gsw.reconstruction_review/1", version=1, ready=True, dataset=str(root),
                  sourceKind="locked" if locked else "colmap", selectedGroup=group,
                  frame="canonical" if locked else "source", photoScope="task_originals", rows=[])
    manifest = inspection.json(root / "source/metadata_manifest.json")
    records = manifest.get("files", []) if isinstance(manifest, dict) else []
    if not isinstance(records, list) or len(records) > MAX_FILES:
        raise ValueError("invalid_import_manifest")

    def original_snapshot(path, record):
        try:
            current = inspection.capture(safe_path(root, path))
        except (FileNotFoundError, PermissionError):
            return None
        if current is None:
            return None
        captured = inspection.files[path]
        if "byteSize" in record and record["byteSize"] != captured["size"]:
            return None
        if "sha256" in record and record["sha256"] != captured["sha256"]:
            return None
        return captured

    if locked:
        from native.backend.camera_dataset import load_camera_dataset
        captured_manifest = inspection.capture(safe_path(root, "camera_dataset.json"), 16 * 1024 * 1024)
        if captured_manifest is None:
            return _finish_review(report, inspection)
        data = load_camera_dataset(captured_manifest, cancelled=cancelled)
        if data["data_identity"] != expected_identity:
            raise ValueError("source_changed")
        report["dataIdentity"] = expected_identity
        if not (isinstance(manifest, dict) and manifest.get("kind") == "fixed_camera_dataset" and
                manifest.get("posePolicy") == "locked" and manifest.get("dataIdentity") == expected_identity and
                manifest.get("normalizationAppliedToImages") is False):
            return _finish_review(report, inspection)
        ids = {}
        for record in records:
            if isinstance(record, dict) and isinstance(record.get("viewId"), str):
                view_id = record["viewId"]
                ids[view_id] = None if view_id in ids else record
        for view in data["views"]:
            record, path = ids.get(view["view_id"]), relative_name(view["image_path"])
            if (record and record.get("kind") == "image" and record.get("archiveMode") == "copy" and
                    record.get("sourceUnchanged") is True and record.get("relativePath") == path and
                    record.get("trainingPath") == path and record.get("sha256") == view["sha256"] and
                    original_snapshot(path, record) is not None):
                report["rows"].append(dict(id=view["view_id"], name=path, imagePath=path, pose=view))
    elif records:
        model = safe_path(root, group)
        rows, _, _, _ = _read_model(inspection, model, include_points=False)
        # A collision in either the manifest or the COLMAP index disables that
        # name, including case/normalization variants on Windows.
        declared, archive_uses, camera_uses = {}, {}, {}
        for record in records:
            if not isinstance(record, dict) or record.get("kind") != "image":
                continue
            try:
                target = relative_name(record.get("trainingPath"))
            except ValueError:
                continue
            folder, separator, name = target.partition("/")
            if not separator or folded(folder) != "images":
                continue
            # A noncanonical but case-equivalent target is a competing
            # declaration on Windows; it must not let a prior link survive.
            key = folded(name)
            duplicate = key in declared
            declared[key] = None
            try:
                archive = relative_name(record.get("archivePath"))
            except ValueError:
                continue
            if (not archive.startswith("source/originals/") or
                    Path(archive).suffix.lower() not in quality.IMAGE_EXTENSIONS):
                continue
            archive_key = folded(archive)
            archive_uses[archive_key] = archive_uses.get(archive_key, 0) + 1
            if not duplicate and folder == "images":
                declared[key] = name, archive, record
        for row in rows:
            key = folded(row["name"])
            camera_uses[key] = camera_uses.get(key, 0) + 1
        for row in rows:
            key = folded(row["name"])
            match = declared.get(key)
            if (match is None or camera_uses[key] != 1 or row["name"] != match[0] or
                    archive_uses[folded(match[1])] != 1 or not row["pose"]):
                continue
            name, path, record = match
            if original_snapshot(path, record) is not None:
                report["rows"].append(dict(id=hashlib.sha256(name.encode()).hexdigest(),
                                           name=name, imagePath=path, pose=row["pose"]))
    return _finish_review(report, inspection)


def review_dataset(dataset, group=None, camera_dataset=None, expected_identity=None, cancelled=None):
    if not str(dataset).strip():
        raise ValueError("dataset_unavailable")
    root = Path(dataset).absolute()
    if root.is_file():
        camera_dataset, root = str(root), root.parent
    # Refuse linked dataset ancestors as well as linked children.
    root = _review_root(root)
    inspection = Inspection(root, cancelled)
    inspection.check()
    report = dict(schema="gsw.reconstruction_review/1", version=1, ready=True, dataset=str(root),
                  originalSource="input", originalAvailable=False, trainingStatus="not_prepared",
                  originalCount=None, registeredCount=None, unregisteredCount=None, trainingCount=None,
                  groups=[], rows=[], issues=[], points=[], sourceKind="none", selectionReason="current_training_group",
                  perImageErrorStatus="unavailable", registrationApplicable=True)
    canonical = Path(camera_dataset) if camera_dataset else root / "camera_dataset.json"
    transforms = (root / "transforms_train.json").exists()
    if canonical.exists() or transforms:
        from native.backend.camera_dataset import load_camera_dataset, camera_dataset_root
        source = canonical if canonical.exists() else root
        _require_contained_path(source.resolve(), root)
        data = load_camera_dataset(source, cancelled=cancelled)
        if expected_identity and data["data_identity"] != expected_identity:
            raise ValueError("source_changed")
        if camera_dataset_root(source).resolve() != root:
            raise ValueError("unsafe_relative_path")
        for name in ("camera_dataset.json", "transforms_train.json", "transforms_val.json", "transforms_test.json"):
            inspection.capture(root / name, 16 * 1024 * 1024)
        report.update(sourceKind="locked", originalSource=str(source), originalAvailable=True,
                      originalCount=len(data["views"]), registrationApplicable=False,
                      trainingStatus="verified", trainingCount=len(data["splits"]["train"]),
                      selectedGroup="locked", actualTrainingGroup="locked",
                      groups=[dict(id="locked", registeredImages=None, sparsePoints=None, meanReprojectionError=None,
                                   usable=True, reasons=[], cameraCount=len(data["views"]))])
        for view in data["views"]:
            path = inspection.capture(safe_path(root, view["image_path"]), MAX_HASH_BYTES)
            row = dict(id=view["view_id"], name=view["image_path"], imagePath=view["image_path"],
                       status="posed", split=view["split"], original=True, registered=None,
                       training=view["split"] == "train", pose=view, poseReason=None, perImageError=None)
            if path is None:
                row.update(status="missing", pose=None, poseReason="missing_image")
            report["rows"].append(row)
        report["frame"] = "canonical"
    else:
        originals, issues = inspection.images("input")
        report["issues"].extend(issues)
        complete = originals is not None and not issues
        expected = 0
        try:
            metadata = inspection.json(root / "source/metadata_manifest.json")
            records = metadata.get("files", []) if isinstance(metadata, dict) else []
            expected = sum(1 if r["kind"] == "image" else int(r["extractedFrames"]) for r in records)
            if originals is not None and records and expected != len(originals):
                complete = False
                report["issues"].append(dict(code="input_manifest_count_mismatch", expected=expected, enumerated=len(originals)))
            if originals is None and records and all(r.get("kind") == "image" for r in records):
                declared = {}
                for r in records:
                    target = relative_name(r["trainingPath"])
                    archive = relative_name(r["archivePath"])
                    if not target.startswith("images/") or not archive.startswith("source/originals/"):
                        raise ValueError("invalid_import_manifest")
                    name = target[len("images/"):]
                    if name in declared:
                        raise ValueError("ambiguous_image_identity")
                    inspection.capture(safe_path(root, archive), MAX_HASH_BYTES)
                    declared[name] = archive
                originals, complete = declared, True
                report["originalSource"] = "source/metadata_manifest.json"
        except InterruptedError:
            raise
        except (OSError, ValueError, KeyError, TypeError) as exc:
            report["issues"].append(dict(code="original_manifest_unverifiable", diagnostic=str(exc)))
        report.update(originalAvailable=complete, originalCount=len(originals) if complete else None)
        originals = originals or {}
        training, train_issues = inspection.images("images")
        report["issues"].extend(train_issues)
        candidates = []
        for folder in ("sparse", "distorted/sparse", ".alignment_cache/sparse"):
            parent = safe_path(root, folder)
            inspection.files[folder] = dict(path=folder, missing=True) if not parent.exists() else dict(path=folder, directory=True)
            if not parent.is_dir():
                continue
            inspection.directories[folder] = str(parent.stat().st_mtime_ns)
            paths = [parent] if any((parent / ("images." + ext)).exists() for ext in ("bin", "txt")) else sorted(parent.iterdir())
            for path in paths:
                if len(candidates) >= 128:
                    raise ValueError("inspection_budget_exceeded")
                if path.is_dir():
                    safe_path(root, path.relative_to(root).as_posix())
                    candidates.append(path)
        actual = next((p for p in candidates if p.relative_to(root).as_posix() == "sparse/0"), None)
        selected = group or (actual.relative_to(root).as_posix() if actual else candidates[0].relative_to(root).as_posix() if candidates else "")
        if group and group not in [p.relative_to(root).as_posix() for p in candidates]:
            raise ValueError("unknown_group")
        records_by_group, point_sets = {}, {}
        for model in candidates:
            gid = model.relative_to(root).as_posix()
            item = dict(id=gid, registeredImages=None, sparsePoints=None, meanReprojectionError=None, usable=False,
                        reasons=["invalid_model"], cameraCount=None)
            try:
                rows, points, count, extension = _read_model(inspection, model)
                admission = quality.assess_model(model, max(len(originals), expected))
                item.update(registeredImages=len(rows), sparsePoints=admission["sparsePoints"],
                            meanReprojectionError=admission["meanReprojectionError"] if admission["validPoints"] else None,
                            usable=admission["usable"], reasons=admission["reasons"], cameraCount=count, format=extension)
                records_by_group[gid], point_sets[gid] = rows, points
            except InterruptedError:
                raise
            except (OSError, ValueError, KeyError, IndexError, struct.error) as exc:
                item["diagnostic"] = str(exc)
            report["groups"].append(item)
        report.update(sourceKind="colmap" if candidates else "none", selectedGroup=selected,
                      actualTrainingGroup=actual.relative_to(root).as_posix() if actual else None, frame="source")
        current = records_by_group.get(selected)
        train_names = None
        if actual and actual.relative_to(root).as_posix() in records_by_group and training is not None and not train_issues:
            actual_rows = records_by_group[actual.relative_to(root).as_posix()]
            names = [r["name"] for r in actual_rows]
            if (len({folded(n) for n in names}) == len(names) and all(n in training for n in names)
                    and all(r["pose"] for r in actual_rows)):
                train_names = set(names)
                report.update(trainingStatus="verified_colmap_loader", trainingCount=len(names))
        selected_rows = current or []
        buckets = {}
        for name in [*originals, *(r["name"] for r in selected_rows)]:
            buckets.setdefault(folded(name), set()).add(name)
        by_name = {}
        for record in selected_rows:
            by_name.setdefault(record["name"], []).append(record)
        all_names = sorted(set(originals) | set(by_name))
        for name in all_names:
            registered = name in by_name if current is not None else None
            row = dict(id=hashlib.sha256(name.encode()).hexdigest(), name=name, imagePath=originals.get(name),
                       original=name in originals, registered=registered,
                       training=(name in train_names) if train_names is not None else None,
                       split="train" if train_names is not None and name in train_names else None,
                       status="registered" if registered else "unregistered" if registered is False else "unknown",
                       pose=None, poseReason="unregistered" if registered is False else "invalid_model", perImageError=None)
            try:
                relative_name(name)
                if len(buckets[folded(name)]) > 1 or len(by_name.get(name, [])) > 1:
                    raise ValueError("ambiguous_image_identity")
                if not row["imagePath"]:
                    row.update(status="missing", poseReason="original_unavailable")
                elif inspection.files.get(row["imagePath"], {}).get("missing"):
                    row.update(status="missing", poseReason="missing_image", imagePath=None)
                if registered:
                    record = by_name[name][0]
                    row.update(imageId=record["imageId"], cameraId=record["cameraId"])
                    if row["imagePath"]:
                        row.update(pose=record["pose"], poseReason=record["poseReason"])
            except ValueError as exc:
                row.update(status="conflict", registered=None, training=None,
                           pose=None, poseReason=str(exc), imagePath=None)
            report["rows"].append(row)
        report["points"] = point_sets.get(selected, [])
        report["registeredCount"] = len(current) if current is not None else None
        if report["originalAvailable"] and current is not None:
            report["unregisteredCount"] = sum(r["original"] and r["registered"] is False for r in report["rows"])
    return _finish_review(report, inspection)


def review_model(model_directory, group=None, camera_dataset=None, cancelled=None):
    """Review the selected mesh input's declared source, never the active project
    by coincidence. Reuse the mesh reader's literal-only cfg_args contract.
    """
    from native.worker.mesh_generation import _validate_configuration
    root = Path(model_directory).absolute()
    for entry in (root, *root.parents):
        if entry.is_symlink() or (entry.exists() and getattr(entry.lstat(), "st_file_attributes", 0) & 0x400):
            raise ValueError("linked_path_refused")
    root = root.resolve()
    inspection = Inspection(root, cancelled)
    cfg = inspection.capture(root / "cfg_args", 256 * 1024)
    if cfg is None:
        raise ValueError("model_source_unverifiable")
    values = _validate_configuration(cfg)
    source = values.get("source_path")
    if not isinstance(source, str) or not source.strip() or not Path(source).is_absolute():
        raise ValueError("model_source_unverifiable")
    contract = inspection.json(root / "training-camera-contract.json")
    expected = None
    if contract is not None:
        if contract.get("schema") != "gsw.training_camera_contract/1" or not camera_dataset:
            raise ValueError("original_camera_dataset_required")
        expected = contract.get("input_data_identity")
        if not isinstance(expected, str) or len(expected) != 64:
            raise ValueError("model_source_unverifiable")
        source = str(Path(camera_dataset).absolute().parent)
    elif camera_dataset:
        raise ValueError("model_camera_binding_unverifiable")
    report = review_dataset(source, group, camera_dataset, expected, cancelled)
    # A legacy model may have an evaluation split or a non-default image root.
    # Without an execution-bound training manifest, do not label every registered
    # view as a sampled training view or guess a backend's held-out policy.
    if contract is None and (values.get("eval", False) is not False or
                             values.get("images", "images") not in (None, "images")):
        report.update(trainingStatus="unverifiable_training_policy", trainingCount=None)
        for row in report["rows"]:
            row.update(training=None, split=None)
    before = {name: dict(item) for name, item in inspection.files.items()}
    for name in before:
        inspection.capture(safe_path(root, name))
    if inspection.files != before:
        raise ValueError("source_changed")
    report["modelSource"] = dict(root=str(root), files=list(inspection.files.values()))
    report["identity"] = hashlib.sha256(json.dumps(dict(datasetIdentity=report["identity"],
        modelSource=report["modelSource"]), sort_keys=True).encode()).hexdigest()
    return report
