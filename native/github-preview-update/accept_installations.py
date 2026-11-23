"""Remote-only black-box upgrade of both installed Windows layouts, using the real Workbench and Setup.

A is a controlled earlier updater revision: the real app/launcher, a distinct
fixture-transport helper and a lower installed sequence. B has the exact,
unmodified package and Setup bytes from this candidate's Native Windows run.
Never start this on a user's workstation, or publish the test transport.
"""
import argparse
import ctypes
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import winreg
import zipfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "scripts"))
from prepare_github_preview_release import STARTER, build, validate_relative_path

APP = "Gaussian Scene Workbench.exe"
LAUNCHER = "Gaussian Scene Workbench Launcher.exe"
HELPER = "GSW GitHub Preview Update.exe"
PRODUCT = "Fubukisama.GaussianSceneWorkbench.Native"
ARCHIVE = "gsw-windows-x64-managed.zip"
MANIFEST = "gsw-windows-x64-preview.json"
U, K = ctypes.windll.user32, ctypes.windll.kernel32
ENUM = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
K.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
K.OpenProcess.restype = w.HANDLE
K.CloseHandle.argtypes = [w.HANDLE]
K.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
K.WaitForSingleObject.restype = w.DWORD
K.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
U.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
U.IsWindowVisible.argtypes = [w.HWND]
U.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
U.GetWindow.argtypes = [w.HWND, w.UINT]
U.GetWindow.restype = w.HWND
U.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
U.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
U.EnumWindows.argtypes = [ENUM, w.LPARAM]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save(path, obj):
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def wait(probe, seconds, label):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            value = probe()
        except (FileNotFoundError, PermissionError, json.JSONDecodeError):
            value = None  # Atomic replacement or Setup still has the file open.
        if value:
            return value
        time.sleep(.35)
    raise RuntimeError("timed out: " + label)


def windows(exe):
    result = []
    @ENUM
    def each(hwnd, unused):
        if not U.IsWindowVisible(hwnd):
            return True
        pid = w.DWORD()
        U.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        handle = K.OpenProcess(0x1000, False, pid.value)
        if not handle:
            return True
        try:
            size, path, title = w.DWORD(32768), ctypes.create_unicode_buffer(32768), ctypes.create_unicode_buffer(1024)
            if K.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(size)) and Path(path.value) == exe:
                U.GetWindowTextW(hwnd, title, len(title))
                if title.value:
                    result.append((hwnd, pid.value, title.value))
        finally:
            K.CloseHandle(handle)
        return True
    U.EnumWindows(each, 0)
    return result


def main_window(exe):
    for row in windows(exe):
        rect = w.RECT()
        if (not U.GetWindow(row[0], 4) and U.GetWindowRect(row[0], ctypes.byref(rect))
                and rect.right - rect.left > 600 and rect.bottom - rect.top > 360):
            return row
    return None


def launch(link, exe):
    os.startfile(str(link))
    return wait(lambda: main_window(exe), 65, "real window at " + str(exe))


def close(window, exe, accept=True):
    assert U.PostMessageW(window[0], 0x0010, 0, 0), window  # WM_CLOSE, no termination
    def prompt():
        return next((r for r in windows(exe) if r[1] == window[1] and r[0] != window[0]
                     and (r[2].lower().startswith("confirm exit") or r[2].startswith(("确认退出", "終了の確認", "終了確認")))), None)
    dlg = wait(prompt, 20, "owned exit confirmation")
    if not accept:
        U.PostMessageW(dlg[0], 0x0100, 0x1B, 0); U.PostMessageW(dlg[0], 0x0101, 0x1B, 0)
    else:
        environment = dict(os.environ, GSW_QA_DIALOG=str(dlg[0]))
        action = """
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$el=[System.Windows.Automation.AutomationElement]::FromHandle([intptr][long]$env:GSW_QA_DIALOG)
$cond=New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Button)
$buttons=$el.FindAll([System.Windows.Automation.TreeScope]::Descendants,$cond)
foreach($b in $buttons){if($b.Current.Name.Replace('&','') -in @('Exit','Quit','退出','終了')){
  $b.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke();exit 0}}
throw ('Exit button absent: '+(($buttons|ForEach-Object {$_.Current.Name}) -join ','))
"""
        subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", action],
                       check=True, timeout=35, env=environment)
    wait(lambda: not prompt(), 20, "exit confirmation dismissed")


def helper_call(helper, op, env):
    proc = subprocess.run([str(helper), op, "preview"], capture_output=True, env=env, timeout=180)
    rows = [json.loads(line) for line in proc.stdout.splitlines() if line]
    assert rows, (proc.returncode, proc.stderr[-700:])
    return proc.returncode, rows[-1]


def link_info(launcher, link):
    out = subprocess.check_output([str(launcher), "--read-shortcut", str(link)], timeout=30)
    value = json.loads(out)
    assert value["valid"], value
    return {k: value[k] for k in ("target", "arguments", "workingDirectory")}


def feed_assets(feed, manifest, setup):
    tag = manifest["tag"]
    names = [MANIFEST, ARCHIVE, setup.name]
    assets = [{"name": name, "size": (feed / name).stat().st_size, "digest": "sha256:" + digest(feed / name),
               "state": "uploaded", "browser_download_url":
               f"https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/{tag}/{name}"} for name in names]
    save(feed / "releases.json", [{"tag_name": tag, "prerelease": True, "immutable": True, "draft": False, "assets": assets}])


def corrupt_case(layout, helper, payload, env, initial, marker):
    # Flip the served file, not its trusted metadata, the original CI artifact,
    # an installed file or a user file. Restore it for the forward transition.
    with payload.open("r+b") as f:
        original = f.read(1)
        f.seek(0); f.write(bytes([original[0] ^ 1]))
    try:
        rc, row = helper_call(helper, "--prepare", env)
        assert rc != 0 and row["code"] == "preview_package_digest_mismatch", row
        assert not marker.exists() and initial(), "refusal mutated " + layout
        return row["code"]
    finally:
        with payload.open("r+b") as f:
            f.write(original)


def install_starter(archive, root, package, version):
    assert not any(root.iterdir()) and "/" not in validate_relative_path(version)
    files = {f.relative_to(package).as_posix(): f for f in package.rglob("*") if f.is_file()}
    boot_files = {name: f for name, f in files.items() if name.startswith("bin/") and name != "bin/" + APP}
    copies = {f"versions/{version}/{name}": f for name, f in files.items()}
    copies.update(boot_files)
    with zipfile.ZipFile(archive) as starter:
        expected = set(copies) | {"bootstrap.json", "current.json", "START-HERE.txt"}
        assert len(starter.infolist()) == len(expected) and set(starter.namelist()) == expected
        for name in sorted(expected):
            validate_relative_path(name)
            output = root / name
            output.parent.mkdir(parents=True, exist_ok=True)
            with starter.open(name) as src, output.open("xb") as dst:
                shutil.copyfileobj(src, dst, 1024**2)
            if name in copies:
                assert digest(output) == digest(copies[name]), name
    bootstrap = load(root / "bootstrap.json")
    assert bootstrap == {"schema": "gsw.native_bootstrap/1", "protocol": 1,
                         "files": [{"path": n, "size": f.stat().st_size, "sha256": digest(f)}
                                   for n, f in sorted(boot_files.items())]}, bootstrap
    assert load(root / "current.json") == {"schema": "gsw.native_active_version/1", "version": version,
                                          "executableSha256": digest(package / "bin" / APP)}


def classic_setup(setup, root, group):
    subprocess.run([str(setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
                    "/DIR=" + str(root), "/GROUP=" + group], check=True, timeout=180)


def close_and_wait_for_process(window, exe, seconds):
    process = K.OpenProcess(0x00100000, False, window[1])  # SYNCHRONIZE
    assert process, "unable to observe the owned application process"
    try:
        close(window, exe)
        assert K.WaitForSingleObject(process, seconds * 1000) == 0, "owned application did not exit"
    finally:
        K.CloseHandle(process)


def layout_run(mode, args, package, setup, feed, manifest, env):
    root = args.root / (mode + " 日本語 中文")
    root.mkdir()
    is_managed = mode == "managed"
    expected_id = manifest["offer"]["buildId"]
    seq = manifest["sequence"]
    if is_managed:
        old = root / "versions/fixture-a"
        new = root / "versions" / expected_id
        link = args.root / "Original managed entry.lnk"
        install_starter(args.release_dir / STARTER, root, package, expected_id)
        inspector = root / "bin" / LAUNCHER
        subprocess.run([str(inspector), "--write-shortcut", str(link)], check=True, timeout=30)
        initial_exe = new / "bin" / APP
        first = launch(link, initial_exe)
        close_and_wait_for_process(first, initial_exe, 140)
        # Freeze an older controlled A using the same app bytes, not the
        # actual untouched starter/archive distributed as candidate B.
        new.rename(old)
        pointer = load(root / "current.json")
        pointer["version"] = old.name
        save(root / "current.json", pointer)
    else:
        old = new = root
        group = "GSW GitHub Preview QA " + os.environ["GITHUB_RUN_ID"]
        classic_setup(setup, root, group)
        link = Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs" / group / "Gaussian Splatting Workbench.lnk"
        inspector = package / "bin" / LAUNCHER
        assert link.is_file() and (root / "gsw-installation.ini").is_file(), link
    info_before = link_info(inspector, link)
    helper, exe_a, exe_b = old / "bin" / HELPER, old / "bin" / APP, new / "bin" / APP
    assert digest(helper) == digest(package / "bin" / HELPER)
    production_hash = digest(helper)
    shutil.copy2(args.fixture, helper)
    (helper.parent / "gsw-fixture-issuer.txt").write_text(str(feed), encoding="utf-8")
    (old / "gsw-preview-sequence.txt").write_text(str(seq - 1), encoding="ascii")
    assert digest(helper) != production_hash
    marker = root / ".gsw-github-preview/prepared.json"
    sentinel = root / "user-project-keep.gsw"
    sentinel.write_bytes(b"owned synthetic user data; never overwrite")
    sentinel_hash = digest(sentinel)
    original_pointer = (root / "current.json").read_bytes() if is_managed else None
    seed_hash = digest(helper)
    def before():
        return (digest(helper) == seed_hash and digest(sentinel) == sentinel_hash and
                (not is_managed or (root / "current.json").read_bytes() == original_pointer))
    if not is_managed:
        rc, row = helper_call(helper, "--check", env)
        assert rc == 0 and row["code"] == "GITHUB_PREVIEW_MANUAL" and row["offer"]["buildId"] == expected_id, row
        rc, row = helper_call(helper, "--prepare", env)
        assert rc != 0 and row["code"] == "preview_setup_manual_only" and not marker.parent.exists(), row
        # The classic app can have a marker left by the old implementation.
        # Closing with that stale marker must not initiate a Setup handoff.
        marker.parent.mkdir()
        save(marker, {"schema": "gsw.github_preview_prepared/1", "channel": "preview", "buildId": expected_id,
                      "descriptorSha256": digest(feed / MANIFEST), "sha256": digest(setup)})
        old_marker = digest(marker)
        a = launch(link, exe_a)
        close_and_wait_for_process(a, exe_a, 140)
        assert before() and list(marker.parent.iterdir()) == [marker] and digest(marker) == old_marker
        # Manual setup remains supported. This is a tester-initiated install
        # of its local CI-built artifact, not an updater launch or policy override.
        classic_setup(setup, root, group)
        assert not (root / "current.json").exists()
        for file_row in load(root / "installer-payload.json")["files"]:
            assert digest(root / file_row["path"]) == file_row["sha256"], file_row["path"]
        assert digest(exe_b) == digest(package / "bin" / APP) and digest(helper) == production_hash
        assert digest(sentinel) == sentinel_hash and info_before == link_info(inspector, link)
        b = launch(link, exe_b)
        close(b, exe_b)
        wait(lambda: not any(r[1] == b[1] for r in windows(exe_b)), 100, "manual Setup reopen exit")
        return {"layout": "fixed", "manualSetup": True, "noAutomaticSetup": True, "checkedUpdate": "GITHUB_PREVIEW_MANUAL",
                "sameOriginalEntry": True, "userDataUnchanged": True, "installedExecutableSha256": digest(exe_b)}
    bad = corrupt_case(mode, helper, feed / ARCHIVE, env, before, marker)
    a = launch(link, exe_a)
    assert wait(lambda: marker.is_file(), 190, "published-byte preparation while A remains open")
    assert load(marker)["buildId"] == expected_id and before() and main_window(exe_a)[1] == a[1]
    close(a, exe_a, accept=False)
    assert before() and main_window(exe_a)[1] == a[1] and marker.is_file()
    close(a, exe_a)
    wait(lambda: not any(r[1] == a[1] for r in windows(exe_a)), 140, "normal A exit")
    wait(lambda: load(root / "current.json")["version"] == expected_id, 120, "atomic B pointer")
    receipt = load(root / "updates/receipts" / (expected_id + ".json"))
    assert receipt["mode"] == "github-preview" and digest(helper) == seed_hash and exe_a.is_file()
    assert not (root / ".update-trust-anchor").exists()
    assert digest(new / "bin" / HELPER) == production_hash and digest(exe_b) == digest(package / "bin" / APP)
    assert digest(sentinel) == sentinel_hash and info_before == link_info(inspector, link)
    b = launch(link, exe_b)
    assert a[1] != b[1] and main_window(exe_b)[1] == b[1]
    close(b, exe_b)
    wait(lambda: not any(r[1] == b[1] for r in windows(exe_b)), 100, "normal B exit")
    return {"layout": mode, "sourceA": "real Workbench, controlled older updater/sequence",
            "sourceB": "unmodified Native Windows package", "oldPid": a[1], "newPid": b[1],
            "oldHelperSha256": seed_hash, "newHelperSha256": production_hash, "installedExecutableSha256": digest(exe_b),
            "activeBuild": expected_id, "sameOriginalEntry": True, "cancelledExitKeptA": True,
            "starterFirstLaunch": True, "previousSlotRetained": True,
            "corruptRefusal": bad, "userDataUnchanged": True}


def main():
    parser = argparse.ArgumentParser()
    for name in ("root", "package", "setup", "fixture", "release_dir", "evidence"):
        parser.add_argument("--" + name.replace("_", "-"), dest=name, type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted"
    assert not args.root.exists() and args.root.resolve().drive.lower() == "d:"
    args.root.mkdir(parents=True)
    args.evidence.mkdir(parents=True, exist_ok=True)
    head = os.environ["GITHUB_SHA"]
    tree = subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], text=True).strip()
    assert subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip() == head
    settings = r"Software\GaussianSceneWorkbench\Gaussian Scene Workbench\updates"
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\GaussianSceneWorkbench\Gaussian Scene Workbench\ui") as key:
        winreg.SetValueEx(key, "language", 0, winreg.REG_SZ, "en_US")
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, settings) as key:
        winreg.SetValueEx(key, "channel", 0, winreg.REG_SZ, "preview")
        for name in ("automaticCheck", "prepareForNextLaunch"):
            winreg.SetValueEx(key, name, 0, winreg.REG_SZ, "true")
    result = build(args.package, args.setup, args.release_dir, head, tree)
    manifest = load(args.release_dir / MANIFEST)
    feed = args.root / "owned-issuer"
    shutil.copytree(args.release_dir, feed)
    shutil.copy2(args.setup, feed / args.setup.name)
    feed_assets(feed, manifest, args.setup)
    os.environ["GSW_GITHUB_PREVIEW_FIXTURE"] = str(feed)
    # The shipped helper must consult real GitHub even with a test-only
    # environment left in place. All fixtures remain on this hosted runner.
    prod = args.root / "provider-check"
    (prod / "bin").mkdir(parents=True)
    shutil.copy2(args.package / "bin" / HELPER, prod / "bin" / HELPER)
    (prod / "gsw-installation.ini").write_text("[Installation]\nAppId=" + PRODUCT + "\n")
    rc, row = helper_call(prod / "bin" / HELPER, "--check", os.environ)
    assert rc == 0 and row["phase"] == "current", row
    result.update(realGithubReadback=row["code"], runId=os.environ["GITHUB_RUN_ID"], layouts=[])
    save(args.evidence / "result.json", result)
    for layout in ("managed", "fixed"):
        result["layouts"].append(layout_run(layout, args, args.package, args.setup, feed, manifest, os.environ))
        save(args.evidence / "result.json", result)
    result["passed"] = True
    save(args.evidence / "result.json", result)


if __name__ == "__main__":
    main()
