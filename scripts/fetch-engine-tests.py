#!/usr/bin/env python3
"""Fetch the pinned public ELFs, expected output and ISC license; never execute archive scripts."""
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]

def fetch():
    lock = json.loads((ROOT / "deps.lock.json").read_text())["ps2native_engine"]["ps2autotests"]
    archive = ROOT / "out/dependencies/ps2autotests.tar.gz"
    if archive.exists():
        data = archive.read_bytes()
    else:
        with urllib.request.urlopen(lock["url"], timeout=30) as response:
            data = response.read(4 << 20)
    if hashlib.sha256(data).hexdigest() != lock["sha256"]:
        raise RuntimeError("ps2autotests archive hash mismatch; preserving existing files")
    target = ROOT / "out/ps2autotests"
    verified = {}
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        prefix = "ps2autotests-" + lock["commit"] + "/"
        for name, expected in lock["files"].items():
            if Path(name).is_absolute() or ".." in Path(name).parts:
                raise RuntimeError("Unsafe locked path")
            member = tar.getmember(prefix + name)
            if not member.isfile() or member.size > 2 << 20:
                raise RuntimeError("Invalid archive member: " + name)
            content = tar.extractfile(member).read()
            if hashlib.sha256(content).hexdigest() != expected:
                raise RuntimeError("Test input hash mismatch: " + name)
            verified[name] = content
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        archive.write_bytes(data)
    for name, content in verified.items():
        path = target / name
        if path.exists() and path.read_bytes() == content:
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    print(f"Verified {sum(n.endswith('.elf') for n in verified)} ELF/expected pairs and ISC license at {lock['commit']}")
    return target

if __name__ == "__main__":
    fetch()
