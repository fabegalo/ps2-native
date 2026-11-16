#!/usr/bin/env python3
"""Verify the isolated GS source and pinned Windows WebGPU package; no installer is run."""
import hashlib
import io
import json
import os
from pathlib import Path
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]

def digest(data):
    return hashlib.sha256(data).hexdigest()

def verify_sources():
    engine=json.loads((ROOT/"deps.lock.json").read_text(encoding="utf-8"))["ps2native_engine"]
    for name,expected in engine["gs"]["files"].items():
        path=ROOT/"third_party/ps2-native-gs"/name
        if digest(path.read_bytes().replace(b"\r\n",b"\n"))!=expected:
            raise RuntimeError("GS component hash mismatch: "+name)
    for name,expected in engine["wgpu"]["notices"].items():
        if digest((ROOT/"third_party/wgpu-native"/name).read_bytes().replace(b"\r\n",b"\n"))!=expected:
            raise RuntimeError("WebGPU license hash mismatch: "+name)
    return engine

def fetch():
    engine=verify_sources()
    if os.name!="nt":
        print("GS source verified; supply a host build of wgpu-native v29.0.1.1 via PS2N_ENGINE_WGPU")
        return
    lock=engine["wgpu"]["windows_x64_gnu"]
    target=ROOT/lock["directory"]
    valid=lambda: all((target/name).is_file() and digest((target/name).read_bytes())==expected for name,expected in lock["files"].items())
    if valid():
        print("Verified GS compute source and wgpu-native v29.0.1.1 package")
        return
    archive=ROOT/lock["archive"]
    if archive.exists():data=archive.read_bytes()
    else:
        with urllib.request.urlopen(lock["url"],timeout=30) as response:
            data=response.read(lock["archive_bytes"]+1)
    if len(data)!=lock["archive_bytes"] or digest(data)!=lock["sha256"]:
        raise RuntimeError("WebGPU archive hash mismatch; existing files preserved")
    verified={}
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for name,expected in lock["files"].items():
            if Path(name).is_absolute() or ".." in Path(name).parts:
                raise RuntimeError("Invalid locked archive path")
            info=z.getinfo(name)
            if info.file_size>lock["source_bytes"]:raise RuntimeError("WebGPU archive member limit")
            content=z.read(name)
            if digest(content)!=expected:raise RuntimeError("WebGPU member hash mismatch: "+name)
            verified[name]=content
    if sum(map(len,verified.values()))>lock["source_bytes"]:raise RuntimeError("WebGPU extracted size limit")
    archive.parent.mkdir(parents=True,exist_ok=True)
    if not archive.exists():archive.write_bytes(data)
    for name,content in verified.items():
        path=target/name;path.parent.mkdir(parents=True,exist_ok=True)
        temporary=path.with_suffix(path.suffix+".tmp");temporary.write_bytes(content);temporary.replace(path)
    if not valid():raise RuntimeError("WebGPU package verification failed")
    print("Verified GS compute source and wgpu-native v29.0.1.1 package")

if __name__=="__main__":
    fetch()
