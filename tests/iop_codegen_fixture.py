"""Build a small, redistributable ELF to exercise emitted R3000A operations."""
import importlib.util
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('iop_aot', sys.argv[1])
emitter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(emitter)
root = Path(sys.argv[2])
root.mkdir(parents=True, exist_ok=True)

words = [(1 << 21) | (2 << 16) | (3 << 11) | (5 << 6) | fn for fn in
         (0, 2, 3, 4, 6, 7, 8, 9, 12, 13, 16, 17, 18, 19, 24, 25, 26, 27,
          32, 33, 34, 35, 36, 37, 38, 39, 42, 43, 63)]
words += [(1 << 26) | (1 << 21) | (rt << 16) | 0xfffc for rt in (0, 1, 16, 17, 31)]
words += [(op << 26) | (1 << 21) | (2 << 16) | imm for op in
          (4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
           32, 33, 34, 35, 36, 37, 38, 40, 41, 42, 43, 46)
          for imm in (0, 1, 3, 0xffff)]
words += [0x08000008, 0x0c000008, 0x40026000, 0x40826000, 0x42000010,
          0xffffffff, (1 << 21) | 9, (31 << 21) | (16 << 16) | (1 << 26)]
relocations = [(len(words) * 4, 5), ((len(words) + 1) * 4, 6), ((len(words) + 2) * 4, 4)]
words += [0x3c080000, 0x35080020, 0x08000008]
relocations.append((len(words) * 4, 2))
words.append(0x20)  # Address literal in .text; executing it requires an explicit gap.
relocations += [(len(words) * 4, 5), (len(words) * 4, 5), ((len(words) - 3) * 4, 6)]
words.append(0)  # Repeated dummy HI16 records may change SPECIAL decoding bits.
code = struct.pack(f'<{len(words)}I', *words)
rel = b''.join(struct.pack('<II', *entry) for entry in relocations)
data = bytearray(0x100) + code + rel
shoff = len(data)
data += bytes(40)
data += struct.pack('<10I', 0, 1, 6, 0, 0x100, len(code), 0, 0, 4, 0)
data += struct.pack('<10I', 0, 9, 0, 0, 0x100 + len(code), len(rel), 0, 1, 4, 8)
struct.pack_into('<16sHHIIIIIHHHHHH', data, 0,
                 b'\x7fELF\x01\x01\x01' + bytes(9), 0xff80, 8, 1, 0, 52,
                 shoff, 0, 52, 32, 1, 40, 3, 0)
struct.pack_into('<8I', data, 52, 1, 0x100, 0, 0, len(code), len(code), 5, 4)
fixture = root / 'fixture.irx'
if not fixture.exists() or fixture.read_bytes() != data:
    fixture.write_bytes(data)
result = emitter.generate(fixture, root)
header = '#pragma once\n#include <array>\n#include <cstdint>\n'
header += f'void register_iop_{result["identity"]}();\n'
header += f'inline void registerFixture() {{ register_iop_{result["identity"]}(); }}\n'
header += f'inline constexpr std::array<uint8_t,{len(data)}> kImage = {{' + ','.join(map(str, data)) + '};\n'
header += f'inline constexpr unsigned kInstructionCount = {len(words)};\n'
header += f'inline constexpr unsigned kComparableInstructionCount = {len(words) - 2};\n'
emitter.changed(root / 'fixture.h', header)

# Generator errors and unchanged-output behavior are part of the build contract.
before = {p.name: (p.stat().st_mtime_ns, p.read_bytes()) for p in root.glob('*.cpp')}
emitter.generate(fixture, root)
assert all((root / n).stat().st_mtime_ns == stamp and (root / n).read_bytes() == body
           for n, (stamp, body) in before.items())
for invalid in (b'not an ELF', data[:60]):
    try:
        emitter.parse(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError('invalid code image accepted')
print(f'Generated {len(words)} instruction cases with HI16/LO16/J26 relocations')
