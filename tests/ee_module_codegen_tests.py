"""Flat EE input uses the existing analyzer and emits isolated, guarded native code."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile

tool = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(dir=sys.argv[2] if len(sys.argv) > 2 else None) as tmp:
    root = Path(tmp)
    image = struct.pack('<4I', 0x2402002a, 0x03e00008, 0, 0)
    identity = hashlib.sha256(image).hexdigest()
    (root / 'image.bin').write_bytes(image)

    def run(base=0x200000, entry=0x200000, name=identity, extra='', ok=True):
        (root / 'config.toml').write_text(
            '[general]\ninput="image.bin"\noutput="generated"\n'
            f'module_id="{name}"\n{extra}\n[raw_image]\n'
            f'load_address={base}\nentry={entry}\n')
        result = subprocess.run([tool, 'config.toml'], cwd=root, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == ok, result.stdout + result.stderr
        if ok:
            return {p.name: (p.read_bytes(), p.stat().st_mtime_ns)
                    for p in (root / 'generated').iterdir() if p.suffix in ('.h', '.cpp')}

    first = run()
    registration = first['register_functions.cpp'][0]
    assert f'g_ps2EeAot_{identity}'.encode() in registration
    assert b'g_ps2RecompiledFunctionTable' not in registration
    assert b'2097152u,0u,image,sizeof(image)' in registration
    sources = b'\n'.join(data for name, (data, _) in first.items()
                         if name.endswith('.cpp') and name != 'register_functions.cpp')
    assert f'void ee_{identity}_'.encode() in sources
    assert b'ps2CheckEeAotInstruction(rdram, 0x200000u, 0x2402002au)' in sources
    assert b'ps2CheckEeAotInstruction(rdram, 0x200008u, 0x0u)' in sources
    assert b'#include <ps2_recompiled_functions.h>' not in sources
    assert run() == first, 'Repeated raw preparation rewrote unchanged outputs'
    run(base=-1, ok=False)
    run(base=0x1fffffc, entry=0x1fffffc, ok=False)
    run(entry=0x200002, ok=False)
    run(entry=0x200010, ok=False)
    run(name='../invalid', ok=False)
    run(name='', ok=False)
    run(extra='single_file_output=true', ok=False)
    run(extra='skip=["entry@0x00200000"]', ok=False)
    (root / 'image.bin').write_bytes(image[:-1])
    run(ok=False)
    (root / 'image.bin').write_bytes(struct.pack('<3I', 0x74000000, 0x03e00008, 0))
    run(ok=False)
    # An indirect entry is prepared through the existing entry_points contract.
    indirect = struct.pack('<7I', 0x00800008, 0, 0x74000000, 0x74000000,
                           0x2402002a, 0x03e00008, 0)
    learned_identity = hashlib.sha256(indirect).hexdigest()
    (root / 'image.bin').write_bytes(indirect)
    initial = run(name=learned_identity)
    learned = run(name=learned_identity, extra='entry_points=["entry@0x00200010"]')
    original_function = next(name for name in initial if name.endswith('_0x200000.cpp'))
    assert initial[original_function] == learned[original_function], 'Entry learning rewrote unrelated code'
    assert any(name.endswith('_0x200010.cpp') for name in learned), 'Indirect entry was not prepared'
    if len(sys.argv) > 4:
        # Exercise the SDK build interface: learning must not shift unity groups.
        cmake, sdk = sys.argv[3:5]
        build = root / 'build'
        (root / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(ee_groups LANGUAGES CXX)\n'
            'add_library(ps2_runtime INTERFACE)\n'
            f'include("{Path(sdk).as_posix()}/ps2xRuntime/cmake/PS2EEAot.cmake")\n'
            'ps2x_add_ee_aot_module(module "${CMAKE_CURRENT_SOURCE_DIR}/generated")\n')

        def configure():
            result = subprocess.run([cmake, '-S', str(root), '-B', str(build), '-G', 'Ninja'],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            return {p.name: p.read_bytes() for p in build.glob('CMakeFiles/module.dir/Unity/*.cxx')}

        groups = configure()
        assert groups, 'SDK did not generate unity groups'
        assert all(b'register_functions.cpp' not in data for data in groups.values())
        (root / 'generated/new_entry.cpp').write_text('void newly_learned_entry() {}\n')
        expanded = configure()
        changed = {name for name in groups.keys() | expanded.keys()
                   if groups.get(name) != expanded.get(name)}
        assert len(changed) == 1, f'Learning invalidated unrelated unity groups: {changed}'
print('PASS raw EE module identity, dispatch isolation, instruction/delay guards, cache and invalid inputs')
