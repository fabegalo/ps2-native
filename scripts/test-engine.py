#!/usr/bin/env python3
"""Original public ELF -> AOT C++ -> own engine -> exact reference comparison.

Requires CMake, Ninja and Clang with C++20. Reuses one Release build directory.
Each child is supervised with a timeout and a 1 MiB stdout/stderr limit.
"""
import argparse
import difflib
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
EE = ("lsudelay", "branchdelay", "alu", "branch", "lsu", "muldiv")

def run(args, timeout=180, env=None):
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        child = subprocess.Popen(list(map(str, args)), stdout=out, stderr=err, cwd=ROOT, env=env)
        start = time.monotonic()
        try:
            while child.poll() is None:
                if time.monotonic() - start > timeout or os.fstat(out.fileno()).st_size + os.fstat(err.fileno()).st_size > 1 << 20:
                    raise RuntimeError("Child timeout/output limit: " + str(args[0]))
                time.sleep(0.05)
        except BaseException as error:
            child.kill(); child.wait()
            err.seek(max(0, os.fstat(err.fileno()).st_size - 4096))
            if isinstance(error, RuntimeError):
                raise RuntimeError(str(error) + "\n" + err.read(4096).decode(errors="replace")) from error
            raise
        out.seek(0); err.seek(0)
        stdout, stderr = out.read(1 << 20), err.read(1 << 20)
    return child.returncode, stdout, stderr

def checked(args):
    code, out, err = run(args)
    if code:
        raise RuntimeError((out + err).decode(errors="replace")[-6000:])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tests", nargs="*", choices=EE)
    parser.add_argument("--cmake", default=shutil.which("cmake"))
    parser.add_argument("--cxx", default=shutil.which("clang++"))
    parser.add_argument("--cc", default=shutil.which("clang"))
    parser.add_argument("--ninja", default=shutil.which("ninja"))
    parser.add_argument("--build", type=Path, default=ROOT / "out/engine-build")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare-only", action="store_true")
    mode.add_argument("--build-only", action="store_true")
    options = parser.parse_args()
    if not all((options.cmake, options.cxx, options.cc, options.ninja)):
        parser.error("Provide CMake, Ninja, clang++ and clang paths")
    component = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))["ps2native_engine"]["aot"]
    for name, expected in component["files"].items():
        data = (ROOT / "third_party/ps2-native-recomp" / name).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError("AOT component hash mismatch: " + name)
    spec = importlib.util.spec_from_file_location("engine_fetch", ROOT / "scripts/fetch-engine-tests.py")
    fetcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(fetcher)
    tests = fetcher.fetch() / "tests/cpu/ee"
    build = options.build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    configure = [options.cmake, "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DPS2N_ENGINE_ONLY=ON", f"-DCMAKE_CXX_COMPILER={Path(options.cxx).as_posix()}",
                 f"-DCMAKE_C_COMPILER={Path(options.cc).as_posix()}", f"-DCMAKE_MAKE_PROGRAM={Path(options.ninja).as_posix()}"]
    checked(configure)
    checked([options.cmake, "--build", build, "--target", "ps2n-ee-recomp", "--parallel", "2"])
    suffix = ".exe" if os.name == "nt" else ""
    checked([options.cmake, "--build", build, "--target", "ps2n-engine-tests", "ps2n-engine-aot-tests", "--parallel", "2"])
    checked([build / ("ps2n-engine-tests" + suffix)])
    checked([build / ("ps2n-engine-aot-tests" + suffix)])
    summary = {}
    for name in options.tests or EE:
        elf = tests / (name + ".elf")
        identity = hashlib.sha256(elf.read_bytes()).hexdigest()
        generated = build / "aot" / identity
        manifest = generated / "captured.json"
        modules = json.loads(manifest.read_text(encoding="utf-8")) if manifest.exists() else []
        if len(modules) > 32:
            raise RuntimeError("Captured AOT module limit exceeded")
        missing = build / "missing"
        for attempt in range(33):
            prepare = [build / ("ps2n-ee-recomp" + suffix), elf, "--out", generated]
            for module in modules:
                if module["file"] != module["sha256"] + ".bin" or not re.fullmatch(r"[0-9a-f]{64}", module["sha256"]):
                    raise RuntimeError("Invalid AOT cache identity")
                image = generated / module["file"]
                if hashlib.sha256(image.read_bytes()).hexdigest() != module["sha256"]:
                    raise RuntimeError("Captured AOT image hash mismatch")
                prepare += ["--module", image, f"{module['base']:08x}", f"{module['entry']:08x}"]
            checked(prepare)
            if options.prepare_only:
                print(name + ": AOT generated (not executed)", flush=True); break
            checked(configure + [f"-DPS2N_ENGINE_ELF={elf.as_posix()}", f"-DPS2N_ENGINE_GENERATED={generated.as_posix()}",
                                 f"-DPS2N_ENGINE_ELF_SHA256={identity}"])
            checked([options.cmake, "--build", build, "--target", "ps2n-engine-run", "--parallel", "2"])
            if options.build_only:
                print(name + ": native AOT linked (not executed)", flush=True); break
            environment = dict(os.environ, PS2N_ENGINE_AOT_MISSING_DIR=str(missing))
            code, actual, diagnostic = run([build / ("ps2n-engine-run" + suffix)], timeout=15, env=environment)
            if code != 86 or attempt == 32:
                break
            address = re.search(rb"(?m)^\[engine-aot-missing\] ([0-9A-F]{8})", diagnostic)
            captured = json.loads((missing / "missing.json").read_text(encoding="utf-8"))
            raw = (missing / "image.bin").read_bytes()
            base, entry = captured["base"], captured["entry"]
            if not address or int(address[1], 16) != entry or len(raw) != 65536 or base & 65535 or entry & 3 or not base <= entry < base + 65536:
                raise RuntimeError("Invalid missing-code capture")
            digest = hashlib.sha256(raw).hexdigest()
            module = dict(file=digest + ".bin", sha256=digest, base=base, entry=entry)
            if module in modules:
                break  # No progress: keep the explicit missing-entry failure.
            image = generated / module["file"]
            if not image.exists():
                image.write_bytes(raw)
            modules.append(module)
            temporary = manifest.with_suffix(".tmp")
            temporary.write_text(json.dumps(modules, indent=2) + "\n", encoding="utf-8")
            temporary.replace(manifest)
            print(f"{name}: preparing captured AOT entry {entry:08X}", flush=True)
        if options.prepare_only or options.build_only:
            continue
        # Only host text-mode CRLF conversion is normalized; all guest text is compared.
        actual = actual.replace(b"\r\n", b"\n")
        expected = (tests / (name + ".expected")).read_bytes().replace(b"\r\n", b"\n")
        passed = code == 0 and actual == expected
        summary[name] = {"passed": passed, "exit_code": code, "elf_sha256": identity,
                         "captured_aot_entries": len(modules),
                         "stdout_sha256": hashlib.sha256(actual).hexdigest(), "expected_sha256": hashlib.sha256(expected).hexdigest(),
                         "diagnostic": diagnostic.decode(errors="replace")[:2000]}
        (build / (name + ".actual")).write_bytes(actual)
        print(f"{name}: {'PASS' if passed else 'FAIL'} ({len(actual)}/{len(expected)} bytes)", flush=True)
        if not passed:
            print(diagnostic.decode(errors="replace"))
            print("".join(list(difflib.unified_diff(expected.decode().splitlines(True), actual.decode(errors="replace").splitlines(True)))[:30]))
    if summary:
        (build / "results.json").write_text(json.dumps(summary, indent=2) + "\n")
    return int(any(not r["passed"] for r in summary.values()))

if __name__ == "__main__":
    raise SystemExit(main())
