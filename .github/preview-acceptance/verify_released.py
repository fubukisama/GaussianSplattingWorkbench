"""Post-publication Windows readback. No token, signing key, publish access or test server."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request
import winreg

REPO = "fubukisama/GaussianSplattingWorkbench"
HOSTS = {"api.github.com", "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"}


class PublicRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        url = urllib.parse.urlparse(newurl)
        if url.scheme != "https" or url.hostname not in HOSTS or url.username or url.password or url.port not in (None, 443):
            raise RuntimeError("unexpected public download host")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def fetch(url, limit, path=None):
    request = urllib.request.Request(url, headers={"User-Agent": "GSW-anonymous-public-readback"})
    with urllib.request.build_opener(PublicRedirect()).open(request, timeout=90) as stream:
        if path is None:
            data = stream.read(limit + 1)
            assert len(data) <= limit
            return data
        size, hash_ = 0, hashlib.sha256()
        with path.open("xb") as file:
            while chunk := stream.read(1024 * 1024):
                size += len(chunk)
                assert size <= limit
                hash_.update(chunk)
                file.write(chunk)
        assert size == limit
        return hash_.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    for name in ("source", "payload", "baseline", "root", "evidence"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    assert os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted"
    assert not args.root.exists() and args.root.resolve().drive.lower() == "d:"
    args.root.mkdir()
    args.evidence.mkdir(exist_ok=True)
    os.environ.pop("GSW_GITHUB_PREVIEW_FIXTURE", None)
    expected_sha = os.environ["EXPECTED_SOURCE"]
    product_root = args.source.resolve()
    actual_sha = subprocess.check_output(["git", "-C", str(product_root), "rev-parse", "HEAD"], text=True).strip()
    assert expected_sha == actual_sha
    sys.path.insert(0, str(product_root / "native/github-preview-update"))
    import accept_installations as qa
    seq = int((args.payload / "gsw-preview-sequence.txt").read_text())
    version = qa.load(args.payload / "build_manifest.json")["version"]
    tag = f"v{version}.{seq // 100}.{seq % 100}"
    api = json.loads(fetch(f"https://api.github.com/repos/{REPO}/releases/tags/{tag}", 1024*1024))
    assert api["tag_name"] == tag and api["immutable"] is True and api["prerelease"] is True and api["draft"] is False
    records = {row["name"]: row for row in api["assets"]}
    def asset(name):
        rec = records[name]
        assert rec["browser_download_url"] == f"https://github.com/{REPO}/releases/download/{tag}/{name}" and rec["state"] == "uploaded"
        return rec
    meta = asset(qa.MANIFEST)
    data = fetch(meta["browser_download_url"], 256*1024)
    assert meta["size"] == len(data) and meta["digest"] == "sha256:"+hashlib.sha256(data).hexdigest()
    descriptor = json.loads(data)
    assert descriptor["sourceCommit"] == expected_sha and descriptor["sequence"] == seq and descriptor["tag"] == tag
    setup = asset(descriptor["setup"])
    assert setup["name"] == f"Gaussian-Scene-Workbench-{version}-windows-x64-setup.exe"
    binary = args.root / setup["name"]
    observed = fetch(setup["browser_download_url"], setup["size"], binary)
    assert setup["digest"] == "sha256:"+observed
    archive = asset(qa.ARCHIVE)
    assert archive["digest"] == "sha256:"+descriptor["offer"]["archiveSha256"]
    result = {"source": expected_sha, "tag": tag, "releaseId": api["id"], "immutable": True,
              "testServer": False, "authenticatedGithubRequests": False,
              "baselineChange": "compile-time update sequence only", "layouts": []}
    qa.save(args.evidence / "real-release.json", result)
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\GaussianSceneWorkbench\Gaussian Scene Workbench\ui") as key:
        winreg.SetValueEx(key, "language", 0, winreg.REG_SZ, "en_US")
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\GaussianSceneWorkbench\Gaussian Scene Workbench\updates") as key:
        for name, value in (("channel","preview"),("automaticCheck","true"),("prepareForNextLaunch","true")):
            winreg.SetValueEx(key, name, 0, winreg.REG_SZ, value)
    installed_image = qa.digest(args.payload / "bin" / qa.APP)
    winner = qa.digest(args.payload / "bin" / qa.HELPER)
    assert winner != qa.digest(args.baseline) and b"GSW_GITHUB_PREVIEW_FIXTURE" not in args.baseline.read_bytes()
    for managed in (True, False):
        label = "managed" if managed else "fixed"
        root = args.root / (label + " public 日本語 中文"); root.mkdir()
        if managed:
            old, new = root / "versions/fixture-a", root / "versions" / descriptor["offer"]["buildId"]
            link, inspector = args.root / "Managed official entry.lnk", root / "bin" / qa.LAUNCHER
            qa.seed_managed(root, args.payload, old, link)
        else:
            old = new = root
            group = "GSW Published Preview " + os.environ["GITHUB_RUN_ID"]
            subprocess.run([str(binary), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
                            "/DIR="+str(root), "/GROUP="+group], check=True, timeout=240)
            link = Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs" / group / "Gaussian Splatting Workbench.lnk"
            inspector = args.payload / "bin" / qa.LAUNCHER
        target = qa.link_info(inspector, link)
        old_helper = old / "bin" / qa.HELPER
        shutil.copy2(args.baseline, old_helper)
        (old / "gsw-preview-sequence.txt").write_text(str(seq-1), encoding="ascii")
        good = root / "saved-user-project.fixture"; good.write_bytes(b"public-readback-preserved-data")
        old_exe, new_exe = old / "bin" / qa.APP, new / "bin" / qa.APP
        proc = qa.launch(link, old_exe)
        cache = root / ".gsw-github-preview"
        marker = cache / "prepared.json"
        qa.wait(lambda: marker.is_file(), 300, "real public download and verification")
        receipt = qa.load(marker)
        expected_bytes = descriptor["offer"]["archiveSha256"] if managed else observed
        assert receipt["buildId"] == descriptor["offer"]["buildId"] and receipt["sha256"] == expected_bytes
        assert qa.digest(cache / (expected_bytes + (".zip" if managed else ".exe"))) == expected_bytes
        qa.close(proc, old_exe, accept=False)
        assert qa.main_window(old_exe)[1] == proc[1] and qa.digest(old_helper) == qa.digest(args.baseline)
        qa.close(proc, old_exe)
        qa.wait(lambda: not any(w[1]==proc[1] for w in qa.windows(old_exe)),120,"accepted close")
        if managed:
            qa.wait(lambda: qa.load(root / "current.json")["version"] == receipt["buildId"], 120, "public managed activation")
            assert qa.load(root / "updates/receipts" / (receipt["buildId"]+".json"))["mode"] == "github-preview"
        else:
            done = cache / ("setup-result-" + observed + ".json")
            assert qa.wait(lambda: qa.load(done) if done.exists() else None, 300, "public Setup handoff")["exitCode"] == 0
        assert qa.digest(new_exe) == installed_image and qa.digest(new / "bin" / qa.HELPER) == winner
        assert qa.link_info(inspector, link) == target and good.read_bytes() == b"public-readback-preserved-data"
        proc_b = qa.launch(link, new_exe)
        assert proc[1] != proc_b[1]
        qa.close(proc_b, new_exe)
        qa.wait(lambda: not any(w[1]==proc_b[1] for w in qa.windows(new_exe)), 120, "new app normal exit")
        result["layouts"].append({"layout":label,"oldPid":proc[1],"newPid":proc_b[1],"source":"real-github",
                                  "sameEntry":True,"userDataPreserved":True,"cancelledExitKeptA":True,
                                  "assetSha256":expected_bytes,"exeSha256":installed_image,"helperSha256":winner})
        qa.save(args.evidence / "real-release.json", result)
    result["passed"] = True
    qa.save(args.evidence / "real-release.json", result)


if __name__ == "__main__":
    main()
