"""Cache matching public Windows PDBs on D: for a hosted symbol-only replay."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import struct
import time
from urllib.request import urlopen
import uuid

SYSTEM_IMAGES = (
    "ntdll", "kernel32", "kernelbase", "shell32", "shdocvw", "windows.storage",
    "user32", "win32u", "combase", "ole32", "urlmon", "winhttp", "wininet",
    "iexframe", "iertutil", "apphelp", "apprepapi",
)


def pdb_index(module):
    image = module.read_bytes()
    (pe,) = struct.unpack_from("<I", image, 0x3C)
    if image[pe:pe + 4] != b"PE\0\0":
        return None
    sections, = struct.unpack_from("<H", image, pe + 6)
    opt_size, = struct.unpack_from("<H", image, pe + 20)
    opt = pe + 24
    if image[opt:opt + 2] != b"\x0b\x02" or opt_size < 168:
        return None
    debug_va, debug_size = struct.unpack_from("<II", image, opt + 112 + 6 * 8)
    if not debug_va or not debug_size:
        return None
    for i in range(sections):
        _, va, size, offset = struct.unpack_from("<IIII", image, opt + opt_size + i * 40 + 8)
        if not va <= debug_va < va + size:
            continue
        debug_start = offset + debug_va - va
        for pos in range(debug_start, min(debug_start + debug_size, len(image)) - 27, 28):
            kind, length, _, start = struct.unpack_from("<IIII", image, pos + 12)
            if kind != 2 or length < 25 or start + length > len(image) or image[start:start + 4] != b"RSDS":
                continue
            codeview_path = image[start + 24:start + length].split(b"\0", 1)[0]
            name = re.split(rb"[\\/]", codeview_path)[-1].decode("ascii")
            if not re.fullmatch(r"[a-zA-Z0-9_.-]+\.pdb", name, flags=re.I):
                continue
            guid = uuid.UUID(bytes_le=image[start + 4:start + 20]).hex.upper()
            age, = struct.unpack_from("<I", image, start + 20)
            return name, f"{guid}{age:X}"
    return None


def prefetch(image, directory, deadline):
    try:
        index = pdb_index(image)
        if index is None or time.monotonic() >= deadline:
            return False
        name, key = index
        destination = directory / name
        with urlopen(f"https://msdl.microsoft.com/download/symbols/{name}/{key}/{name}", timeout=8) as response:
            if not response.url.startswith("https://"):
                return False
            temporary = destination.with_suffix(".download")
            total = 0
            with temporary.open("xb") as out:
                while True:
                    if time.monotonic() >= deadline or total > 36 * 1024**2:
                        return False
                    data = response.read(128 * 1024)
                    if not data:
                        break
                    if not total and not data.startswith(b"Microsoft C/C++ MSF"):
                        return False
                    total += len(data)
                    out.write(data)
            if total and total <= 36 * 1024**2:
                temporary.replace(destination)
                return True
    except (OSError, UnicodeError, ValueError, struct.error):
        # Missing public symbols are not grounds for a broad capture or
        # reporting a nearest, nonmatching export as a resolved frame.
        return False
    return False


if __name__ == "__main__":
    assert os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted"
    system = Path(os.environ["SystemRoot"]) / "System32"
    directory = Path("D:/gsw-preview-qa/symbols")
    directory.mkdir(parents=True)
    deadline = time.monotonic() + 48
    with ThreadPoolExecutor(max_workers=6) as pool:
        count = sum(pool.map(lambda n: prefetch(system / (n + ".dll"), directory, deadline), SYSTEM_IMAGES))
    print(f"Fetched {count} matching Windows public symbol files (not uploaded).")
