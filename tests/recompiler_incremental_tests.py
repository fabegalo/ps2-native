"""A stub-to-guest change must preserve shared headers and unchanged output timestamps."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def elf(words=(0x03e00008, 0)):
    strings = b'\0_start\0'
    names = b'\0.text\0.symtab\0.strtab\0.shstrtab\0'
    data = bytearray(0x100)
    code_size = len(words) * 4
    data += struct.pack(f'<{len(words)}I', *words)
    symbols = len(data)
    data += bytes(16) + struct.pack('<IIIBBH', 1, 0x100000, code_size, 0x12, 0, 1)
    strtab = len(data)
    data += strings
    shstrtab = len(data)
    data += names
    data += bytes(-len(data) % 4)
    sections = len(data)
    data += bytes(40)
    for section in (
        (1, 1, 6, 0x100000, 0x100, code_size, 0, 0, 16, 0),
        (7, 2, 0, 0, symbols, 32, 3, 1, 4, 16),
        (15, 3, 0, 0, strtab, len(strings), 0, 0, 1, 0),
        (23, 3, 0, 0, shstrtab, len(names), 0, 0, 1, 0),
    ):
        data += struct.pack('<10I', *section)
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    struct.pack_into('<16sHHIIIIIHHHHHH', data, 0, ident, 2, 8, 1, 0x100000,
                     52, sections, 0x20924001, 52, 32, 1, 40, 5, 4)
    struct.pack_into('<8I', data, 52, 1, 0x100, 0x100000, 0x100000, code_size, code_size, 5, 16)
    return data


recompiler = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(dir=sys.argv[2] if len(sys.argv) > 2 else None) as tmp:
    root = Path(tmp)
    (root / 'fixture.elf').write_bytes(elf())

    def run(handler, entry_points=''):
        (root / 'config.toml').write_text(
            '[general]\ninput = "fixture.elf"\noutput = "generated/"\n'
            f'stubs = ["{handler}@0x00100000"]\nentry_points = [{entry_points}]\n')
        result = subprocess.run([recompiler, 'config.toml'], cwd=root, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        return {p.name: (p.read_bytes(), p.stat().st_mtime_ns)
                for p in (root / 'generated').iterdir() if p.suffix in ('.h', '.cpp')}

    before = run('sceIpuSync')
    def describe():
        result = subprocess.run([recompiler, 'config.toml', '--describe-inputs', 'inputs.txt'],
                                cwd=root, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        lines = (root / 'inputs.txt').read_text().splitlines()
        assert lines.pop(0) == 'ps2x-inputs-v1'
        return [(role, bytes.fromhex(path).decode()) for role, path in
                (line.split(' ', 1) for line in lines)]
    assert describe() == [('elf', 'fixture.elf'), ('output', 'generated/')]
    with (root / 'config.toml').open('a') as config:
        config.write('ghidra_output = "symbols map.csv"\n')
    assert describe() == [('elf', 'fixture.elf'), ('output', 'generated/'),
                          ('extra', 'symbols map.csv')]
    after = run('sceIpuInit')
    for name in ('ps2_recompiled_functions.h', 'ps2_recompiled_stubs.h'):
        assert before[name] == after[name], f'{name} invalidated unrelated objects'
    assert before['register_functions.cpp'] == after['register_functions.cpp']
    assert any(before.get(name) != value for name, value in after.items() if name.endswith('.cpp'))
    assert not any(b'ps2_stubs::sceIpuInit(' in value[0] for value in after.values())
    assert run('sceIpuInit') == after, 'Identical preparation rewrote generated output'
    learned = run('sceIpuInit', '"entry@0x00100004"')
    for name in ('ps2_recompiled_functions.h', 'ps2_recompiled_stubs.h'):
        assert learned[name] == after[name], f'Entry learning invalidated {name}'
    assert run('sceIpuInit', '"entry@0x00100004"') == learned
    # MFC0/ MTC0 Count use the one EE clock, including guest writes.
    (root / 'fixture.elf').write_bytes(elf((0x40024800, 0x40844800, 0x03e00008, 0)))
    count = run('sceIpuInit')
    code = b'\n'.join(value[0] for name, value in count.items() if name.endswith('.cpp'))
    assert b'runtime->readCop0Count()' in code
    assert b'runtime->writeCop0Count(GPR_U32(ctx, 4))' in code
print('Recompiler incremental regression passed')
