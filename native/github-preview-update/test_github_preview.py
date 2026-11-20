"""External process contract for the test-only transport build. No production feed overrides."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def put(path, value):
    path.write_text(json.dumps(value), encoding="utf-8")


def run(helper, *args, env):
    proc = subprocess.run([str(helper), *args], capture_output=True, env=env, timeout=90)
    rows = [json.loads(line) for line in proc.stdout.splitlines() if line]
    assert rows, (proc.returncode, proc.stderr)
    return proc.returncode, rows[-1]


def check(helper_build, root):
    product = "Fubukisama.GaussianSceneWorkbench.Native"
    fixed = root / "Fixed path 日本語 中文"
    bindir = fixed / "bin"
    bindir.mkdir(parents=True)
    helper = bindir / helper_build.name
    shutil.copy2(helper_build, helper)
    (fixed / "gsw-installation.ini").write_text("[Installation]\nAppId=" + product + "\n")
    feed = root / "issuer"
    feed.mkdir()
    env = dict(os.environ, GSW_GITHUB_PREVIEW_FIXTURE=str(feed))
    base = "https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/"
    seq = 2026112002
    tag = "v0.3.9-native-preview.20261120.2"
    setup = "Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe"
    payload = b"controlled non-executable download: do not run"
    source = "ab" * 20
    manifest = {"schema": "gsw.github_preview/1", "product": product, "platform": "windows-x64",
                "tag": tag, "sequence": seq, "sourceCommit": source, "setup": setup,
                "archive": "gsw-windows-x64-managed.zip", "offer": {
                    "schema": "gsw.update_offer/1", "product": product, "platform": "windows-x64",
                    "buildId": "preview-2026112002-abababababab", "sourceCommit": source,
                    "sourceTree": "cd" * 20, "releaseSequence": seq, "displayVersion": "0.3.9 preview",
                    "minimumBootstrapProtocol": 1, "archiveSha256": sha(b"managed"),
                    "archiveLength": 7, "unpackedLength": 7, "fileCount": 1,
                    "payloadManifestSha256": "34" * 32, "correspondingSourceSha256": "56" * 32}}
    manifest_name = "gsw-windows-x64-preview.json"
    put(feed / manifest_name, manifest)
    (feed / setup).write_bytes(payload)
    (feed / manifest["archive"]).write_bytes(b"managed")
    def asset(name):
        data = (feed / name).read_bytes()
        return {"name": name, "size": len(data), "digest": "sha256:" + sha(data),
                "state": "uploaded", "browser_download_url": base + tag + "/" + name}
    release = {"tag_name": tag, "prerelease": True, "immutable": True, "draft": False,
               "assets": [asset(n) for n in (manifest_name, setup, manifest["archive"])]}
    put(feed / "releases.json", [release])
    code, row = run(helper, "--check", "preview", env=env)
    assert code == 0 and row["phase"] == "available" and row["offer"]["buildId"] == manifest["offer"]["buildId"], row
    code, row = run(helper, "--prepare", "preview", env=env)
    assert code == 0 and row["phase"] == "prepared", row
    assert (fixed / "gsw-installation.ini").exists() and not (fixed / "current.json").exists()
    (feed / setup).write_bytes(b"x" * len(payload))
    # A new empty cache must reject corruption, even with the expected length.
    other = root / "fresh fixed"
    (other / "bin").mkdir(parents=True)
    shutil.copy2(helper_build, other / "bin" / helper.name)
    shutil.copy2(fixed / "gsw-installation.ini", other)
    code, row = run(other / "bin" / helper.name, "--prepare", "preview", env=env)
    assert code != 0 and row["phase"] == "failed", row
    assert not (other / ".gsw-github-preview/prepared.json").exists()
    release["assets"][1]["browser_download_url"] = "https://example.invalid/other.exe"
    put(feed / "releases.json", [release])
    code, row = run(helper, "--prepare", "preview", env=env)
    assert code != 0, row
    release["immutable"] = False
    put(feed / "releases.json", [release])
    assert run(helper, "--prepare", "preview", env=env)[0] != 0
    release["immutable"] = True
    release["tag_name"] = "v0.3.9-native-preview.20261119.1"
    put(feed / "releases.json", [release])
    assert run(helper, "--check", "preview", env=env)[1]["phase"] == "current"
    assert run(helper, "--prepare", "preview", env=env)[0] != 0
    (fixed / ".update-trust-anchor").write_bytes(b"existing independent trust")
    code, row = run(helper, "--check", "preview", env=env)
    assert code != 0 and row["code"] == "preview_trusted_root_forbidden", row
    assert run(helper, "--check", "stable", env=env)[0] != 0


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        check(Path(sys.argv[1]).resolve(), Path(directory))
