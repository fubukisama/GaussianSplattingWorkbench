"""Starter packaging contract: the stable entry and the installed app are distinct."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from prepare_github_preview_release import APP, LAUNCHER, STARTER, write_starter


def sha(data):
    return hashlib.sha256(data).hexdigest()


def create_fixture(update, slot, payload):
    manifest = json.dumps({"buildId": slot, "files": [
        {"path": name, "size": len(data), "sha256": sha(data)} for name, data in payload.items()
    ]}).encode()
    with zipfile.ZipFile(update, "x") as pack:
        pack.writestr("release-files.json", manifest)
        for name, data in payload.items():
            pack.writestr(name, data)
    return {"buildId": slot, "archiveSha256": sha(update.read_bytes()),
            "payloadManifestSha256": sha(manifest), "fileCount": len(payload)}


def check(root):
    payload = {APP: b"versioned app fixture; never executed",
               LAUNCHER: b"stable launcher fixture; never executed",
               "bin/Qt6Core.dll": b"launcher dependency",
               "source/native/file.txt": b"unchanged corresponding source"}
    slot = "preview-2026112301-012345abcdef"
    update = root / "update.zip"
    report = create_fixture(update, slot, payload)
    starter = root / STARTER
    assert write_starter(update, starter, report, sha(payload[APP])) == sha(starter.read_bytes())
    with zipfile.ZipFile(starter) as pack:
        assert APP not in pack.namelist() and pack.read(LAUNCHER) == payload[LAUNCHER]
        assert pack.read(f"versions/{slot}/{APP}") == payload[APP]
        assert pack.read(f"versions/{slot}/source/native/file.txt") == payload["source/native/file.txt"]
        boot = json.loads(pack.read("bootstrap.json"))
        assert boot["protocol"] == 1 and {f["path"] for f in boot["files"]} == {LAUNCHER, "bin/Qt6Core.dll"}
        assert json.loads(pack.read("current.json")) == {
            "schema": "gsw.native_active_version/1", "version": slot, "executableSha256": sha(payload[APP])}
        assert not any("trust" in name or "gsw-installation.ini" in name for name in pack.namelist())
    try:
        write_starter(update, starter, report, sha(payload[APP]))
    except ValueError:
        pass
    else:
        raise AssertionError("starter builder overwrote an existing output")
    try:
        write_starter(update, root / "wrong.zip", report, "f" * 64)
    except ValueError:
        pass
    else:
        raise AssertionError("starter did not reject the wrong application identity")
    anchored = root / "anchored-input.zip"
    trusted_report = create_fixture(anchored, slot, {**payload, ".update-trust-anchor": b"owned trust fixture"})
    try:
        write_starter(anchored, root / "reject.zip", trusted_report, sha(payload[APP]))
    except ValueError as error:
        assert "not an existing installation" in str(error), error
    else:
        raise AssertionError("starter accepted an existing trusted installation")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        check(Path(tmp))
