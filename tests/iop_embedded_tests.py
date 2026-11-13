"""Source views must match buffer-loaded IRX bytes and preserve cached outputs."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('iop_aot', sys.argv[1])
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)
fixture = Path(sys.argv[2])
cache = fixture.parent
image = fixture.read_bytes()
identity = hashlib.sha256(image).hexdigest()


def ee_container(payload, outside=b''):
    data = bytearray(0x100) + payload + outside
    struct.pack_into('<16sHHIIIIIHHHHHH', data, 0,
                     b'\x7fELF\x01\x01\x01' + bytes(9), 2, 8, 1, 0x100000,
                     52, 0, 0, 52, 32, 1, 0, 0, 0)
    struct.pack_into('<8I', data, 52, 1, 0x100, 0x100000, 0x100000,
                     len(payload), len(payload), 6, 16)
    return data


def rejects(operation):
    try:
        operation()
    except ValueError:
        return
    raise AssertionError('invalid source view accepted')


scratch = Path(sys.argv[3])
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='iop-view-', dir=scratch) as temporary:
    root = Path(temporary)
    source = root / 'conteúdo EE.elf'
    # A repeated module is prepared once. Valid bytes outside PT_LOAD are not inputs.
    payload = bytes(32) + image + bytes(32) + image + bytes(32)
    source.write_bytes(ee_container(payload, image))
    found = sdk.embedded_modules(source)
    assert found == [{'identity': identity, 'offset': 0x120, 'size': len(image)}]
    assert sdk.elf_extent(source.read_bytes(), 0x120) == len(image)
    stamps = {p: (p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).digest())
              for p in cache.glob('*.cpp')}
    # Same native module, reached through a container rather than a standalone file.
    assert sdk.generate(source, cache, 0x120, len(image), identity)['identity'] == identity
    assert all(p.stat().st_mtime_ns == stamp and hashlib.sha256(p.read_bytes()).digest() == digest
               for p, (stamp, digest) in stamps.items())
    forbidden = root / 'must-not-publish'
    rejects(lambda: sdk.generate(source, forbidden, 0x120, len(image), '0' * 64))
    rejects(lambda: sdk.generate(source, forbidden, -1, len(image), identity))
    rejects(lambda: sdk.generate(source, forbidden, 0x120, 16 * 1024 * 1024 + 1))
    rejects(lambda: sdk.generate(source, forbidden, len(source.read_bytes()), len(image)))
    assert not forbidden.exists()
    source.write_bytes(ee_container(bytes(32), image))
    assert sdk.embedded_modules(source) == []
    source.write_bytes(ee_container(bytes(32) + image[:-1]))
    rejects(lambda: sdk.embedded_modules(source))
    source.write_bytes(b'\x7fELF\x01\x01\x01')
    rejects(lambda: sdk.embedded_modules(source))
print('PASS embedded IOP identity, load ranges, deduplication, cache reuse and invalid views')
