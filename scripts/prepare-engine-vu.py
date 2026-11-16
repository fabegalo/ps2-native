#!/usr/bin/env python3
"""Compile hash-verified VU micro-memory images into resumable native AOT pairs."""
import argparse
import hashlib
from pathlib import Path
import re
import struct

def write(path, text):
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")

def prepare(capture, output):
    output.mkdir(parents=True, exist_ok=True)
    sources = sorted(capture.glob("vu[01]-*.bin"))
    if len(sources) > 32:
        raise RuntimeError("VU image count exceeds bound")
    names = []
    for source in sources:
        match = re.fullmatch(r"vu([01])-([0-9a-f]{64})\.bin", source.name)
        if not match:
            raise RuntimeError("Invalid VU image identity")
        unit, identity = int(match[1]), match[2]
        data = source.read_bytes()
        if len(data) != (16384 if unit else 4096) or hashlib.sha256(data).hexdigest() != identity:
            raise RuntimeError("VU image hash/size mismatch")
        name = "vu_" + str(unit) + "_" + identity
        names.append(name)
        words = struct.unpack("<" + "I" * (len(data)//4), data)
        text = '#include "ps2native/engine/vu.hpp"\nusing namespace ps2native::engine;\n'
        text += 'namespace {\nconst std::uint8_t image[] = {' + ','.join(map(str,data)) + '};\n'
        text += 'const VuPairFn pairs[] = {\n'
        for lo, hi in zip(words[::2], words[1::2]):
            text += f'&native_vu_pair<0x{lo:08x}u,0x{hi:08x}u>,\n'
        text += '};\n}\n'
        text += f'extern const VuProgram {name} = {{{unit},image,sizeof(image),pairs}};\n'
        write(output/(name+'.cpp'), text)
    table = '#include "ps2native/engine/vu.hpp"\nusing namespace ps2native::engine;\n'
    for name in names:
        table += f'extern const VuProgram {name};\n'
    table += 'void register_generated_vu() {\n'
    for name in names:
        table += f'register_vu_programs(&{name},1);\n'
    table += '}\n'
    write(output/'vu_table.cpp', table)
    return len(names)

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    print('Verified VU AOT images:',prepare(args.capture,args.out))
