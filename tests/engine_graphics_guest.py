#!/usr/bin/env python3
"""Build a tiny original EE diagnostic ELF, without a proprietary SDK or game data.
This exercises EE AOT -> DMA1 -> VIF DIRECT -> GIF PATH2 -> GS, not gameplay.
"""
from pathlib import Path
import struct
import sys

def make_guest():
    code=[]
    def li(reg,value):
        code.extend((0x3c000000|(reg<<16)|(value>>16),0x34000000|(reg<<21)|(reg<<16)|(value&0xffff)))
    def write(address,value):
        li(8,address);li(9,value);code.append(0xad090000)
    def write64(address,value):
        write(address,value&0xffffffff);write(address+4,value>>32)
    write64(0x12000000,1)
    write64(0x12000070,1<<9)
    write64(0x12000080,(31<<32)|(31<<44))
    write(0x1000e000,1)
    write(0x10009010,0x101000)
    write(0x10009020,11)
    write(0x10009000,0x101)
    li(8,0x10009000)
    code.extend((0x8d090000,0x31290100,0x1520fffd,0))
    li(8,0x12001000)
    code.extend((0x8d090000,0x31290002,0x1120fffd,0))
    code.extend((0x24030004,0x24040000,0x0000000c,0))
    packet=[(0x1000000000008009,0x0e),(0x10000,0x4c),(1<<32,0x4e),
            ((31<<16)|(31<<48),0x40),(0x30000,0x47),(6,0),
            (0x3f800000800000ff,1),(0,5),((512<<16)|512,5),(0,0x61)]
    image=bytearray(4096+176)
    struct.pack_into('<'+'I'*len(code),image,0,*code)
    struct.pack_into('<4I',image,4096,0,0,0,0x5000000a)
    for i,(lo,hi) in enumerate(packet):struct.pack_into('<QQ',image,4112+16*i,lo,hi)
    ident=b'\x7fELF\x01\x01\x01'+bytes(9)
    header=struct.pack('<16sHHIIIIIHHHHHH',ident,2,8,1,0x100000,52,0,0x20924001,52,32,1,40,0,0)
    segment=struct.pack('<8I',1,4096,0x100000,0x100000,len(image),len(image),7,4096)
    return header+segment+bytes(4096-len(header)-len(segment))+image
if __name__=='__main__':
    target=Path(sys.argv[1]);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(make_guest())
