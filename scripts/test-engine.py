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
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
EE = ("lsudelay", "branchdelay", "alu", "branch", "lsu", "muldiv")

def run(args, timeout=180):
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        child = subprocess.Popen(list(map(str, args)), stdout=out, stderr=err, cwd=ROOT)
        start = time.monotonic()
        try:
            while child.poll() is None:
                if time.monotonic() - start > timeout or os.fstat(out.fileno()).st_size + os.fstat(err.fileno()).st_size > 1 << 20:
                    raise RuntimeError("Child timeout/output limit: " + str(args[0]))
                time.sleep(0.05)
        except BaseException:
            child.kill(); child.wait(); raise
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
    checked([options.cmake, "--build", build, "--target", "ps2n-engine-tests", "--parallel", "2"])
    checked([build / ("ps2n-engine-tests" + suffix)])
    summary = {}
    for name in options.tests or EE:
        elf = tests / (name + ".elf")
        identity = hashlib.sha256(elf.read_bytes()).hexdigest()
        generated = build / "aot" / identity
        checked([build / ("ps2n-ee-recomp" + suffix), elf, "--out", generated])
        if options.prepare_only:
            print(name + ": AOT generated (not executed)"); continue
        checked(configure + [f"-DPS2N_ENGINE_ELF={elf.as_posix()}", f"-DPS2N_ENGINE_GENERATED={generated.as_posix()}",
                             f"-DPS2N_ENGINE_ELF_SHA256={identity}"])
        checked([options.cmake, "--build", build, "--target", "ps2n-engine-run", "--parallel", "2"])
        if options.build_only:
            print(name + ": native AOT linked (not executed)", flush=True); continue
        code, actual, diagnostic = run([build / ("ps2n-engine-run" + suffix)], timeout=15)
        # Only host text-mode CRLF conversion is normalized; all guest text is compared.
        actual = actual.replace(b"\r\n", b"\n")
        expected = (tests / (name + ".expected")).read_bytes().replace(b"\r\n", b"\n")
        passed = code == 0 and actual == expected
        summary[name] = {"passed": passed, "exit_code": code, "elf_sha256": identity,
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
