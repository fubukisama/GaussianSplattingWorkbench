#!/usr/bin/env python3
"""Bounded, opt-in CUDA + native-GL acceptance. Never use a user's scene or job."""
import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

import readme_demo_training as demo

ROOT = Path(__file__).resolve().parents[1]
FLAGS = getattr(subprocess, "CREATE_NO_WINDOW", 0)
STEPS = 4200
SEED = 73129
ALLOCATOR_MIB = 3072
WALL_SECONDS = 115
PREFIXES = ("[gsw-training-preview]", "[gsw-training-metrics]")


def audited_child(args):
    """Observe actual optimizer/topology calls in the owned fixture only."""
    import importlib
    import random
    from native.worker.training_density_control import NativeDensityControl

    checkpoint = importlib.import_module("native.worker.training_checkpoint")
    root = demo.ordinary_d_path(args.run_root)
    control = demo.read_json(root / "native-control.json")
    phase = "resume" if control["resume"] else "live"
    density = NativeDensityControl.densify_and_prune
    restore = checkpoint.restore_state

    def audit_density(controller, model, *values, **options):
        old = getattr(model, "prune_points")
        had_instance_method = "prune_points" in vars(model)
        reductions = []
        before = len(model.get_xyz)

        def observe_prune(mask):
            prior = len(model.get_xyz)
            output = old(mask)
            reductions.append({"before": prior, "after": len(model.get_xyz),
                               "caller": sys._getframe(1).f_code.co_name})
            return output

        model.prune_points = observe_prune
        try:
            output = density(controller, model, *values, **options)
        finally:
            if had_instance_method:
                model.prune_points = old
            else:
                del model.prune_points
        with (root / (phase + "-actual-topology.jsonl")).open("a", encoding="utf-8") as record:
            record.write(json.dumps({"iteration": values[3], "before": before,
                       "after": len(model.get_xyz), "pruneCalls": reductions}) + "\n")
        return output

    def audit_restore(torch, np, model, opt, state, *values, **options):
        output = restore(torch, np, model, opt, state, *values, **options)

        def equal(expected, actual):
            if torch.is_tensor(expected):
                return torch.is_tensor(actual) and torch.equal(expected, actual)
            if isinstance(expected, np.ndarray):
                return np.array_equal(expected, actual)
            if isinstance(expected, dict):
                return (isinstance(actual, dict) and expected.keys() == actual.keys() and
                        all(equal(value, actual[key]) for key, value in expected.items()))
            if isinstance(expected, (list, tuple)):
                return (isinstance(actual, (list, tuple)) and len(expected) == len(actual) and
                        all(equal(a, b) for a, b in zip(expected, actual)))
            return expected == actual

        proof = {"iteration": state["iteration"], "identity": state["identity"],
                 "model_with_adam_exact": equal(state["model"], model.capture()),
                 "python_random_exact": equal(state["random"], random.getstate()),
                 "numpy_random_exact": equal(state["numpy"], np.random.get_state()),
                 "torch_random_exact": equal(state["torch"], torch.get_rng_state()),
                 "cuda_random_exact": equal(state["cuda"], torch.cuda.get_rng_state_all())}
        demo.write_json(root / "restore-state-audit.json", proof)
        if not all(value for key, value in proof.items() if key.endswith("_exact")):
            raise RuntimeError("Owned restore did not reproduce persisted Adam / RNG state")
        return output

    NativeDensityControl.densify_and_prune = audit_density
    checkpoint.restore_state = audit_restore
    args.repository = str(ROOT)
    return demo.child(args)


def jsonl(path):
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def foreground_pid():
    user = ctypes.windll.user32
    user.GetForegroundWindow.restype = ctypes.c_void_p
    user.GetWindowThreadProcessId.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong))
    pid = ctypes.c_ulong()
    user.GetWindowThreadProcessId(user.GetForegroundWindow(), ctypes.byref(pid))
    return pid.value


def verified_runtime(backend):
    env = "gaussian_splatting" if backend == "3dgs" else "two_dgs"
    return demo.ordinary_d_path(f"D:/Apps/GSW/runtime/envs/{env}/python.exe")


def start_owned(command, environment, **kwargs):
    return subprocess.Popen([str(part) for part in command], cwd=str(ROOT), env=environment,
        stdin=subprocess.DEVNULL, stderr=subprocess.STDOUT, creationflags=FLAGS, **kwargs)


def gpu_headroom():
    observed = demo.gpu_memory()
    if observed["free_mib"] < 6144:
        raise RuntimeError("GPU headroom below 6144 MiB; no test process started: " + str(observed))
    return observed


def run_case(backend, case, dataset, prepared, base_env, app, software_gl):
    from native.backend.compute import apply_training_preview, apply_training_metrics
    from native.worker.gsw_worker import status_telemetry
    from native.worker.training_checkpoint import read_manifest

    case.mkdir()
    model = case / "model"
    model.mkdir()
    cpu_python = verified_runtime(backend)
    run_id, session = uuid.uuid4().hex, uuid.uuid4().hex
    source = prepared["contract"]["input_data_identity"]
    pause_path = case / "pause-request.json"
    control = {"output": str(model), "request": str(pause_path), "session": session,
               "backend": backend, "resume": False, "training_seed": SEED, "observationRunId": run_id,
               "runtimeOptions": {"version": 2,
                   "resourceGuard": {"enabled": True, "mode": "pause", "usage_fraction": .9,
                       "minimum_spare_mib": 512, "max_consecutive_denials": 3},
                   "execution": {"image_staging": True, "camera_prefetch": True, "capacity_storage": True,
                       "surfel_auxiliary": True, "loss_mode": "stock", "engine": "stock",
                       "gpu_slots": 1, "pinned_mib": 32, "gpu_mib": 64}}}
    viewer_config = case / "viewer.json"
    demo.write_json(viewer_config, {"runId": run_id, "backend": backend, "datasetIdentity": source, "iterations": STEPS})
    train_args = ["-s", str(dataset), "-m", str(model), "-r", "1", "--data_device", "cpu",
                  "--iterations", str(STEPS), "--test_iterations", "0", "--save_iterations", str(STEPS),
                  "--checkpoint_iterations", str(STEPS), "--training_seed", str(SEED),
                  "--densify_from_iter", "100", "--densify_until_iter", "850", "--densification_interval", "100",
                  "--densify_grad_threshold", "0.0002", "--opacity_reset_interval", "300"]
    if backend == "3dgs":
        train_args.extend(["--disable_viewer", "--enable_gpu_preview", "--optimizer_type", "default"])
    demo.write_json(case / "trainer-arguments.json", train_args)
    environment = dict(base_env, GSW_CAMERA_DATASET=prepared["manifestPath"],
        GSW_CAMERA_IMAGE_ROOT=prepared["imageRoot"], GSW_POSE_POLICY="locked", GSW_TRAINING_SEED=str(SEED),
        GS_EDITOR_WORKSPACE_ROOT=str(case / "workspace"))
    prefix = cpu_python.parent
    environment["PATH"] = os.pathsep.join((str(prefix), str(prefix / "Library/bin"),
         str(prefix / "Scripts"), "D:/Apps/CUDA/11.8/bin", environment.get("PATH", "")))
    command = [cpu_python, "-B", Path(__file__).resolve(), "--child", "--backend", backend,
               "--run-root", case, "--allocator-mib", str(ALLOCATOR_MIB)]
    observation_log = case / "observer.log"
    observation_log.touch(exist_ok=False)
    job = {"kind": "training", "native_control": control, "backend": backend,
           "camera_dataset_identity": source, "pose_policy": "locked", "total_iterations": STEPS}
    pipe_errors = []
    started = time.monotonic()
    producer = viewer = pump = None
    if foreground_pid() == 0:
        raise RuntimeError("Interactive Windows foreground protection is unavailable")
    take_gpu = gpu_headroom()

    def drain(process, filename, live):
        try:
            with (case / filename).open("x", encoding="utf-8", newline="\n") as log, \
                    observation_log.open("a", encoding="utf-8", newline="\n") as observer:
                for raw in process.stdout:
                    raw = raw.rstrip("\r\n")
                    log.write(raw + "\n")
                    log.flush()
                    if not live:
                        continue
                    if raw.startswith("[gsw-training-gpu-preview] "):
                        observer.write(raw + "\n")
                    elif any(raw.startswith(prefix + " ") for prefix in PREFIXES):
                        prefix, payload = raw.split(" ", 1)
                        event = json.loads(payload)
                        if prefix == PREFIXES[0]:
                            apply_training_preview(job, event)
                        elif prefix == PREFIXES[1]:
                            apply_training_metrics(job, event)
                        status = {"version": 1, "type": "status", "state": "running", "stage": "train"}
                        status.update(status_telemetry(job))
                        observer.write("[worker-event] " + json.dumps(status, allow_nan=False) + "\n")
                    observer.flush()
        except (ValueError, OSError) as error:
            pipe_errors.append(str(error))

    def start_producer(payload, label, live=False):
        demo.write_json(case / "native-control.json", payload)
        env = dict(environment, GSW_NATIVE_TRAINING_CONTROL=json.dumps(payload))
        process = start_owned(command, env, stdout=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
        reader = threading.Thread(target=drain, args=(process, label, live), daemon=True)
        reader.start()
        return process, reader

    try:
        qt = Path("D:/Apps/GSW/runtime/envs/gsw_native/Library")
        plugins = qt / "lib/qt6/plugins"
        if not (plugins / "platforms/qwindows.dll").is_file():
            raise RuntimeError("Compatible D-drive Qt platform plugin missing")
        viewer_env = dict(base_env, GSW_SMOKE_BACKGROUND="1", QT_LOGGING_TO_CONSOLE="1",
                          QT_PLUGIN_PATH=str(plugins), QT_QPA_PLATFORM_PLUGIN_PATH=str(plugins / "platforms"))
        viewer_env["PATH"] = str(qt / "bin") + ";" + viewer_env["PATH"]
        if software_gl is not None:
            viewer_env.update(QT_OPENGL="software", QT_OPENGL_DLL=str(software_gl))
            viewer_env["PATH"] = str(software_gl.parent) + ";" + viewer_env["PATH"]
        with (case / "viewport.log").open("xb") as console:
            viewer = start_owned([app, "--smoke-test-live-training", viewer_config, "--theme", "dark"],
                                 viewer_env, stdout=console)
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline and viewer.poll() is None:
                if foreground_pid() == viewer.pid:
                    raise RuntimeError("Owned viewport took foreground")
                viewport_record = case / "viewport.jsonl"
                if viewport_record.is_file() and b'"kind":"ready"' in viewport_record.read_bytes():
                    break
                time.sleep(.1)
            else:
                raise RuntimeError(f"Unactivated native GL viewport did not initialize (exit: {viewer.poll()})")
            producer, pump = start_producer(control, "training.log", live=True)
            paused = False
            while time.monotonic() - started < WALL_SECONDS:
                if pipe_errors or foreground_pid() in (viewer.pid, producer.pid):
                    raise RuntimeError("Foreground or observer transport guard failed: " + str(pipe_errors))
                if producer.poll() is not None:
                    break
                if viewer.poll() not in (None, 0):
                    raise RuntimeError("Viewport could not verify the real stream")
                if viewer.poll() == 0 and not paused and job.get("iteration", 0) >= 900:
                    demo.write_json(pause_path, {"session": session})
                    paused = True
                time.sleep(.1)
            if producer.poll() is None:
                raise TimeoutError("Owned live CUDA budget expired")
            pump.join(2)
            demo.write_json(case / "producer-exit.json", {"code": producer.returncode})
            if not paused or producer.returncode != 75 or viewer.poll() != 0 or pipe_errors:
                raise RuntimeError("Live capture or full-state pause failed")
        checkpoint, state = read_manifest(model)
        retained = {"iteration": checkpoint["iteration"], "sha256": demo.digest(state), "file": str(state)}
        pre_resume = demo.audit_cameras(model, prepared)
        events = jsonl(case / "viewport.jsonl")
        topology = jsonl(case / "live-actual-topology.jsonl")
        grew = [event for event in topology if event["after"] > event["before"]]
        trimmed = [event for event in topology if any(call["before"] > call["after"] for call in event["pruneCalls"])]
        demo.write_json(case / "growth.json", {"events": topology, "real_growth": grew, "actual_removed_or_reordered": trimmed})
        if not grew or not trimmed:
            raise RuntimeError("No actual growth and removal counts in the live density events")
        gpu_headroom()
        # Fresh process, original immutable input and schedule, native manifest
        # and actual optimizers/RNG restored by the backend; no PLY warm start.
        (case / "allocator-observations.jsonl").rename(case / "allocator-live.jsonl")
        (case / "child-runtime.json").rename(case / "child-runtime-live.json")
        resume = dict(control, resume=True, session=uuid.uuid4().hex, observationRunId=uuid.uuid4().hex,
                      request=str(case / "resume-pause-request.json"))
        control = resume
        producer, pump = start_producer(resume, "resume.log")
        producer.wait(timeout=75)
        pump.join(2)
        if producer.returncode != 0 or pipe_errors:
            raise RuntimeError("Fresh full-state resume failed")
        final = demo.ply_record(model / "point_cloud" / f"iteration_{STEPS}" / "point_cloud.ply")
        resumed_samples = demo.audit_cameras(model, prepared)
        restored = demo.read_json(case / "restore-state-audit.json")
        if (resumed_samples["last_observed_iteration"] != STEPS or
                pre_resume["camera_contract_sha256"] != resumed_samples["camera_contract_sha256"]):
            raise RuntimeError("Resumed optimizer did not preserve the locked camera contract")
        return {"backend": backend, "frames": [event for event in events if event["kind"] == "drawn"],
                "viewport_checks": events[-1], "renderer": events[0], "foreground_unchanged_by_test": True,
                "pause": retained, "exact_restore": restored, "resumed_final_model": final, "cameras": resumed_samples,
                "grown_events": len(grew), "trimmed_events": len(trimmed), "gpu_before": take_gpu}
    finally:
        if producer is not None and producer.poll() is None:
            demo.write_json(control["request"], {"session": control["session"]})
            try:
                producer.wait(timeout=8)
            except subprocess.TimeoutExpired:
                producer.terminate()
                producer.wait(timeout=5)
        if viewer is not None and viewer.poll() is None:
            viewer.terminate()
            viewer.wait(timeout=5)
        if pump is not None:
            pump.join(2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset")
    parser.add_argument("--output")
    parser.add_argument("--app")
    parser.add_argument("--software-gl", help="Use an existing compatible OpenGL test DLL; not an IPC or hardware proof.")
    parser.add_argument("--backend", choices=("3dgs", "2dgs", "both"), default="both")
    parser.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--run-root", help=argparse.SUPPRESS)
    parser.add_argument("--allocator-mib", type=int, default=ALLOCATOR_MIB, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if os.name != "nt" or os.getenv("CUDA_VISIBLE_DEVICES", "") not in ("", "0"):
        parser.error("Requires a Windows GPU0 with no conflicting device mask")
    if args.child:
        if not args.run_root or args.backend == "both" or args.allocator_mib != ALLOCATOR_MIB:
            parser.error("Invalid bounded owned worker arguments")
        return audited_child(args)
    if not (args.dataset and args.app and args.output):
        parser.error("The independent fixture, candidate EXE and fresh D output are required")
    app, output = demo.ordinary_d_path(args.app), demo.ordinary_d_path(args.output, exists=False)
    software_gl = demo.ordinary_d_path(args.software_gl) if args.software_gl else None
    source = demo.ordinary_d_path(args.dataset)
    if source == output or source in output.parents or output.exists():
        parser.error("Use a fresh, independent D-drive output directory")
    output.mkdir(parents=True)
    # Do not map/lock the compiler output; each take owns the actual test EXE.
    copy = output / app.name
    shutil.copyfile(app, copy)
    if demo.digest(copy) != demo.digest(app):
        raise RuntimeError("Candidate executable copy differs")
    from native.worker.canonical_training import prepare_training_dataset
    dataset, _contract, input_hashes = demo.clone_dataset(source, output)
    prepared = prepare_training_dataset(dataset, output / "prepared", {"resolutionPolicy": "preserve"})
    for name in ("tmp", "appdata", "cache"):
        (output / name).mkdir(exist_ok=True)
    env = dict(os.environ, PYTHONUTF8="1", PYTHONIOENCODING="utf-8", PYTHONDONTWRITEBYTECODE="1", PYTHONNOUSERSITE="1",
               TEMP=str(output / "tmp"), TMP=str(output / "tmp"), LOCALAPPDATA=str(output / "appdata"),
               APPDATA=str(output / "appdata"), CUDA_CACHE_PATH=str(output / "cache/cuda"),
               TORCH_HOME=str(output / "cache/torch"), XDG_CACHE_HOME=str(output / "cache"),
               TORCH_EXTENSIONS_DIR=str(output / "cache/extensions"), MPLCONFIGDIR=str(output / "cache/mpl"),
               MPLBACKEND="Agg", GSW_TWO_DGS_SOURCE="D:/Apps/GSW/runtime/2dgs",
               GSW_CAMERA_RASTERIZER_ROOT="D:/Apps/GSW/runtime/kernels/camera-pinhole3d-quality-20261023-v2")
    for key in ("PYTHONPATH", "GSW_QUALITY_PLAN", "GSW_TRAINING_LOSS_MODULE_PATH", "QT_QPA_PLATFORM", "QT_OPENGL", "QT_OPENGL_DLL"):
        env.pop(key, None)
    result = {"status": "running", "app": str(app), "executed_copy": str(copy), "app_sha256": demo.digest(copy), "cases": [],
              "software_gl": (str(software_gl) if software_gl else None),
              "software_gl_sha256": (demo.digest(software_gl) if software_gl else None)}
    try:
        for backend in (("3dgs", "2dgs") if args.backend == "both" else (args.backend,)):
            result["cases"].append(run_case(backend, output / backend, dataset, prepared, env, copy, software_gl))
        result["original_fixture_unchanged"] = all(demo.digest(source / name) == digest for name, digest in input_hashes.items())
        result["status"] = "passed" if result["original_fixture_unchanged"] else "failed"
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        result.update(status="failed", error=str(error))
    finally:
        demo.write_json(output / "RESULTS.json", result)
    print(json.dumps({"status": result["status"], "result": str(output / "RESULTS.json"), "error": result.get("error")}))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    sys.path.insert(0, str(ROOT))
    sys.exit(main())
