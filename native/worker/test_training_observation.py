"""Exercise the observation producer and the real worker status boundary."""
import hashlib
import tempfile
import unittest
from pathlib import Path

import numpy as np

from native.backend.compute import apply_training_preview
from native.worker.gsw_worker import status_telemetry
from native.worker.training_preview import TrainingPreviewPublisher


class TrainingObservationTests(unittest.TestCase):
    def test_atomic_observations_survive_status_and_reject_foreign_or_replayed_frames(self):
        for backend, fields in (("3dgs", 14), ("2dgs", 13)):
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as directory:
                control = {"session": "a" * 32, "observationRunId": "b" * 32, "backend": backend}
                source, identity = "1" * 64, "2" * 64
                sim = {"s": 2., "R": [[1, 0, 0], [0, 1, 0], [0, 0, 1]], "t": [1, 2, 3],
                       "applied": True}
                contract = {"input_data_identity": source, "scale": {"source_unit": "cm",
                            "meters_per_unit": .01}, "transforms": {"source_to_canonical": sim,
                            "canonical_to_training": sim}}
                events = []
                publisher = TrainingPreviewPublisher(directory, lambda _, value: events.append(value),
                    control=control, training_identity=identity, camera_contract=contract)
                try:
                    self.assertTrue(publisher.publish_rows(np.zeros((3, fields)), 7, 5))
                finally:
                    publisher.close()
                event, = events
                frame = event["observation"]
                path = Path(event["point_cloud_path"])
                self.assertEqual(frame["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
                self.assertEqual(frame["iteration"], 7)
                self.assertEqual(frame["generation"], 1)
                self.assertEqual(frame["rowFloats"], fields)
                self.assertEqual(frame["datasetIdentity"], source)
                self.assertEqual(frame["frame"]["units"], "u")
                self.assertEqual(frame["frame"]["canonicalToTraining"], sim)
                self.assertEqual(b"property float scale_2\n" in path.read_bytes(), backend == "3dgs")
                job = {"native_control": control, "backend": backend, "output_dir": directory,
                       "camera_dataset_identity": source, "pose_policy": "locked"}
                for altered in (dict(frame, runId="c" * 32), dict(frame, datasetIdentity="3" * 64),
                                dict(frame, rowFloats=15), dict(frame, generation=True)):
                    apply_training_preview(job, dict(event, observation=altered))
                    self.assertNotIn("preview_observation", job)
                apply_training_preview(job, event)
                self.assertEqual(status_telemetry(job)["previewObservation"], frame)
                apply_training_preview(job, dict(event, iteration=8, observation=dict(frame, iteration=8)))
                self.assertEqual(job["preview_iteration"], 7)  # same generation
                apply_training_preview(job, {"iteration": 9, "point_cloud_path": str(path),
                                              "preview_kind": "training_checkpoint"})
                self.assertNotIn("previewObservation", status_telemetry(job))


if __name__ == "__main__":
    unittest.main()
