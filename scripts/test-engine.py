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
    parser.add_argument("tests", nargs="*", help="EE short names or locked paths such as cpu/ee_fpu/transfer")
    parser.add_argument("--cmake", default=shutil.which("cmake"))
    parser.add_argument("--cxx", default=shutil.which("clang++"))
    parser.add_argument("--cc", default=shutil.which("clang"))
    parser.add_argument("--ninja", default=shutil.which("ninja"))
    parser.add_argument("--with-iop", help="Run this locked IRX concurrently with an EE ELF")
    parser.add_argument("--report", default="results.json", help="Summary filename inside the build directory")
    parser.add_argument("--build", type=Path, default=ROOT / "out/engine-build")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare-only", action="store_true")
    mode.add_argument("--build-only", action="store_true")
    options = parser.parse_args()
    if not all((options.cmake, options.cxx, options.cc, options.ninja)):
        parser.error("Provide CMake, Ninja, clang++ and clang paths")
    components = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))["ps2native_engine"]
    for component, directory in (("aot","ps2-native-recomp"),("iop","ps2recomp-iop"),("vu","ps2-native-vu")):
        for name, expected in components[component]["files"].items():
            data = (ROOT / "third_party" / directory / name).read_bytes().replace(b"\r\n", b"\n")
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError(component + " component hash mismatch: " + name)
    spec = importlib.util.spec_from_file_location("engine_fetch", ROOT / "scripts/fetch-engine-tests.py")
    fetcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(fetcher)
    tests = fetcher.fetch() / "tests"
    build = options.build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    configure = [options.cmake, "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DPS2N_ENGINE_ONLY=ON", f"-DCMAKE_CXX_COMPILER={Path(options.cxx).as_posix()}",
                 f"-DCMAKE_C_COMPILER={Path(options.cc).as_posix()}", f"-DCMAKE_MAKE_PROGRAM={Path(options.ninja).as_posix()}"]
    checked(configure)
    checked([options.cmake, "--build", build, "--target", "ps2n-ee-recomp", "--parallel", "2"])
    suffix = ".exe" if os.name == "nt" else ""
    checked([options.cmake, "--build", build, "--target", "ps2n-engine-tests", "ps2n-engine-aot-tests", "ps2n-engine-iop-tests", "ps2n-engine-vu-tests", "ps2n-iop-run", "--parallel", "2"])
    checked([build / ("ps2n-engine-tests" + suffix)])
    checked([build / ("ps2n-engine-aot-tests" + suffix)])
    checked([build / ("ps2n-engine-iop-tests" + suffix)])
    checked([build / ("ps2n-engine-vu-tests" + suffix)])
    vu_spec=importlib.util.spec_from_file_location("prepare_engine_vu",ROOT/"scripts/prepare-engine-vu.py")
    vu_preparer=importlib.util.module_from_spec(vu_spec);vu_spec.loader.exec_module(vu_preparer)
    summary = {}
    locked = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))["ps2native_engine"]["ps2autotests"]["files"]
    if options.with_iop and "tests/" + options.with_iop + ".irx" not in locked:
        parser.error("--with-iop must select a locked IRX")
    if Path(options.report).name != options.report:
        parser.error("--report must be a filename")
    for requested in options.tests or EE:
        relative = "cpu/ee/" + requested if requested in EE else requested
        extension = ".elf" if "tests/" + relative + ".elf" in locked else ".irx"
        if "tests/" + relative + extension not in locked:
            parser.error("Not a hash-verified ELF/IRX: " + relative)
        if extension == ".irx" and options.with_iop:
            parser.error("--with-iop requires EE primary tests")
        name = requested.replace("/", "-")
        elf = tests / (relative + extension)
        identity = hashlib.sha256(elf.read_bytes()).hexdigest()
        generated = build / "aot" / identity
        manifest = generated / "captured.json"
        modules = json.loads(manifest.read_text(encoding="utf-8")) if manifest.exists() else []
        if len(modules) > 32:
            raise RuntimeError("Captured AOT module limit exceeded")
        missing = build / "missing"
        for attempt in range(33):
            if extension == ".irx":
                if options.prepare_only or options.build_only:
                    print(name + ": interpreter component linked (not executed)", flush=True); break
                code, actual, diagnostic = run([build / ("ps2n-iop-run" + suffix), elf], timeout=15)
                break
            prepare = [build / ("ps2n-ee-recomp" + suffix), elf, "--out", generated]
            for module in modules:
                if module["file"] != module["sha256"] + ".bin" or not re.fullmatch(r"[0-9a-f]{64}", module["sha256"]):
                    raise RuntimeError("Invalid AOT cache identity")
                image = generated / module["file"]
                if hashlib.sha256(image.read_bytes()).hexdigest() != module["sha256"]:
                    raise RuntimeError("Captured AOT image hash mismatch")
                prepare += ["--module", image, f"{module['base']:08x}", f"{module['entry']:08x}"]
            checked(prepare)
            vu_capture=generated/"vu-capture"
            vu_count=vu_preparer.prepare(vu_capture,generated)
            if options.prepare_only:
                print(name + ": AOT generated (not executed)", flush=True); break
            checked(configure + [f"-DPS2N_ENGINE_ELF={elf.as_posix()}", f"-DPS2N_ENGINE_GENERATED={generated.as_posix()}",
                                 f"-DPS2N_ENGINE_ELF_SHA256={identity}"])
            checked([options.cmake, "--build", build, "--target", "ps2n-engine-run", "--parallel", "2"])
            if options.build_only:
                print(name + ": native AOT linked (not executed)", flush=True); break
            environment = dict(os.environ, PS2N_ENGINE_AOT_MISSING_DIR=str(missing),PS2N_ENGINE_VU_CAPTURE_DIR=str(vu_capture))
            command = [build / ("ps2n-engine-run" + suffix)]
            joint_output = build / "joint-iop.actual"
            if options.with_iop:
                command += ["--iop", tests / (options.with_iop + ".irx"), "--iop-output", joint_output]
            code, actual, diagnostic = run(command, timeout=15, env=environment)
            if code==0 and len(list(vu_capture.glob("vu[01]-*.bin")))>vu_count and attempt<32:
                print(name+": preparing captured VU AOT",flush=True)
                continue
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
        expected = (tests / (relative + ".expected")).read_bytes().replace(b"\r\n", b"\n")
        passed = code == 0 and actual == expected
        joint = {}
        if options.with_iop and code == 0:
            observed_iop = joint_output.read_bytes().replace(b"\r\n",b"\n")
            expected_iop = (tests / (options.with_iop + ".expected")).read_bytes().replace(b"\r\n",b"\n")
            timing = re.search(rb"\[joint\] ee_finish=(\d+) iop_first=(\d+) iop_last=(\d+) iop_instructions=(\d+)",diagnostic)
            interleaved = bool(timing and int(timing[2]) < int(timing[1]) and int(timing[4]) > 0)
            passed = passed and observed_iop == expected_iop and interleaved
            joint = {"iop_test": options.with_iop, "iop_stdout_sha256": hashlib.sha256(observed_iop).hexdigest(),
                     "iop_expected_sha256": hashlib.sha256(expected_iop).hexdigest(), "interleaved": interleaved}
        summary[name] = {"passed": passed, "exit_code": code, "elf_sha256": identity,
                         "captured_aot_entries": len(modules),
                         "stdout_sha256": hashlib.sha256(actual).hexdigest(), "expected_sha256": hashlib.sha256(expected).hexdigest(),
                         "diagnostic": diagnostic.decode(errors="replace")[:2000],
                         "vu_aot_images": vu_count if extension==".elf" else 0, **joint}
        (build / (name + ".actual")).write_bytes(actual)
        print(f"{name}: {'PASS' if passed else 'FAIL'} ({len(actual)}/{len(expected)} bytes)", flush=True)
        if not passed:
            print(diagnostic.decode(errors="replace"))
            print("".join(list(difflib.unified_diff(expected.decode().splitlines(True), actual.decode(errors="replace").splitlines(True)))[:30]))
    if summary:
        (build / options.report).write_text(json.dumps(summary, indent=2) + "\n")
    return int(any(not r["passed"] for r in summary.values()))

if __name__ == "__main__":
    raise SystemExit(main())
