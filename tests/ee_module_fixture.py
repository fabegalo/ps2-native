"""Build public synthetic EE modules through the production SDK generator."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys

tool, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
for name, words in (
    ('branch', (0x10000003, 0, 0xffffffff, 0x0fffffff, 0x2402002a, 0x03e00008, 0)),
    ('likely', (0x50850003, 0x24020063, 0x03e00008, 0, 0x2402002a, 0x03e00008, 0)),
):
    directory = output / name
    directory.mkdir(parents=True, exist_ok=True)
    image = struct.pack('<7I', *words)
    identity = hashlib.sha256(image).hexdigest()
    (directory / 'image.bin').write_bytes(image)
    (directory / 'config.toml').write_text(
        '[general]\ninput="image.bin"\noutput="g"\n'
        f'module_id="{identity}"\n[raw_image]\nload_address=2097152\nentry=2097152\n')
    result = subprocess.run([str(tool), 'config.toml'], cwd=directory,
                            capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    (directory / 'fixture.h').write_text(
        '#include "runtime/ee_aot.h"\n'
        f'extern const EeAotModule g_ps2EeAot_{identity};\n'
        f'inline const EeAotModule& {name}_module() {{ return g_ps2EeAot_{identity}; }}\n')
