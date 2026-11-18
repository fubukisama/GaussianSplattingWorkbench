"""The immutable identity of a read-only stream, not a training checkpoint."""
import copy
import re
from pathlib import Path


SCHEMA = "gsw.training_observation/1"
ROW_FLOATS = {"3dgs": 14, "2dgs": 13}
_STREAM_KEYS = ("schema", "runId", "backend", "datasetIdentity", "trainingIdentity", "frame")


def is_hex(value, length):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{%d}" % length, value) is not None


def stream_identity(control, identity, camera_contract):
    if not control or not is_hex(control.get("observationRunId"), 32) or not is_hex(identity, 64):
        return None  # Compatibility for older jobs and the external trainer CLI.
    contract = camera_contract or {}
    source = contract.get("input_data_identity", "")
    transforms = contract.get("transforms", {})

    def similarity(name):
        value = transforms.get(name)
        return {key: copy.deepcopy(value[key]) for key in ("s", "R", "t", "applied")} if value else None

    scale = contract.get("scale", {})
    return {"schema": SCHEMA, "runId": control["observationRunId"], "backend": control["backend"],
            "datasetIdentity": source, "trainingIdentity": identity,
            "frame": {"coordinates": "training", "units": "u",
                      "sourceUnit": scale.get("source_unit"),
                      "sourceMetersPerUnit": scale.get("meters_per_unit"),
                      "sourceToCanonical": similarity("source_to_canonical"),
                      "canonicalToTraining": similarity("canonical_to_training")}}


def stream_fields(observation):
    return {key: observation.get(key) for key in _STREAM_KEYS}


def admit_file_observation(job, payload, iteration):
    """Reject mixed jobs, backwards generation and escaped/wrong layout paths.

    The viewer verifies the payload digest and layout on the same read handle.
    Capacity, accepted iteration and elapsed time are not training controls.
    """
    control = job.get("native_control") or {}
    owner = control.get("observationRunId")
    frame = payload.get("observation")
    if owner is None and frame is None:
        return True  # An already-running older worker may still publish its file.
    if not isinstance(frame, dict) or not is_hex(owner, 32):
        return False
    backend = job.get("backend", "3dgs")
    dataset = frame.get("datasetIdentity")
    if (frame.get("schema") != SCHEMA or frame.get("runId") != owner or frame.get("backend") != backend
            or not is_hex(frame.get("trainingIdentity"), 64) or not is_hex(frame.get("sha256"), 64)
            or (dataset != "" and not is_hex(dataset, 64))
            or (job.get("pose_policy") == "locked" and dataset != job.get("camera_dataset_identity"))
            or frame.get("transport") != "atomic_ply" or frame.get("rowFloats") != ROW_FLOATS.get(backend)
            or type(frame.get("iteration")) is not int or frame["iteration"] != iteration
            or type(frame.get("generation")) is not int or not 0 < frame["generation"] <= 2147483647
            or type(frame.get("previewCount")) is not int or not 0 < frame["previewCount"] <= 500000
            or type(frame.get("sourceCount")) is not int or frame["sourceCount"] != payload.get("gaussian_count")
            or not frame["previewCount"] <= frame["sourceCount"] <= 2147483647
            or frame["previewCount"] != payload.get("preview_count")
            or not isinstance(frame.get("producerMonotonicNs"), str)
            or not re.fullmatch(r"[1-9][0-9]{0,19}", frame["producerMonotonicNs"])):
        return False
    if job.get("observation_stream") not in (None, stream_fields(frame)):
        return False
    if frame["generation"] <= job.get("observation_generation", 0):
        return False
    if iteration <= job.get("observation_iteration", -1):
        return False
    try:
        path = Path(payload["point_cloud_path"]).resolve(strict=True)
        base = Path(control.get("output") or job["output_dir"]).resolve(strict=True) / ".gsw" / "previews"
        group, filename = path.relative_to(base).parts
        if not is_hex(group, 32) or filename != "preview_%06d.ply" % frame["generation"]:
            return False
    except (KeyError, OSError, ValueError):
        return False
    return True
