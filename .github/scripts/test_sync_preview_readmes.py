"""Regression tests at the readback and public download-link boundaries."""
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import unittest
import zipfile

from sync_preview_readmes import AcceptedPreview, accepted_preview, plan_updates


REPO = "fubukisama/GaussianSplattingWorkbench"
BASE = f"https://github.com/{REPO}/releases"
STARTER = "gsw-windows-x64-preview-start.zip"
MANAGED = "gsw-windows-x64-managed.zip"
SETUP = "Gaussian-Scene-Workbench-0.4.2-native-preview-windows-x64-setup.exe"
TAG = "v0.4.2-native-preview.20300501.2"
SOURCE = "a1" * 20
ACCEPTANCE_STEP = "Upgrade both original installed entries from the actual public release"
RUN = "actions/runs/123"


class ReadbackApi:
    """GitHub HTTP boundary, with realistic immutable metadata and receipt ZIP."""
    def __init__(self):
        self.report = {
            "source": SOURCE, "tag": TAG, "releaseId": 456, "immutable": True, "passed": True,
            "testServer": False, "authenticatedGithubRequests": False, "starterSha256": "a" * 64,
            "layouts": [
                {"layout": "managed", "source": "real-github", "assetSha256": "b" * 64,
                 "starterFirstLaunch": True, "sameEntry": True, "userDataPreserved": True,
                 "previousSlotRetained": True, "cancelledExitKeptA": True},
                {"layout": "fixed", "source": "real-github", "assetSha256": "c" * 64,
                 "manualSetup": True, "noAutomaticSetup": True,
                 "sameEntry": True, "userDataPreserved": True},
            ],
        }
        self.sums = "".join(f"{value * 64}  {name}\n" for name, value in
                            ((STARTER, "a"), (MANAGED, "b"), (SETUP, "c"))).encode()
        url = f"{BASE}/download/{TAG}"
        self.release = {
            "id": 456, "tag_name": TAG, "immutable": True, "draft": False, "prerelease": True,
            "published_at": "2030-05-01T01:00:00Z", "html_url": f"{BASE}/tag/{TAG}",
            "assets": [
                {"name": name, "state": "uploaded", "size": size, "digest": f"sha256:{digest}",
                 "browser_download_url": f"{url}/{name}"}
                for name, size, digest in (
                    (STARTER, 111, "a" * 64), (MANAGED, 222, "b" * 64), (SETUP, 333, "c" * 64),
                    ("SHA256SUMS", len(self.sums), hashlib.sha256(self.sums).hexdigest()),
                )
            ],
        }
        self.run = {
            "id": 123, "repository": {"id": 1286130219}, "head_repository": {"id": 1286130219},
            "head_sha": SOURCE, "head_branch": TAG, "event": "workflow_dispatch",
            "path": ".github/workflows/github-preview-live.yml", "run_attempt": 2,
            "status": "completed", "conclusion": "success",
        }
        self.release_step = "success"
        self.tag_commit = SOURCE
        self.bad_artifact_digest = False

    def zip_bytes(self):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            archive.writestr("cdn.log", "read-only CDN report")
            archive.writestr("real-release.json", json.dumps(self.report))
        return data.getvalue()

    def api(self, path):
        if path == RUN:
            return self.run
        if path == RUN + "/attempts/2/jobs?per_page=100":
            return {"jobs": [{"name": "live-public-readback", "status": "completed", "conclusion": "success",
                              "steps": [{"name": "Validate readback identity", "conclusion": "success"},
                                        {"name": ACCEPTANCE_STEP, "conclusion": self.release_step}]}]}
        if path == RUN + "/artifacts?per_page=100":
            data = self.zip_bytes()
            return {"artifacts": [{"name": "live-public-preview-readback", "id": 321, "expired": False,
                                    "size_in_bytes": len(data), "digest": "sha256:" + (
                                        "f" * 64 if self.bad_artifact_digest else hashlib.sha256(data).hexdigest()),
                                    "workflow_run": {"id": 123, "repository_id": 1286130219,
                                                     "head_repository_id": 1286130219,
                                                     "head_sha": SOURCE}}]}
        raise AssertionError(f"unexpected authenticated API request: {path}")

    def artifact(self, asset_id, size):
        assert asset_id == 321
        return self.zip_bytes()

    def public_json(self, path):
        if path == f"releases/tags/{TAG}":
            return self.release
        if path == f"git/ref/tags/{TAG}":
            return {"ref": f"refs/tags/{TAG}", "object": {"type": "commit", "sha": self.tag_commit}}
        raise AssertionError(f"unexpected public API request: {path}")

    def public_bytes(self, url, cap):
        assert url == f"{BASE}/download/{TAG}/SHA256SUMS"
        return self.sums


class AcceptedReleaseChecks(unittest.TestCase):
    def test_accepted_public_readback_returns_exact_release(self):
        self.assertEqual(accepted_preview(ReadbackApi(), 123, attempt=2), AcceptedPreview(TAG, SOURCE))

    def test_cdn_check_cannot_advertise_new_release(self):
        api = ReadbackApi()
        api.release_step = "skipped"
        self.assertIsNone(accepted_preview(api, 123))

    def test_wrong_or_incomplete_evidence_cannot_advertise_new_release(self):
        for bad in ("run failure", "superseded attempt", "other repository", "failed released step",
                    "bad artifact", "incomplete receipt", "draft", "mutable", "different release",
                    "wrong tag commit", "missing starter", "starter digest", "wrong checksum"):
            with self.subTest(bad=bad):
                api = ReadbackApi()
                attempt = 2
                if bad == "run failure":
                    api.run["conclusion"] = "failure"
                elif bad == "superseded attempt":
                    attempt = 1
                elif bad == "other repository":
                    api.run["head_repository"]["id"] = 99
                elif bad == "failed released step":
                    api.release_step = "failure"
                elif bad == "bad artifact":
                    api.bad_artifact_digest = True
                elif bad == "incomplete receipt":
                    api.report["layouts"].pop()
                elif bad == "draft":
                    api.release["draft"] = True
                elif bad == "mutable":
                    api.release["immutable"] = False
                elif bad == "different release":
                    api.release["id"] = 457
                elif bad == "wrong tag commit":
                    api.tag_commit = "f" * 40
                elif bad == "missing starter":
                    del api.release["assets"][0]
                elif bad == "starter digest":
                    api.release["assets"][0]["digest"] = "sha256:" + "d" * 64
                else:
                    api.sums = api.sums.replace(b"a" * 64, b"d" * 64)
                    api.release["assets"][-1]["digest"] = "sha256:" + hashlib.sha256(api.sums).hexdigest()
                with self.assertRaises(ValueError):
                    accepted_preview(api, 123, attempt=attempt)


PAGES = ("README.md", "README.zh-CN.md", "README.ja.md", "README_RELEASE.md")
PREVIOUS_TAG = "v0.4.1-native-preview.20300430.8"
PREVIOUS = AcceptedPreview(PREVIOUS_TAG, "def4567" + "0" * 33)
NEXT = AcceptedPreview(TAG, SOURCE)


def fixture():
    first = f"{BASE}/download/{PREVIOUS_TAG}"
    names = ("First launch (def4567)", "首次启动（def4567）", "初回起動（def4567）")
    pages = {page: f'<a href="{first}/{STARTER}">ZIP</a> · <a href="{first}/SHA256SUMS">SHA-256</a>\n'
                   f"[{name}]({BASE}/tag/{PREVIOUS_TAG})\nBody and links stay intact.\n"
             for page, name in zip(PAGES, names)}
    pages["README_RELEASE.md"] = "\n".join(
        f"## {locale}\n\n[ZIP]({first}/{STARTER})\n\n[SHA-256]({first}/SHA256SUMS) · "
        f"[{label}]({BASE}/tag/{PREVIOUS_TAG}) · "
        f"[Manual]({first}/Gaussian-Scene-Workbench-0.4.1-native-preview-windows-x64-setup.exe)\n"
        for locale, label in zip(("English", "简体中文", "日本語"), names)
    )
    return pages


class DownloadLinkChecks(unittest.TestCase):
    def test_new_accepted_build_updates_all_downloads_without_rewriting_prose(self):
        current = fixture()
        edited = plan_updates(current, NEXT)
        self.assertEqual(set(edited), set(PAGES))
        for index, file in enumerate(PAGES):
            repeats = 3 if index == 3 else 1
            self.assertEqual(edited[file].count(f"{BASE}/download/{TAG}/{STARTER}"), repeats)
            self.assertEqual(edited[file].count(f"{BASE}/download/{TAG}/SHA256SUMS"), repeats)
            self.assertEqual(edited[file].count(f"{BASE}/tag/{TAG}"), repeats)
            self.assertEqual(edited[file].count(SOURCE[:7]), repeats)
            self.assertNotIn(PREVIOUS_TAG, edited[file])
        self.assertIn("Body and links stay intact.", edited["README.zh-CN.md"])
        self.assertEqual(edited["README_RELEASE.md"].count(f"/{SETUP})"), 3)
        self.assertEqual(plan_updates(edited, NEXT), {})
        self.assertEqual(plan_updates(edited, PREVIOUS), {})

    def test_inconsistent_download_contract_leaves_every_page_untouched(self):
        current = fixture()
        current["README.ja.md"] = current["README.ja.md"].replace(PREVIOUS_TAG, TAG, 1)
        baseline = deepcopy(current)
        with self.assertRaises(ValueError):
            plan_updates(current, NEXT)
        self.assertEqual(current, baseline)

    def test_same_sequence_with_another_source_is_not_a_replacement(self):
        with self.assertRaises(ValueError):
            plan_updates(fixture(), AcceptedPreview(PREVIOUS_TAG, SOURCE))

    def test_checked_in_pages_have_a_consistent_starter_first_contract(self):
        directory = Path(__file__).resolve().parents[2]
        pages = {path: (directory / path).read_text(encoding="utf-8") for path in PAGES}
        prospective = AcceptedPreview("v9.9.9-native-preview.20990101.1", SOURCE)
        self.assertEqual(set(plan_updates(pages, prospective)), set(PAGES))


if __name__ == "__main__":
    unittest.main()
