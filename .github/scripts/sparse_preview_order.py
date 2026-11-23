"""Inspect the worker event and actual PLY for a late sparse mapper snapshot."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

from plyfile import PlyData


BASELINE = "e96102ef6d19b0c39384fb5dab530444dba3f5b8"


def snapshot(root, folder, coordinate, count, mtime):
    path = root / folder / "points3D.bin"
    path.parent.mkdir(parents=True, exist_ok=True)
    # Native COLMAP structure: count, then XYZ/RGB/error and a track per point.
    with path.open("wb") as stream:
        stream.write(struct.pack("<Q", count))
        for index in range(count):
            stream.write(struct.pack("<QdddBBBdQII",
                index + 1, coordinate, index, 1.0, 11, 21, 31, 0.25, 1, 1, index))
    os.utime(path, (mtime, mtime))
    return path


def read_observation(job, compute, worker):
    status = (compute.mesh_job_snapshot(job) if job["kind"] == "colmap"
              else compute.job_snapshot(job))
    event = worker.status_telemetry(status)
    assert event["previewKind"] == "colmap_sparse", event
    vertices = PlyData.read(event["previewPath"])["vertex"]
    assert len(vertices) == event["gaussianCount"], "The announced fixture PLY is incomplete"
    assert len(set(vertices["x"])) == 1, "The observation must come from one capture"
    return [event["previewIteration"], len(vertices), float(vertices["x"][0])]


def late_snapshot_contract(publisher, source, read, witness):
    newest = [1, 3, 20.0]
    snapshot(source, "1700000000002", 20.0, 3, 1_700_000_000)
    assert not publisher.publish_latest(), "A source must be observed as stable first"
    assert publisher.publish_latest() and read() == newest, "Initial newer observation did not arrive"
    snapshot(source, "1700000000001", 10.0, 6, 1_700_000_010)
    assert not publisher.publish_latest(), "The late source was not stable on first sight"
    publisher.publish_latest()
    actual = read()
    if witness:
        assert actual == [2, 6, 10.0], f"Did not reproduce an older payload with a fresh generation: {actual}"
    else:
        assert actual == newest, f"A late older payload replaced the newer observation: {actual}"
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--witness", action="store_true")
    args = parser.parse_args()
    if os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted" or sys.platform != "win32":
        raise SystemExit("Use the disposable hosted Windows workflow")
    source = args.source.resolve(strict=True)
    commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    expected = BASELINE if args.witness else os.environ["GITHUB_SHA"]
    if commit != expected: raise SystemExit("Source does not match requested test mode")
    sys.path.insert(0, str(source))
    from native.backend import compute
    from native.worker import gsw_worker as worker

    for route in ("colmap", "3dgs", "2dgs"):
        dataset = args.root / route
        job = dict(id=route, kind="colmap" if route == "colmap" else "training",
                   backend=route, scene="fixture", output_scene="fixture", mode="colmap",
                   iteration=0, status="running", stage="colmap", log=[], created_at=1, updated_at=1)
        mapper = dataset / "snapshots"
        publisher = compute.ColmapPointCloudPreviewPublisher(job, dataset, [mapper])
        actual = late_snapshot_contract(publisher, mapper, lambda: read_observation(job, compute, worker), args.witness)
        print(json.dumps(dict(source=commit, route=route, outcome="REPRODUCED" if args.witness else "PASS",
                              observedGenerationPointsX=actual)), flush=True)


if __name__ == "__main__":
    main()
