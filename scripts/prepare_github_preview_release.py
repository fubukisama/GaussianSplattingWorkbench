"""Build the unsigned public GitHub preview transport from an accepted matching Setup and native payload.

This command creates files only. It does not decide CI eligibility, upload a
Release, use signing credentials, or install anything.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_native_update import PRODUCT, create_archive  # noqa: E402


def checksum(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


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
    offer = {key: value for key, value in pack.items() if key not in ("schema", "archivePath")}
    offer.update(schema="gsw.update_offer/1", minimumBootstrapProtocol=1, channel="preview")
    manifest = {"schema": "gsw.github_preview/1", "product": PRODUCT, "platform": "windows-x64",
                "tag": tag, "sequence": sequence, "sourceCommit": commit, "setup": setup.name,
                "archive": archive.name, "offer": offer}
    with (output / "gsw-windows-x64-preview.json").open("x", encoding="utf-8") as file:
        json.dump(manifest, file, ensure_ascii=False, sort_keys=True, indent=2)
    return {"tag": tag, "buildId": build_id, "sourceCommit": commit, "sourceTree": tree,
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
