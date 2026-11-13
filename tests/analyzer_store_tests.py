"""A heuristic warning must never remove an original guest store."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import tomllib


def fixture():
    words = (0x3C080010, 0x35080100, 0x24091234, 0xAD090000,
             0x08040006, 0, 0x03E00008, 0)
    data = bytearray(0x100)
    data += struct.pack('<8I', *words)
    data += bytes(0x200 - len(data)) + bytes(4)
    symbols = len(data)
    data += bytes(16) + struct.pack('<IIIBBH', 1, 0x100000, 32, 0x12, 0, 1)
    strings = len(data)
    data += b'\0_start\0'
    names = b'\0.text\0.data\0.symtab\0.strtab\0.shstrtab\0'
    names_at = len(data)
    data += names
    data += bytes(-len(data) % 4)
    sections = len(data)
    data += bytes(40)
    for section in (
        (1, 1, 6, 0x100000, 0x100, 32, 0, 0, 16, 0),
        (7, 1, 7, 0x100100, 0x200, 4, 0, 0, 4, 0),  # writable data with the PS2 linker's executable flag
        (13, 2, 0, 0, symbols, 32, 4, 1, 4, 16),
        (21, 3, 0, 0, strings, 8, 0, 0, 1, 0),
        (29, 3, 0, 0, names_at, len(names), 0, 0, 1, 0),
    ):
        data += struct.pack('<10I', *section)
    struct.pack_into('<16sHHIIIIIHHHHHH', data, 0,
                     b'\x7fELF\x01\x01\x01' + bytes(9), 2, 8, 1, 0x100000,
                     52, sections, 0x20924001, 52, 32, 1, 40, 6, 5)
    struct.pack_into('<8I', data, 52, 1, 0x100, 0x100000, 0x100000, 0x104, 0x104, 7, 16)
    return data


analyzer = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(dir=sys.argv[2] if len(sys.argv) > 2 else None) as tmp:
    root = Path(tmp)
    (root / 'fixture.elf').write_bytes(fixture())
    result = subprocess.run([analyzer, 'fixture.elf', 'config.toml'], cwd=root,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'original instruction preserved, review required' in result.stdout, 'Potential code write lost its diagnostic'
    config = tomllib.loads((root / 'config.toml').read_text())
    patches = config.get('patches', {}).get('instructions', [])
    assert not any(int(item['address'], 0) == 0x10000C for item in patches), \
        'Analyzer replaced a guest data store with a guessed NOP'
print('Analyzer store-preservation regression passed')
