"""Owned inputs only. Assertions are independent of review's reported counters."""
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from native.backend.reconstruction_review import review_dataset, review_model


def temporary_directory():
    # CI supplies its build-local root. Standalone runs stay in the checkout's
    # existing fixture area rather than falling back to the OS system TEMP.
    root = Path(os.environ.get("GSW_RECONSTRUCTION_REVIEW_ROOT") or
                Path(__file__).resolve().parents[2] / ".tmp" / "reconstruction-review-fixtures")
    root.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(dir=root)


def model(root, names, binary=False, roll=False):
    root.mkdir(parents=True)
    if binary:
        (root / "cameras.bin").write_bytes(struct.pack("<QiiQQdddd", 1, 91, 1, 64, 48, 50, 55, 32, 24))
        with (root / "images.bin").open("wb") as stream:
            stream.write(struct.pack("<Q", len(names)))
            for i, name in enumerate(names):
                q = (0.9238795325112867, 0, 0, 0.3826834323650898) if roll else (1, 0, 0, 0)
                stream.write(struct.pack("<idddddddi", i * 7 + 3, *q, i * .2, 0, 0, 91))
                stream.write(name.encode() + b"\0" + struct.pack("<Q", 0))
        with (root / "points3D.bin").open("wb") as stream:
            stream.write(struct.pack("<Q", 1))
            stream.write(struct.pack("<QdddBBBdQiiii", 77, .1, .2, 3., 20, 30, 40, 0., 2, 3, 0, 10, 0))
    else:
        (root / "cameras.txt").write_text("91 PINHOLE 64 48 50 55 32 24\n")
        (root / "images.txt").write_text("".join(
            f"{i * 7 + 3} 1 0 0 0 {i * .2} 0 0 91 {name}\n\n" for i, name in enumerate(names)), encoding="utf-8")
        (root / "points3D.txt").write_text("77 .1 .2 3 20 30 40 0 3 0 10 0\n")


def files(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob("*") if p.is_file()}


def fixture(root, binary=False):
    names = [f"写真/frame{i:02d}.png" for i in range(9)]
    for name in names:
        p = root / "input" / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b"original " + name.encode())
    for name in names[:2]:
        p = root / "images" / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b"undistorted subset")
    model(root / "sparse/0", names[:2], binary, roll=binary)
    model(root / "distorted/sparse/1", names[:5])
    return names


class ReconstructionReviewTests(unittest.TestCase):
    def test_nine_originals_two_registered_are_separate_from_training_subset_and_other_group(self):
        for binary in (False, True):
            with self.subTest(binary=binary), temporary_directory() as temp:
                root = Path(temp)
                names = fixture(root, binary)
                before = files(root)
                with patch("subprocess.Popen", side_effect=AssertionError("no training/COLMAP/mesh process")):
                    result = review_dataset(root)
                    other = review_dataset(root, "distorted/sparse/1")
                self.assertEqual(files(root), before)
                self.assertEqual(result["originalCount"], 9)
                self.assertEqual(result["registeredCount"], 2)
                self.assertEqual(result["trainingCount"], 2)
                self.assertEqual([r["name"] for r in result["rows"] if r["status"] == "unregistered"], names[2:])
                self.assertEqual(result["unregisteredCount"], 7)
                self.assertTrue(all(r["pose"] is None for r in result["rows"][2:]))
                self.assertEqual(other["registeredCount"], 5)
                self.assertEqual(other["trainingCount"], 2)
                self.assertEqual(other["actualTrainingGroup"], "sparse/0")
                self.assertEqual(len(other["rows"]), 9)
                self.assertNotEqual(result["identity"], other["identity"])
                self.assertFalse(result["groups"][0]["usable"])
                self.assertIsNone(result["rows"][0]["perImageError"])
                self.assertEqual(result["groups"][0]["meanReprojectionError"], 0.)
                self.assertEqual(result["groups"][0]["cameraCount"], 1)
                self.assertEqual(result["rows"][0]["pose"]["K"], [[50, 0, 32], [0, 55, 24], [0, 0, 1]])

    def test_missing_original_list_does_not_invent_seven_unregistered_rows(self):
        with temporary_directory() as temp:
            root = Path(temp)
            model(root / "sparse/0", ["a.png", "b.png"])
            (root / "images").mkdir()
            for name in ("a.png", "b.png"):
                (root / "images" / name).write_bytes(b"subset")
            report = review_dataset(root)
            self.assertIsNone(report["originalCount"])
            self.assertIsNone(report["unregisteredCount"])
            self.assertEqual(len(report["rows"]), 2)
            self.assertTrue(all(r["status"] == "missing" and r["pose"] is None for r in report["rows"]))

    def test_pre_reconstruction_and_corrupt_model_are_distinct_unknowns(self):
        with temporary_directory() as temp:
            root = Path(temp)
            (root / "input").mkdir()
            (root / "input/a.png").write_bytes(b"raw")
            before = review_dataset(root)
            self.assertEqual(before["sourceKind"], "none")
            self.assertEqual(before["rows"][0]["status"], "unknown")
            self.assertIsNone(before["trainingCount"])
            model(root / "sparse/0", ["a.png", "b.png"], True)
            (root / "sparse/0/images.bin").write_bytes(struct.pack("<Q", 2**63))
            bad = review_dataset(root)
            self.assertEqual(bad["sourceKind"], "colmap")
            self.assertEqual(bad["groups"][0]["reasons"], ["invalid_model"])
            self.assertIsNone(bad["registeredCount"])
            self.assertEqual(bad["rows"][0]["status"], "unknown")

    def test_nested_same_basename_does_not_match_and_case_alias_is_conflict(self):
        with temporary_directory() as temp:
            root = Path(temp)
            for name in ("a/same.png", "b/same.png", "a/other.png"):
                path = root / "input" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"raw")
            # Model spelling differs from the actual directory entry even on
            # Windows, where creating two case-only aliases produces one file.
            model(root / "sparse/0", ["a/same.png", "A/OTHER.png"])
            report = review_dataset(root)
            rows = {r["name"]: r for r in report["rows"]}
            self.assertEqual(rows["b/same.png"]["registered"], False)
            self.assertIsNotNone(rows["a/same.png"]["pose"])
            self.assertTrue(any(r["status"] == "conflict" for r in report["rows"]))
            self.assertTrue(all(r["registered"] is None and r["training"] is None
                                for r in report["rows"] if r["status"] == "conflict"))

    def test_escaping_names_cannot_preview_or_navigate_and_unknown_group_refused(self):
        with temporary_directory() as temp:
            root = Path(temp)
            fixture(root)
            (root / "sparse/0/images.txt").write_text("3 1 0 0 0 0 0 0 91 ../private.png\n\n")
            report = review_dataset(root)
            row = next(r for r in report["rows"] if r["name"] == "../private.png")
            self.assertEqual(row["status"], "conflict")
            self.assertIsNone(row["imagePath"])
            self.assertIsNone(row["pose"])
            with self.assertRaisesRegex(ValueError, "unknown_group"):
                review_dataset(root, "../other")

    def test_file_and_group_mutations_change_identity_without_writes(self):
        with temporary_directory() as temp:
            root = Path(temp)
            fixture(root)
            first = review_dataset(root)
            (root / "input/写真/frame08.png").write_bytes(b"changed input")
            second = review_dataset(root)
            self.assertNotEqual(first["identity"], second["identity"])
            (root / "sparse/0/cameras.txt").write_text("91 PINHOLE 64 48 60 65 32 24\n")
            self.assertNotEqual(second["identity"], review_dataset(root)["identity"])

    def test_locked_transforms_keep_splits_without_claiming_sfm_registration(self):
        from PIL import Image
        with temporary_directory() as temp:
            root = Path(temp)
            for split in ("train", "val", "test"):
                Image.new("RGB", (64, 48), "gray").save(root / (split + ".png"))
                (root / ("transforms_" + split + ".json")).write_text(json.dumps(dict(camera_angle_x=.8,
                    frames=[dict(file_path="./" + split, transform_matrix=[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]])])))
            before = files(root)
            report = review_dataset(root)
            self.assertEqual(files(root), before)
            self.assertFalse(report["registrationApplicable"])
            self.assertIsNone(report["registeredCount"])
            self.assertEqual(report["trainingCount"], 1)
            self.assertEqual({r["split"] for r in report["rows"]}, {"train", "val", "test"})
            self.assertEqual(sum(r["training"] for r in report["rows"]), 1)
            self.assertTrue(all(r["pose"] is not None for r in report["rows"]))

    def test_cancellation_is_observed_before_enumeration_or_hash(self):
        with temporary_directory() as temp:
            with self.assertRaises(InterruptedError):
                review_dataset(temp, cancelled=lambda: True)

    def test_archived_photo_manifest_can_enumerate_originals_but_video_count_cannot(self):
        with temporary_directory() as temp:
            root = Path(temp)
            (root / "source/originals").mkdir(parents=True)
            (root / "source/originals/a.png").write_bytes(b"original")
            path = root / "source/metadata_manifest.json"
            path.write_text(json.dumps(dict(files=[dict(kind="image", trainingPath="images/a.png",
                archivePath="source/originals/a.png")])))
            report = review_dataset(root)
            self.assertEqual(report["originalCount"], 1)
            self.assertEqual(report["rows"][0]["imagePath"], "source/originals/a.png")
            path.write_text(json.dumps(dict(files=[dict(kind="video", extractedFrames=9)])))
            report = review_dataset(root)
            self.assertIsNone(report["originalCount"])
            self.assertEqual(report["rows"], [])

    def test_missing_prepared_image_invalidates_training_list_not_original_registration(self):
        with temporary_directory() as temp:
            root = Path(temp)
            names = fixture(root)
            (root / "images" / names[0]).unlink()
            report = review_dataset(root)
            self.assertEqual(report["originalCount"], 9)
            self.assertEqual(report["registeredCount"], 2)
            self.assertIsNone(report["trainingCount"])
            self.assertEqual(report["trainingStatus"], "not_prepared")

    def test_changed_calibration_or_missing_model_never_becomes_zero_error(self):
        with temporary_directory() as temp:
            root = Path(temp)
            fixture(root)
            (root / "sparse/0/cameras.txt").write_text("91 PINHOLE 64 48 nan 55 32 24\n")
            report = review_dataset(root)
            self.assertIsNone(report["groups"][0]["meanReprojectionError"])
            self.assertTrue(all(r["pose"] is None for r in report["rows"]))

    def test_mesh_review_follows_selected_model_source_not_an_unrelated_workspace(self):
        with temporary_directory() as temp:
            root = Path(temp)
            dataset, output = root / "dataset", root / "output"
            dataset.mkdir()
            fixture(dataset)
            output.mkdir()
            (output / "cfg_args").write_text("Namespace(source_path={!r}, images='images')".format(str(dataset)))
            before = files(root)
            report = review_model(output)
            self.assertEqual(report["dataset"], str(dataset.resolve()))
            self.assertEqual(report["originalCount"], 9)
            self.assertEqual(files(root), before)
            (output / "cfg_args").write_text("Namespace(source_path=__import__('os').getcwd())")
            with self.assertRaises(ValueError):
                review_model(output)
            (output / "cfg_args").write_text("Namespace(source_path='relative-maybe-wrong')")
            with self.assertRaisesRegex(ValueError, "model_source_unverifiable"):
                review_model(output)


if __name__ == "__main__":
    unittest.main()
