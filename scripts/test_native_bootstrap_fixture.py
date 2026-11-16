"""Remote-only FIRST-01 tracer; Python is setupLab, never the client runtime.

The native harness creates an actual restricted medium-integrity child token,
private desktop and an environment without developer tools or credentials.
The client runs the shared first-install transaction against an independently
pinned lab bootstrap kit and the real signed HTTPS/TUF helper.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys


def write_json(path: Path, value: dict):
    with path.open("x", encoding="utf-8") as output:
        json.dump(value, output, ensure_ascii=True, indent=2)
        output.write("\n")


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def script(name: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def regular_tree(path: Path) -> dict[str, str]:
    assert path.is_dir() and not path.is_symlink() and not path.is_junction(), path
    result = {}
    for item in path.rglob("*"):
        info = item.lstat()
        assert not getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT, item
        if not item.is_dir():
            assert stat.S_ISREG(info.st_mode) and info.st_nlink == 1, item
            result[item.relative_to(path).as_posix()] = digest(item)
    return result


def lab_preparation(path: Path):
    prep = json.loads(path.read_text(encoding="utf-8-sig"))
    assert prep["schema"] == "gsw.update_end_to_end_preparation/1" and prep["testOnly"] is True
    assert prep["production"] is False and prep["formalGswAcceptance"] is False
    assert Path(prep["root"]).resolve() == path.resolve().parent == Path("D:/gb/lab").resolve()
    assert prep["sourceCommit"] == os.environ["GITHUB_SHA"]
    return prep


def prepare_bundle(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare-bundle", type=Path, required=True)
    parser.add_argument("--preparation", type=Path, required=True)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--windeployqt", type=Path, required=True)
    args = parser.parse_args(argv)
    prep = lab_preparation(args.preparation)
    root = args.prepare_bundle.resolve()
    assert root == Path("D:/gb/build/Release/bootstrap").resolve()
    assert args.helper.name == "GSW Update Fixture Helper.exe"
    root.mkdir(parents=True, exist_ok=False)
    shutil.copytree(Path(prep["installationRoot"]) / "bin", root / "bin")
    helper = root / "bin" / args.helper.name
    shutil.copyfile(args.helper, helper)
    script("prepare_native_update_end_to_end").deploy(args.windeployqt.resolve(), helper, helper.parent)
    files = regular_tree(root / "bin")
    manifest = root / "bootstrap.json"
    write_json(manifest, {"schema": "gsw.native_bootstrap/1", "protocol": 2,
        "product": "Fubukisama.GaussianSceneWorkbench.UpdateFixture", "platform": "windows-x64",
        "files": [{"path": "bin/" + path, "size": (root / "bin" / path).stat().st_size, "sha256": value}
                  for path, value in sorted(files.items())]})
    print(digest(manifest))
    return 0


def main() -> int:
    if os.name != "nt" or os.environ.get("GITHUB_ACTIONS") != "true" or os.environ.get("RUNNER_OS") != "Windows":
        raise SystemExit("remote_windows_lab_only")
    if "--prepare-bundle" in sys.argv[1:]:
        return prepare_bundle(sys.argv[1:])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", type=Path, required=True)
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--preparation", type=Path, required=True)
    parser.add_argument("--loader-probe", type=Path)
    args = parser.parse_args()
    if not args.root.is_absolute() or args.root.drive.casefold() != "d:" or args.root.exists() or args.root.is_symlink():
        parser.error("fresh_owned_d_drive_fixture_root_required")
    prep = lab_preparation(args.preparation)
    fixture = json.loads(Path(prep["fixture"]).read_text(encoding="utf-8"))
    assert fixture["testOnly"] is True and fixture["product"] == "Fubukisama.GaussianSceneWorkbench.UpdateFixture"
    bootstrap_manifest = args.bootstrap.resolve().parent / "bootstrap" / "bootstrap.json"
    assert bootstrap_manifest.is_file()
    manifest_bytes = bootstrap_manifest.read_bytes()
    manifest_hash = hashlib.sha256(manifest_bytes).hexdigest()
    source_files = regular_tree(bootstrap_manifest.parent / "bin")
    args.evidence.mkdir(parents=True, exist_ok=True)
    args.root.mkdir(parents=True)
    destination = args.root / "First install with spaces 日本語 中文"
    pinned = digest(args.bootstrap)
    # The controller selects an exact locally built CI artifact independently
    # of the update feed. This pin is only test-bootstrap source authorization,
    # not an Authenticode certificate or product/release acceptance proof.
    write_json(args.evidence / "bootstrap-source-pin.json", {"schema": "gsw.bootstrap_fixture_authorization/1",
        "testOnly": True, "sourceAuthority": "root-approved-out-of-band-ci-artifact-pin",
        "sourceCommit": os.environ.get("GITHUB_SHA"), "runId": os.environ.get("GITHUB_RUN_ID"),
        "runAttempt": os.environ.get("GITHUB_RUN_ATTEMPT"), "bootstrapSha256": pinned,
        "bootstrapManifestSha256": manifest_hash,
        "authenticodeClaim": False, "production": False, "privateKeyCreated": False})
    def install(label: str, target: Path, expected: str, *, predicate: bool = False):
        command = [str(args.harness.absolute()), "--bootstrap", str(args.bootstrap.absolute()),
            "--destination", str(target), "--expected-sha256", pinned,
            "--evidence", str(args.evidence.absolute() / (label + "-terminal-native.json"))]
        if args.loader_probe:
            command.extend(["--loader-probe", str(args.loader_probe.absolute()),
                            "--expected-probe-sha256", digest(args.loader_probe)])
        result = subprocess.run(command, capture_output=True, timeout=210, creationflags=subprocess.CREATE_NO_WINDOW)
        native = json.loads(result.stdout)
        write_json(args.evidence / (label + "-tracer.json"), {"schema": "gsw.bootstrap_first_install_tracer/1",
            "testOnly": True, "production": False, "harnessExitCode": result.returncode, "harness": native,
            "stderrPresent": bool(result.stderr), "expectedBootstrapCode": expected,
            "scope": "actual-restricted-medium-token-native-client-path", "physicalToolRemovalClaim": False})
        # An environment/loader failure is not a successful negative case.
        assert result.returncode == 0 and native.get("ok") is True, native
        facts = native["terminalToken"]
        assert facts["restricted"] is True and facts["integrityRid"] == 8192
        assert facts["adminEnabled"] is False and facts["elevated"] is False and facts["sandboxInert"] is False
        assert facts["systemAccount"] is False and facts["unexpectedEnabledPrivilegeCount"] == 0
        assert native["setupLabTokenUsedByClient"] is False and native["passwordCreated"] is False
        assert native["authenticode"]["status"] == "NOT_SIGNED" and native["authenticode"]["publisherAuthenticated"] is False
        child = native["childResponse"]
        assert child["terminalEnvironment"]["discoverableDeveloperTools"] == []
        assert child["terminalEnvironment"]["sensitiveEnvironmentPresent"] is False
        assert native["childExitCode"] == (0 if predicate else 2) and child.get("ok") is predicate, child
        assert child.get("code") == expected and child.get("targetMutated") is predicate, child
        assert not list(args.root.glob(".gsw-first-install-*")), "unexpected leftover staging root"
        return child

    end_to_end = script("test_native_update_end_to_end").EndToEnd()
    end_to_end.preparation, end_to_end.fixture, end_to_end.installation = prep, fixture, destination
    end_to_end.evidence, end_to_end.steps = args.evidence.resolve(), []
    end_to_end.helper = destination / "bin/GSW Update Fixture Helper.exe"
    end_to_end.select("A")
    child = install("first-install", destination, "BOOTSTRAP_COMMITTED", predicate=True)
    pointer = json.loads((destination / "current.json").read_bytes())
    actual_slot = destination / "versions/fixture-a"
    expected_slot = Path(prep["root"]) / "package-A"
    expected_digest = digest(expected_slot / "bin/Gaussian Scene Workbench.exe")
    assert pointer == {"schema": "gsw.native_active_version/1", "version": "fixture-a", "executableSha256": expected_digest}
    assert child["buildId"] == pointer["version"] and child["executableSha256"] == expected_digest
    assert (destination / "bootstrap.json").read_bytes() == manifest_bytes
    assert regular_tree(destination / "bin") == source_files
    slot_hashes = regular_tree(actual_slot)
    release_manifest_hash = slot_hashes.pop("release-files.json")
    assert slot_hashes == regular_tree(expected_slot)
    receipt = json.loads((destination / "updates/receipts/fixture-a.json").read_bytes())
    report = fixture["archives"]["A"]
    assert receipt["mode"] == "online" and receipt["executableSha256"] == expected_digest
    assert receipt["manifestSha256"] == release_manifest_hash == report["payloadManifestSha256"]
    assert receipt["offer"]["archiveSha256"] == report["archiveSha256"]
    assert re.fullmatch(r"[a-f0-9]{64}", receipt["descriptorSha256"])
    assert (destination / ".update-trust-anchor").is_file() and (destination / "updates/trust").is_dir()
    stable = regular_tree(destination)
    install("occupied-root", destination, "BOOTSTRAP_DESTINATION_OCCUPIED")
    assert regular_tree(destination) == stable
    tampered = args.root / "Tampered bootstrap must not install"
    try:
        bootstrap_manifest.write_bytes(manifest_bytes + b"\n")
        install("tampered-bootstrap", tampered, "BOOTSTRAP_MANIFEST_MISMATCH")
        assert not tampered.exists()
    finally:
        bootstrap_manifest.write_bytes(manifest_bytes)
    untrusted = args.root / "Corrupted signed asset must not install"
    end_to_end.select("badC")
    try:
        rejected = install("corrupt-signed-asset", untrusted, "BOOTSTRAP_HELPER_FAILED")
        assert rejected["helperCode"] == "download_hash_mismatch", rejected
        assert not untrusted.exists() and regular_tree(destination) == stable
    finally:
        end_to_end.select("A")
    # Check stable-entry launch separately from the mutation-free negative
    # cases. Launcher startup health may legitimately write an observation.
    observed_launch = end_to_end.launch("A", arguments=("first-install-readback", "中 日"))
    assert regular_tree(destination / "bin") == source_files
    assert regular_tree(actual_slot) == {**slot_hashes, "release-files.json": release_manifest_hash}
    write_json(args.evidence / "acceptance.json", {"schema": "gsw.bootstrap_first_install_acceptance/1",
        "testOnly": True, "production": False, "authenticodeClaim": False, "accepted": True,
        "scope": "restricted-native-first-install-fixture-only", "physicalToolRemovalClaim": False,
        "sourceCommit": os.environ.get("GITHUB_SHA"), "sourceTree": prep["sourceTree"], "runId": os.environ.get("GITHUB_RUN_ID"),
        "bootstrapManifestSha256": manifest_hash, "executableSha256": expected_digest,
        "independentBootstrapFiles": len(source_files), "independentSlotFiles": len(slot_hashes),
        "signedOfferArchiveSha256": report["archiveSha256"], "stableEntryLaunched": observed_launch["windowShown"],
        "occupiedRootPreserved": True, "changedPinnedManifestRefused": True, "corruptSignedAssetRefused": True})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
