"""Build the unsigned public GitHub preview transport from an accepted matching Setup and native payload.

This command creates files only. It does not decide CI eligibility, upload a
Release, use signing credentials, or install anything.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_native_update import PRODUCT, create_archive, validate_relative_path  # noqa: E402

STARTER = "gsw-windows-x64-preview-start.zip"
APP = "bin/Gaussian Scene Workbench.exe"
LAUNCHER = "bin/Gaussian Scene Workbench Launcher.exe"
START_HELP = """Gaussian Splatting Workbench — Managed Preview

English: Extract this archive into a new empty folder. Open bin/Gaussian Scene Workbench Launcher.exe, not the executable inside versions. In Help → Software Updates, select Preview and enable automatic download to apply verified updates on normal exit. Do not extract over an existing installation.

简体中文：解压到一个全新的空目录，从 bin/Gaussian Scene Workbench Launcher.exe 启动，不要直接运行 versions 里的程序。在「帮助 → 软件更新」选预览版并开启自动下载后，正常退出时应用已校验的更新。不要覆盖现有安装。

日本語：新規の空フォルダーに展開し、versions 内ではなく bin/Gaussian Scene Workbench Launcher.exe から起動してください。「ヘルプ → ソフトウェア更新」でプレビュー版を選び、自動ダウンロードを有効にすると通常終了時に検証済みの更新が適用されます。既存のインストールには上書きしないでください。
"""


def checksum(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_starter(update, destination, package, executable_hash):
    """Assemble a first-use root from the verified managed archive, never an existing install."""
    expected_managed = package["archiveSha256"]
    if checksum(update) != expected_managed or destination.exists() or destination.is_symlink():
        raise ValueError("starter requires an unchanged archive and a fresh output")
    slot = package["buildId"]
    validate_relative_path(slot)
    if "/" in slot:
        raise ValueError("starter version must be a single directory")
    with zipfile.ZipFile(update) as origin:
        raw_manifest = origin.read("release-files.json")
        if hashlib.sha256(raw_manifest).hexdigest() != package["payloadManifestSha256"]:
            raise ValueError("starter archive manifest changed")
        manifest = json.loads(raw_manifest)
        rows = {row["path"]: row for row in manifest["files"]}
        if (manifest["buildId"] != slot or LAUNCHER not in rows or len(rows) != package["fileCount"]
                or rows[APP]["sha256"] != executable_hash
                or set(origin.namelist()) != set(rows) | {"release-files.json"}):
            raise ValueError("starter payload identity mismatch")
        forbidden = {"bootstrap.json", "current.json", ".update-trust-anchor", "gsw-installation.ini",
                     "updates", "versions", ".gsw-github-preview"}
        if any(path.split("/", 1)[0].casefold() in forbidden for path in rows):
            raise ValueError("starter accepts only a portable build, not an existing installation")
        bootstrap_rows = [row for name, row in rows.items() if name.startswith("bin/") and name != APP]
        bootstrap = json.dumps({"schema": "gsw.native_bootstrap/1", "protocol": 1,
                                "files": bootstrap_rows}, ensure_ascii=False, separators=(",", ":")).encode()
        if len(bootstrap) > 16384:
            raise ValueError("starter bootstrap manifest exceeds the launcher limit")
        metadata = {"bootstrap.json": bootstrap, "current.json": json.dumps(
            {"schema": "gsw.native_active_version/1", "version": slot, "executableSha256": executable_hash},
            separators=(",", ":")).encode(), "START-HERE.txt": START_HELP.encode()}
        expected_files = {}
        with zipfile.ZipFile(destination, "x", zipfile.ZIP_DEFLATED) as starter:
            for name, row in rows.items():
                destinations = [f"versions/{slot}/{name}"]
                if name.startswith("bin/") and name != APP:
                    destinations.append(name)
                for relative in destinations:
                    validate_relative_path(relative)
                    entry = zipfile.ZipInfo(relative)
                    entry.compress_type = zipfile.ZIP_DEFLATED
                    entry.file_size = row["size"]
                    with origin.open(name) as src, starter.open(entry, "w") as out:
                        shutil.copyfileobj(src, out, 1024**2)
                    expected_files[relative] = (row["size"], row["sha256"])
            for name, content in metadata.items():
                starter.writestr(name, content)
                expected_files[name] = (len(content), hashlib.sha256(content).hexdigest())
    with zipfile.ZipFile(destination) as starter:
        if len(starter.infolist()) != len(expected_files) or set(starter.namelist()) != set(expected_files):
            raise ValueError("starter file set mismatch")
        for entry in starter.infolist():
            size, digest = expected_files[entry.filename]
            with starter.open(entry) as src:
                if entry.file_size != size or hashlib.file_digest(src, "sha256").hexdigest() != digest:
                    raise ValueError("starter file verification failed: " + entry.filename)
    if checksum(update) != expected_managed:
        raise ValueError("starter source archive changed during packaging")
    return checksum(destination)


def build(root, setup, output, commit, tree):
    sequence = int((root / "gsw-preview-sequence.txt").read_text(encoding="ascii").strip())
    if not re.fullmatch(r"20\d{8}", str(sequence)) or not 1 <= sequence % 100 <= 99:
        raise ValueError("invalid native preview sequence")
    if not (root / "bin/GSW GitHub Preview Update.exe").is_file():
        raise ValueError("this binary predates GitHub preview updating")
    version = json.loads((root / "build_manifest.json").read_text(encoding="utf-8-sig"))["version"]
    if not re.fullmatch(r"\d{1,3}\.\d{1,3}\.\d{1,3}-native-preview", version):
        raise ValueError("only a Windows native preview is eligible")
    tag = f"v{version}.{sequence // 100}.{sequence % 100}"
    source_exe = root / "bin/Gaussian Scene Workbench.exe"
    report = json.loads(Path(str(setup) + ".json").read_text(encoding="utf-8-sig"))
    if (setup.name != f"Gaussian-Scene-Workbench-{version}-windows-x64-setup.exe" or report["appId"] != PRODUCT
            or report["testBuild"] or report["installerFixture"] or report["version"] != version
            or report["executableSha256"] != checksum(source_exe)
            or report["setupBytes"] != setup.stat().st_size or report["setupSha256"] != checksum(setup)):
        raise ValueError("Setup/readback does not match the native package")
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise ValueError("use a fresh release staging directory")
    archive = output / "gsw-windows-x64-managed.zip"
    build_id = f"preview-{sequence}-{commit[:12]}"
    pack = create_archive(root, archive, product=PRODUCT, source_commit=commit, source_tree=tree,
                          build_id=build_id, display_version=version, release_sequence=sequence)
    starter_sha = write_starter(archive, output / STARTER, pack, report["executableSha256"])
    (output / (STARTER + ".sha256")).write_text(f"{starter_sha}  {STARTER}\n", encoding="ascii")
    offer = {key: value for key, value in pack.items() if key not in ("schema", "archivePath")}
    offer.update(schema="gsw.update_offer/1", minimumBootstrapProtocol=1, channel="preview")
    manifest = {"schema": "gsw.github_preview/1", "product": PRODUCT, "platform": "windows-x64",
                "tag": tag, "sequence": sequence, "sourceCommit": commit, "setup": setup.name,
                "archive": archive.name, "offer": offer}
    with (output / "gsw-windows-x64-preview.json").open("x", encoding="utf-8") as file:
        json.dump(manifest, file, ensure_ascii=False, sort_keys=True, indent=2)
    return {"tag": tag, "buildId": build_id, "sourceCommit": commit, "sourceTree": tree,
            "starterSha256": starter_sha,
            "setupSha256": report["setupSha256"], "archiveSha256": pack["archiveSha256"],
            "manifestSha256": checksum(output / "gsw-windows-x64-preview.json")}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--setup", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    git = lambda ref: subprocess.check_output(["git", "rev-parse", "--verify", ref], text=True).strip()
    if not re.fullmatch(r"[a-f0-9]{40}", args.commit) or git("HEAD") != args.commit:
        parser.error("stage only from the exact checked-out candidate")
    print(json.dumps(build(args.package, args.setup, args.output, args.commit, git("HEAD^{tree}")), indent=2))
