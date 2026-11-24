"""Read back a just-uploaded GitHub Actions ZIP by exact ID, never by a run's list.

CI only. Check the upload output, REST metadata and raw archive before extraction.
This does not replace the independent package/source byte verifier.
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
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener
import zipfile


def load_archive_contract():
    # The QA interpreter uses -I, so load the repository's own path rules explicitly.
    path = Path(__file__).with_name("package_native_update.py")
    spec = importlib.util.spec_from_file_location("gsw_artifact_archive_contract", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReadbackError(ValueError):
    pass


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def require(condition, code):
    if not condition:
        raise ReadbackError(code)


def open_response(opener, url, headers, expected_status, deadline, *, wait_for_ready=False):
    # A just-finalized ID can take a moment to become visible. Never retry
    # authorization, expiry, identity, digest or archive validation failures.
    while (remaining := deadline - time.monotonic()) > 0:
        try:
            response = opener.open(Request(url, headers=headers), timeout=min(15, remaining))
        except HTTPError as error:
            response = error  # A documented /zip response is a 302.
        except (URLError, TimeoutError):
            raise ReadbackError("artifact_connection_failed") from None
        status = response.getcode()
        if status == expected_status:
            return response
        response.close()
        if not wait_for_ready or status not in {404, 502, 503, 504}:
            raise ReadbackError(f"artifact_http_{status}")
        print(f"Waiting for the exact GitHub artifact (HTTP {status})", flush=True)
        time.sleep(min(3, max(0, deadline - time.monotonic())))
    raise ReadbackError("artifact_readiness_deadline")


def long_path(path):
    if os.name == "nt" and not str(path).startswith("\\\\?\\"):
        return Path("\\\\?\\" + str(path))
    return path


def extract_verified_zip(path, output, contract):
    output.mkdir()
    root = output.resolve(strict=True)
    with zipfile.ZipFile(path) as archive:
        members = archive.infolist()
        require(0 < len(members) <= contract.MAX_FILES, "artifact_entry_count_invalid")
        require(sum(entry.file_size for entry in members) <= contract.MAX_UNPACKED,
                "artifact_expanded_limit")
        aliases, seen = {}, set()
        for entry in members:
            name = contract.validate_relative_path(entry.filename)
            kind = stat.S_IFMT(entry.external_attr >> 16)
            require(name == entry.orig_filename and name not in seen
                    and ".git" not in name.casefold().split("/")
                    and not entry.is_dir() and not entry.flag_bits & 1
                    and kind in (0, stat.S_IFREG), "artifact_entry_invalid")
            contract._claim_file(aliases, name)
            seen.add(name)
        for entry in members:
            target = (root / entry.filename).resolve()
            require(target != root and target.is_relative_to(root), "artifact_path_outside_output")
            target = long_path(target)
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(entry) as source, target.open("xb") as destination:
                shutil.copyfileobj(source, destination, 1024 * 1024)


def checkout_identity(env, metadata):
    workflow = metadata.get("workflow_run") or {}
    source = env["GITHUB_SHA"]
    allowed = {source}
    # On PRs the hosted checkout may be the merge SHA while the run metadata
    # identifies the PR head. Exact run, repo, upload ID and digest still bind it.
    if env.get("GITHUB_EVENT_NAME") == "pull_request":
        pull_head = env.get("GSW_ARTIFACT_PR_HEAD_SHA", "")
        if re.fullmatch(r"[0-9a-f]{40}", pull_head):
            allowed.add(pull_head)
    require(workflow.get("id") == int(env["GITHUB_RUN_ID"])
            and workflow.get("repository_id") == int(env["GITHUB_REPOSITORY_ID"])
            and re.fullmatch(r"[0-9a-f]{40}", source)
            and workflow.get("head_sha") in allowed, "artifact_run_identity_mismatch")


def readback(args, env):
    require(env.get("GITHUB_ACTIONS") == "true"
            and env.get("RUNNER_ENVIRONMENT") == "github-hosted"
            and env.get("GITHUB_SERVER_URL") == "https://github.com",
            "artifact_requires_hosted_github_actions")
    repository = env["GITHUB_REPOSITORY"]
    artifact_id = env["GSW_ARTIFACT_ID"]
    digest = env["GSW_ARTIFACT_DIGEST"].removeprefix("sha256:")
    require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository)
            and re.fullmatch(r"[1-9][0-9]{0,18}", artifact_id)
            and re.fullmatch(r"[0-9a-f]{64}", digest)
            and env.get("GH_TOKEN"), "artifact_upload_identity_missing")
    contract = load_archive_contract()
    endpoint = f"https://api.github.com/repos/{repository}/actions/artifacts/{artifact_id}"
    headers = {"Authorization": f"Bearer {env['GH_TOKEN']}",
               "Accept": "application/vnd.github+json",
               "X-GitHub-Api-Version": "2022-11-28",
               "User-Agent": "GSW-CI-artifact-readback"}
    opener = build_opener(NoRedirect())
    ready_by = time.monotonic() + 60
    with open_response(opener, endpoint, headers, 200, ready_by, wait_for_ready=True) as response:
        metadata = json.loads(response.read(64 * 1024))
    require(metadata["id"] == int(artifact_id) and metadata["name"] == args.name
            and metadata["expired"] is False, "artifact_metadata_mismatch")
    checkout_identity(env, metadata)
    require(metadata["digest"] == f"sha256:{digest}", "artifact_digest_mismatch")
    size = metadata["size_in_bytes"]
    require(type(size) is int and 0 < size <= contract.MAX_ARCHIVE, "artifact_size_invalid")

    output = args.output
    require(output.is_absolute() and ".." not in output.parts and not os.path.lexists(output),
            "artifact_output_not_fresh")
    parent = output.parent
    anchor = next(path for path in (parent, *parent.parents) if path.exists())
    contract._ordinary(anchor)
    parent.mkdir(parents=True, exist_ok=True)
    contract._ordinary(parent)
    require(output.resolve().parent == parent.resolve(strict=True), "artifact_output_alias")
    with open_response(opener, endpoint + "/zip", headers, 302, ready_by, wait_for_ready=True) as response:
        location = response.headers["Location"]
    url = urlsplit(location)
    require(url.scheme == "https" and url.hostname and not url.username and not url.password
            and url.port in (None, 443) and not url.fragment, "artifact_storage_url_invalid")

    with tempfile.TemporaryDirectory(prefix="gsw-artifact-readback-", dir=parent) as directory:
        work = Path(directory)
        require(work.resolve(strict=True).parent == parent.resolve(strict=True), "artifact_staging_alias")
        archive = work / "provider.zip"
        received, checksum = 0, hashlib.sha256()
        download_by = time.monotonic() + 240
        # The signed storage URL is returned by the authenticated GitHub API.
        # Never forward the GitHub token or print the signed URL.
        with open_response(opener, location, {}, 200, download_by) as response, archive.open("xb") as stream:
            while block := response.read(1024 * 1024):
                received += len(block)
                require(received <= size and time.monotonic() < download_by, "artifact_transfer_limit")
                checksum.update(block)
                stream.write(block)
        require(received == size and checksum.hexdigest() == digest, "artifact_archive_integrity_mismatch")
        expanded = work / "expanded"
        extract_verified_zip(archive, expanded, contract)
        require(not os.path.lexists(output), "artifact_output_not_fresh")
        expanded.rename(output)

    print(json.dumps({"artifactId": int(artifact_id), "runId": int(env["GITHUB_RUN_ID"]),
                      "sourceCommit": env["GITHUB_SHA"], "sha256": digest, "bytes": size,
                      "archiveVerification": "PASS"}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--output", type=Path, required=True)
    try:
        readback(parser.parse_args(), os.environ)
        return 0
    except ReadbackError as error:
        print(str(error))
        return 2
    except (OSError, ValueError, KeyError, TypeError, AttributeError, zipfile.BadZipFile, StopIteration):
        # Exceptions from a response may contain a signed URL; keep these private.
        print("artifact_readback_invalid_response_or_io")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
