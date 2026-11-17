#!/usr/bin/env python3
"""Synthetic preparation checks; no firmware, game execution or performance certification."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from engine_graphics_guest import make_guest

compiler=Path(sys.argv[1]).resolve()
parent=Path.cwd().resolve()
with tempfile.TemporaryDirectory(prefix="engine-prepare-",dir=parent) as temporary:
    root=Path(temporary).resolve()
    assert root.is_relative_to(parent)
    elf=root/"original.elf";elf.write_bytes(make_guest());output=root/"aot"
    image=root/"module.bin";data=bytearray(65536)
    for at,value in ((0,123),(32,456)):struct.pack_into("<3I",data,at,0x24020000|value,0x03e00008,0)
    image.write_bytes(data)
    def prepare(entries):
        args=[str(compiler),str(elf),"--out",str(output)]
        for entry in entries:args += ["--module",str(image),"80070000",f"{0x80070000+entry:08x}"]
        result=subprocess.run(args,capture_output=True,timeout=15)
        assert result.returncode==0,(result.stdout+result.stderr).decode(errors="replace")
    def freeze(paths):
        result={}
        for path in paths:
            os.utime(path,ns=(1600000000000000000,1600000000000000000))
            result[path]=(path.read_bytes(),path.stat().st_mtime_ns)
        return result
    def unchanged(snapshot):
        for path,expected in snapshot.items():assert (path.read_bytes(),path.stat().st_mtime_ns)==expected,path.name
    prepare([])
    primary=freeze([output/"units.hpp",*output.glob("unit_*.cpp")])
    prepare([0]);unchanged(primary)
    prior=freeze([*output.glob("*.cpp"),*output.glob("*.hpp")])
    prepare([0]);unchanged(prior)
    old_units=freeze([*output.glob("unit_*.cpp"),*output.glob("captured_*unit_*.cpp"),output/"units.hpp"])
    prepare([0,32]);unchanged(old_units)
    stable=freeze([*output.glob("*.cpp"),*output.glob("*.hpp")]);prepare([0,32]);unchanged(stable)
    original={p:p.read_bytes() for p in output.glob("unit_*.cpp")}
    changed=bytearray(elf.read_bytes());phoff=struct.unpack_from("<I",changed,28)[0];code=struct.unpack_from("<I",changed,phoff+4)[0];changed[code]^=1;elf.write_bytes(changed);prepare([0,32])
    assert any(p.read_bytes()!=text for p,text in original.items()),"A changed instruction must invalidate its native source"
    assert any(p.stat().st_mtime_ns!=1600000000000000000 for p,text in original.items() if p.read_bytes()!=text)
print("engine-prepare: added modules retain identical primary inputs; repeated preparation is stable; changed code invalidates PASS")
