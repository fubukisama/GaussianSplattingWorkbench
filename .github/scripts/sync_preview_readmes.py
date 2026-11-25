"""Verify an accepted public managed preview, then render its pinned README links."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import io
import json
import os
from pathlib import Path
import re
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener
import zipfile


REPO = "fubukisama/GaussianSplattingWorkbench"
REPO_ID = 1286130219
API = f"https://api.github.com/repos/{REPO}"
BASE = f"https://github.com/{REPO}/releases"
PAGES = ("README.md", "README.zh-CN.md", "README.ja.md", "README_RELEASE.md")
STARTER = "gsw-windows-x64-preview-start.zip"
MANAGED = "gsw-windows-x64-managed.zip"
SUMS = "SHA256SUMS"
RECEIPT = "live-public-preview-readback"
STEP = "Upgrade both original installed entries from the actual public release"
PUBLIC_HOSTS = {"api.github.com", "github.com", "release-assets.githubusercontent.com",
                "objects.githubusercontent.com"}
TAG = r"v([0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}-native-preview)\.(20[0-9]{6})\.([1-9][0-9]?)"
LINK = re.compile(r'(?:href="|\]\()(' + re.escape(BASE) + r'/(?:download|tag)/[^")\s]+)')
SHA256 = r"[0-9a-f]{64}"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def tag_parts(tag):
    match = re.fullmatch(TAG, tag)
    require(match, "Expected an exact versioned managed preview tag")
    version, date, counter = match.groups()
    return version, int(date) * 100 + int(counter)


def commit_sha(sha):
    require(isinstance(sha, str) and re.fullmatch(r"[0-9a-f]{40}", sha), "Invalid source commit")
    return sha


def download(tag, name):
    return f"{BASE}/download/{tag}/{name}"


def setup_name(tag):
    return f"Gaussian-Scene-Workbench-{tag_parts(tag)[0]}-windows-x64-setup.exe"


@dataclass(frozen=True)
class AcceptedPreview:
    tag: str
    source: str

    def __post_init__(self):
        tag_parts(self.tag)
        commit_sha(self.source)


def https_url(url):
    parts = urlsplit(url)
    require(parts.scheme == "https" and parts.hostname and not parts.username and not parts.password
            and parts.port in (None, 443) and not parts.fragment, "Unexpected GitHub transport URL")
    return parts


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


class PublicRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        require(https_url(newurl).hostname in PUBLIC_HOSTS, "Unexpected public release download host")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


class GitHub:
    def __init__(self, token):
        require(token, "An Actions read token is required")
        self.headers = {"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json"}
        self.direct = build_opener(NoRedirect())
        self.public = build_opener(PublicRedirect())

    @staticmethod
    def read(opener, url, cap, headers=None):
        request = Request(url, headers={"User-Agent": "GSW-preview-README-sync", **(headers or {})})
        with opener.open(request, timeout=20) as response:
            data = response.read(cap + 1)
        require(len(data) <= cap, "GitHub response exceeds the readback size bound")
        return data

    def api(self, path):
        return json.loads(self.read(self.direct, f"{API}/{path}", 4 * 1024**2, self.headers))

    def public_json(self, path):
        return json.loads(self.public_bytes(f"{API}/{path}", 1024**2))

    def public_bytes(self, url, cap):
        require(https_url(url).hostname in PUBLIC_HOSTS, "Unexpected public GitHub resource")
        return self.read(self.public, url, cap)

    def artifact(self, asset_id, size):
        require(type(asset_id) is int and asset_id > 0, "Invalid receipt artifact ID")
        endpoint = f"{API}/actions/artifacts/{asset_id}/zip"
        try:
            with self.direct.open(Request(endpoint, headers=self.headers), timeout=20):
                raise ValueError("Expected GitHub's artifact storage redirect")
        except HTTPError as error:
            with error:
                require(error.code == 302, f"Receipt archive unavailable (HTTP {error.code})")
                location = error.headers["Location"]
        https_url(location)
        # The API returned a signed storage URL. Never forward the Actions token
        # to storage, follow another redirect, or extract the ZIP.
        return self.read(self.direct, location, size)


def read_receipt(github, run):
    endpoint = f"actions/runs/{run['id']}/artifacts?per_page=100"
    artifacts = [row for row in github.api(endpoint)["artifacts"] if row["name"] == RECEIPT]
    require(len(artifacts) == 1, "Expected one exact public readback artifact")
    artifact = artifacts[0]
    binding = artifact["workflow_run"]
    length = artifact["size_in_bytes"]
    require(artifact["expired"] is False and binding["id"] == run["id"]
            and binding["repository_id"] == REPO_ID and binding["head_repository_id"] == REPO_ID
            and binding["head_sha"] == run["head_sha"]
            and type(length) is int and 0 < length <= 1024**2, "Readback artifact identity mismatch")
    data = github.artifact(artifact["id"], length)
    require(len(data) == length and artifact["digest"] == "sha256:" + hashlib.sha256(data).hexdigest(),
            "Readback artifact bytes do not match GitHub's digest")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        receipts = [member for member in archive.infolist() if member.orig_filename == "real-release.json"]
        require(len(receipts) == 1 and 0 < receipts[0].file_size <= 64 * 1024,
                "Missing or oversized Windows release receipt")
        with archive.open(receipts[0]) as stream:
            return json.loads(stream.read(64 * 1024 + 1))


def release_assets(github, candidate, report):
    tag = candidate.tag
    release = github.public_json(f"releases/tags/{tag}")
    require(release["id"] == report["releaseId"] and release["tag_name"] == tag
            and release["html_url"] == f"{BASE}/tag/{tag}" and release["published_at"]
            and release["draft"] is False and release["prerelease"] is True
            and release["immutable"] is True, "Acceptance is not for this public immutable preview release")
    ref = github.public_json(f"git/ref/tags/{tag}")
    require(ref["ref"] == f"refs/tags/{tag}", "Unexpected public Git tag")
    obj = ref["object"]
    for _ in range(5):
        if obj["type"] != "tag":
            break
        obj = github.public_json(f"git/tags/{commit_sha(obj['sha'])}")["object"]
    require(obj["type"] == "commit" and obj["sha"] == candidate.source,
            "Public tag does not resolve to the accepted source commit")

    layouts = {entry["layout"]: entry for entry in report["layouts"]}
    require(len(report["layouts"]) == 2 and set(layouts) == {"managed", "fixed"}
            and all(entry["source"] == "real-github" and entry["sameEntry"] is True
                    and entry["userDataPreserved"] is True for entry in layouts.values())
            and all(layouts["managed"][key] is True for key in (
                "starterFirstLaunch", "previousSlotRetained", "cancelledExitKeptA"))
            and layouts["fixed"]["manualSetup"] is True and layouts["fixed"]["noAutomaticSetup"] is True,
            "Successful managed first-launch/upgrade and classic readbacks are required")
    expected = {STARTER: report["starterSha256"], MANAGED: layouts["managed"]["assetSha256"],
                setup_name(tag): layouts["fixed"]["assetSha256"]}
    assets = {row["name"]: row for row in release["assets"]}
    require(len(assets) == len(release["assets"]), "Duplicate public asset name")
    for name in (*expected, SUMS):
        asset = assets.get(name, {})
        require(asset.get("browser_download_url") == download(tag, name) and asset.get("state") == "uploaded"
                and type(asset["size"]) is int and asset["size"] > 0, "Missing exact release asset: " + name)
        digest = asset["digest"].removeprefix("sha256:")
        require(re.fullmatch(SHA256, digest) and asset["digest"] == "sha256:" + digest
                and (name == SUMS or expected[name] == digest), "Asset digest is not from acceptance: " + name)
    sums = assets[SUMS]
    raw = github.public_bytes(download(tag, SUMS), 64 * 1024)
    require(len(raw) == sums["size"] and sums["digest"] == "sha256:" + hashlib.sha256(raw).hexdigest(),
            "Public SHA256SUMS does not match its release digest")
    lines = raw.decode("ascii").splitlines()
    for name, digest in expected.items():
        matching = [re.fullmatch(r"(" + SHA256 + r")[ \t]+\*?" + re.escape(name), line) for line in lines]
        require([m[1] for m in matching if m] == [digest], "Public checksum does not match acceptance: " + name)


def accepted_preview(github, run_id, *, attempt=None):
    require(type(run_id) is int and run_id > 0, "Bad acceptance run ID")
    endpoint = f"actions/runs/{run_id}"
    run = github.api(endpoint)
    require(run["id"] == run_id and run["repository"]["id"] == REPO_ID and run["head_repository"]["id"] == REPO_ID
            and run["path"] == ".github/workflows/github-preview-live.yml" and run["event"] == "workflow_dispatch"
            and run["status"] == "completed" and run["conclusion"] == "success", "Not a successful public readback run")
    run_attempt = run["run_attempt"]
    require(type(run_attempt) is int and run_attempt > 0 and (attempt is None or run_attempt == attempt),
            "Readback attempt changed before verification")
    commit_sha(run["head_sha"])
    jobs = github.api(f"{endpoint}/attempts/{run_attempt}/jobs?per_page=100")["jobs"]
    good_jobs = [job for job in jobs if job["name"] == "live-public-readback"
                 and job["status"] == "completed" and job["conclusion"] == "success"]
    require(len(good_jobs) == 1, "Exact successful Windows readback job not found")
    steps = {step["name"]: step["conclusion"] for step in good_jobs[0]["steps"]}
    require(steps.get("Validate readback identity") == "success", "Missing exact-source native run validation")
    if steps.get(STEP) == "skipped":
        return None  # CDN-only success is not installation or upgrade acceptance.
    require(steps.get(STEP) == "success", "Published Windows first-install and upgrade did not pass")
    report = read_receipt(github, run)
    require(report["passed"] is True and report["immutable"] is True and report["testServer"] is False
            and report["authenticatedGithubRequests"] is False, "No successful anonymous public acceptance receipt")
    candidate = AcceptedPreview(report["tag"], report["source"])
    require(run["head_branch"] == "main" or
            (run["head_branch"] == candidate.tag and run["head_sha"] == candidate.source),
            "Readback did not run from main or the exact immutable release tag")
    release_assets(github, candidate, report)
    return candidate


def link_urls(tag, guide):
    links = [download(tag, STARTER), download(tag, SUMS), f"{BASE}/tag/{tag}"]
    return links + [download(tag, setup_name(tag))] if guide else links


def plan_updates(documents, candidate):
    """Return all four new documents, or none. Validate every locale before writing."""
    require(set(documents) == set(PAGES), "Expected exactly the four public download documents")
    seen = set()
    result = {}
    for name in PAGES:
        guide = name == "README_RELEASE.md"
        # Keep the headings themselves unchanged in the output.
        parts = (re.split(r"(^## (?:English|简体中文|日本語)[ \t]*\r?$)", documents[name], flags=re.M)
                 if guide else [documents[name]])
        require(len(parts) == (7 if guide else 1), "Missing localized guide sections")
        indexes = (2, 4, 6) if guide else (0,)
        for index in indexes:
            block = parts[index]
            links = LINK.findall(block)
            match = (re.fullmatch(re.escape(f"{BASE}/download/") + r"([^/]+)/" + re.escape(STARTER), links[0])
                     if links else None)
            require(match, "First release download is not the managed starter in " + name)
            old_tag = match[1]
            require(links == link_urls(old_tag, guide), "Unexpected release link layout in " + name)
            label = re.compile(r"(?<=[(（])[0-9a-f]{7}(?=[)）]\]\(" + re.escape(links[2]) + r"\))")
            commits = label.findall(block)
            require(len(commits) == 1, "Missing exact release source label in " + name)
            seen.add((old_tag, commits[0]))
            block = label.sub(candidate.source[:7], block)
            replacements = dict(zip(links, link_urls(candidate.tag, guide)))
            parts[index] = LINK.sub(lambda m: m[0].removesuffix(m[1]) + replacements[m[1]], block)
        result[name] = "".join(parts)
    require(len(seen) == 1, "Current release destinations are inconsistent; refusing partial updates")
    old_tag, old_source = seen.pop()
    current_sequence, new_sequence = tag_parts(old_tag)[1], tag_parts(candidate.tag)[1]
    if current_sequence == new_sequence:
        require((old_tag, old_source) == (candidate.tag, candidate.source[:7]),
                "Conflicting release at the already published sequence")
    return result if new_sequence > current_sequence else {}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    verify = commands.add_parser("verify")
    verify.add_argument("--run", type=int, required=True)
    verify.add_argument("--attempt", type=int)
    verify.add_argument("--github-output", type=Path, required=True)
    stage = commands.add_parser("stage")
    stage.add_argument("--tag", required=True)
    stage.add_argument("--source", required=True)
    args = parser.parse_args()
    if args.command == "verify":
        candidate = accepted_preview(GitHub(os.environ.get("GH_TOKEN")), args.run, attempt=args.attempt)
        if candidate is None:
            print("CDN-only readback: README destinations unchanged.")
        else:
            with args.github_output.open("a", encoding="utf-8") as stream:
                stream.write(f"tag={candidate.tag}\nsource={candidate.source}\n")
            print(f"Verified public {candidate.tag} ({candidate.source}), Windows readback run {args.run}.")
    else:
        documents = {name: Path(name).read_bytes().decode("utf-8") for name in PAGES}
        edited = plan_updates(documents, AcceptedPreview(args.tag, args.source))
        for name, text in edited.items():
            Path(name).write_bytes(text.encode("utf-8"))
        print("Staged four synchronized documentation edits." if edited else "README destinations are current or newer.")


if __name__ == "__main__":
    try:
        main()
    except (URLError, OSError, KeyError, TypeError, AttributeError, UnicodeError,
            json.JSONDecodeError, zipfile.BadZipFile):
        # Transport exceptions may contain a signed storage URL.
        sys.exit("Could not verify the GitHub evidence or read/write the expected documentation")
    except ValueError as error:
        sys.exit(str(error))
