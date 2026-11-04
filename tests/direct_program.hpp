// Three independent, original bare-metal programs; no firmware/game content.
#pragma once
#include "bios_program.hpp"
#include "ps2native/ee.hpp"
#include <span>

namespace direct_test {
using namespace m20;
inline constexpr std::uint32_t text = 0x00100000u, data = 0x00110000u;
inline constexpr std::uint32_t dynamic_address=0x00120000u;
inline std::vector<std::uint8_t> dynamic_code() { return le_words({0x2402002Au,0x03E00008u,0u}); }
struct Segment { std::uint32_t address, flags; std::vector<std::uint8_t> bytes; std::uint32_t bss{}; };
struct Program {
    std::vector<Segment> segments;
    std::uint32_t stop{};
    std::vector<std::uint8_t> iop;
    std::vector<std::uint32_t> roots;
};
inline void put(std::vector<std::uint8_t>& b, std::size_t p, std::uint64_t v, unsigned n = 4) {
    for (unsigned i = 0; i < n; ++i) b.at(p+i) = static_cast<std::uint8_t>(v >> (i*8u));
}
inline void store(Assembler& a, std::uint32_t address, std::uint32_t value) {
    a.la(t0,address); a.la(t1,value); a.sw(t1,0,t0);
}
inline Program fpu() {
    Assembler a(text);a.la(t0,ps2native::kStatusCU1);a.mtc0(t0,Status);
    a.la(t0,0xBF800000u);a.raw(0x44880000u);
    a.la(t0,0x3F800000u);a.raw(0x44880800u);
    a.addiu(t1,zero,0);a.addiu(t2,zero,0);
    for(unsigned kind=0;kind<4;++kind)for(unsigned condition=0;condition<2;++condition) {
        a.raw(condition?0x46000032u:0x46010030u);
        a.raw(0x45000002u|(kind<<16));
        a.addiu(t1,t1,1);a.addiu(t2,t2,1);
    }
    a.raw(0x46010034u);a.la(t0,data);a.sw(t1,0,t0);a.sw(t2,4,t0);
    const auto stop=a.here();a.brk();
    return {{{text,5,le_words(a.finish())},{data,6,std::vector<std::uint8_t>(8)}},stop,{}, {}};
}
inline Program cpu() {
    Assembler a(text);
    a.la(s0,data); a.addiu(t4,zero,10000); a.addiu(v0,zero,0);
    a.label("sum"); a.la(t9,text+0x200); a.jalr(t9); a.nop();
    a.addiu(t4,t4,0xFFFF); a.bne(t4,zero,"sum"); a.nop(); a.sw(v0,0,s0);
    a.mtc0(zero,Count); a.addiu(t1,zero,40); a.mtc0(t1,11);
    a.la(t1,0x00018001u); a.mtc0(t1,Status);
    a.label("irq"); a.lw(t2,4,s0); a.beq(t2,zero,"irq"); a.nop();
    a.mtc0(zero,Status);
    a.lq(t1,16,s0);a.raw(0x70000004u|(t1<<21u)|(t1<<11u));a.sq(t1,32,s0);
    const auto stop=a.here(); a.brk();
    while(a.here()<text+0x200) a.nop();
    a.addiu(v0,v0,3); a.jr(ra); a.nop();
    Assembler h(0x80000200u);
    h.la(k0,data+4u); h.addiu(27,zero,1); h.sw(27,0,k0);
    h.mfc0(27,Cause); h.sw(27,4,k0); h.mfc0(27,14); h.sw(27,8,k0);
    h.la(27,0xFFFFFFFFu); h.mtc0(27,11); h.eret();
    std::vector<std::uint8_t> bytes(48);
    put(bytes,16,0x8000000000000001ull,8);put(bytes,24,0x0123456789ABCDEFull,8);
    return {{{text,5,le_words(a.finish())},{data,6,std::move(bytes),64},
             {0x00000200u,5,le_words(h.finish())}},stop,{}, {}};
}
inline std::vector<std::uint8_t> iop_receiver(bool program_interrupt_masks=true, std::uint32_t chcr=0x01000600u,
                                              std::uint32_t dicr2=1u<<19) {
    Assembler a(0x1000u);
    store(a,0xBF801570u,0x8000u); // DPCR2 ch10 enable
    store(a,0xBF801578u,1u); // DMACEN
    store(a,0xBF80157Cu,1u); // DMACINTEN: master flags and IRQ enabled
    if(program_interrupt_masks) {
        store(a,0xBF8010F4u,1u<<23); // master IRQ enable
        store(a,0xBF801574u,dicr2); // channel completion and tag IRQ enables
    }
    store(a,0xBF801538u,chcr); // destination tag stream
    a.la(t0,0xBF801538u);
    a.label("busy"); a.lw(t1,0,t0); a.nop(); a.lui(t2,0x0100); a.r(t1,t2,t1,0,0x24);
    a.bne(t1,zero,"busy"); a.nop();
    // Checksum actual received memory, including R3000A load-delay slots.
    a.la(t0,0x4000); a.addiu(t2,zero,64); a.addiu(v0,zero,0);
    a.label("sum"); a.lw(t1,0,t0); a.addiu(t0,t0,4); a.addu(v0,v0,t1);
    a.addiu(t2,t2,0xFFFF); a.bne(t2,zero,"sum"); a.nop();
    a.la(t0,0x4100); a.sw(v0,0,t0);
    a.la(t0,0xBF801574u); a.lw(t1,0,t0); a.nop(); a.la(t0,0x4104); a.sw(t1,0,t0);
    a.la(t0,0xBF801070u); a.lw(t1,0,t0); a.nop(); a.andi(t1,t1,8);
    a.la(t0,0x4108); a.sw(t1,0,t0);
    // SIF SMFLG signals that all memory writes are observable by the EE.
    store(a,0xBD000030u,1);
    a.label("idle"); a.beq(zero,zero,"idle"); a.nop();
    return le_words(a.finish());
}
inline Program dma() {
    Assembler a(text);
    store(a,0xB000E020u,0); // Establish priority state explicitly, independent of reset bits.
    store(a,0xB000E000u,1); store(a,0xB000C410u,data);
    store(a,0xB000C420u,17); store(a,0xB000C400u,0x101);
    a.la(t0,0xB000F230u);
    a.label("ready"); a.lw(t1,0,t0); a.andi(t1,t1,1); a.beq(t1,zero,"ready"); a.nop();
    a.la(t0,0xBC004100u); a.lw(v0,0,t0); a.lw(v1,4,t0); a.lw(a0,8,t0);
    const auto stop=a.here(); a.brk();
    std::vector<std::uint8_t> payload(272);
    put(payload,0,0x80004000u); put(payload,4,64); // IOP destination tag, words
    for(unsigned i=0;i<64;++i) put(payload,16+i*4,0x01020300u+i);
    return {{{text,5,le_words(a.finish())},{data,6,std::move(payload)}},stop,iop_receiver(),{}};
}
inline std::vector<std::uint8_t> iop_sender(unsigned words=64,unsigned destination=data,bool undefined=false) {
    Assembler a(0x1000u);
    store(a,0xBF801570u,0x800u);store(a,0xBF801578u,1);store(a,0xBF80157Cu,1);
    store(a,0xBF8010F4u,1u<<23u);store(a,0xBF801574u,1u<<18u);
    store(a,0x4000u,0x80005000u);store(a,0x4004u,words);
    store(a,0x4008u,0x70000000u|((words+3u)/4u));store(a,0x400Cu,destination);
    if(!undefined)for(unsigned w=0;w<words;++w)store(a,0x5000u+w*4u,0xCA000000u+w);
    store(a,0xBF80152Cu,0x4000u);store(a,0xBF801528u,0x01000701u);
    a.label("idle");a.beq(zero,zero,"idle");a.nop();
    return le_words(a.finish());
}
inline Program sif0() {
    Assembler a(text);
    store(a,0xB000E020u,0);store(a,0xB000E000u,1);store(a,0xB000E010u,1u<<21u);
    store(a,0xB000C020u,0);store(a,0xB000C000u,0x184);
    a.la(t0,ps2native::kStatusIE|ps2native::kStatusEIE|ps2native::kStatusIM3);a.mtc0(t0,Status);
    a.la(s0,data+256u);
    a.label("wait");a.lw(t0,0,s0);a.beq(t0,zero,"wait");a.nop();
    a.la(t0,data);a.addiu(t2,zero,64);a.addiu(v0,zero,0);
    a.label("sum");a.lw(t1,0,t0);a.addu(v0,v0,t1);a.addiu(t0,t0,4);
    a.addiu(t2,t2,0xFFFF);a.bne(t2,zero,"sum");a.nop();
    const auto stop=a.here();a.brk();
    Assembler h(0x80000200u);
    h.la(k0,data+256u);h.addiu(27,zero,1);h.sw(27,0,k0);
    h.mfc0(27,Cause);h.sw(27,4,k0);
    h.la(k0,0xB000E010u);h.addiu(27,zero,0x20);h.sw(27,0,k0);h.eret();
    return {{{text,5,le_words(a.finish())},{data,6,std::vector<std::uint8_t>(264)},
             {0x200u,5,le_words(h.finish())}},stop,iop_sender(),{}};
}
inline Program frame() {
    Assembler a(text);
    std::vector<std::uint8_t> bytes;
    const auto qword=[&](std::uint64_t lo,std::uint64_t hi) {
        const auto at=bytes.size(); bytes.resize(at+16); put(bytes,at,lo,8); put(bytes,at+8,hi,8);
        return data+static_cast<std::uint32_t>(at);
    };
    const auto privileged=[&](std::uint32_t address,std::uint64_t value) {
        const auto p=qword(value,0); a.la(t0,address); a.la(t1,p); a.lq(t2,0,t1); a.sd(t2,0,t0);
    };
    privileged(0xB2000010u,2u<<13); privileged(0xB2000020u,0);
    privileged(0xB2000000u,0xFF25); privileged(0xB2000070u,1u<<9);
    privileged(0xB2000080u,(63ull<<32)|(31ull<<44));
    privileged(0xB2000060u,2u|(2ull<<42));
    const auto packet=data+static_cast<std::uint32_t>(bytes.size());
    // VIF1 DIRECT followed by PACKED A+D setup and a PSMCT32 IMAGE upload.
    qword(0,0x50000207ull<<32); // 519 GIF qwords: setup tag + 5 regs + image tag + 512 data
    qword(5u|(1ull<<15)|(1ull<<60),0xE);
    qword(1ull<<48,0x50); // BITBLTBUF DBW=1 PSMCT32
    qword(0,0x51); qword(64u|(32ull<<32),0x52); qword(0,0x53); qword(0,0x3F);
    qword(512u|(1u<<15)|(2ull<<58),0);
    for(unsigned y=0;y<32;++y) for(unsigned x=0;x<64;x+=4) {
        const auto pixel=[&](unsigned px) { return 0x80000000u | (px*4u) | (y*8u<<8) | (((px/8u+y/8u)&1u)?0xE00000u:0x200000u); };
        qword(pixel(x)|(std::uint64_t{pixel(x+1)}<<32),pixel(x+2)|(std::uint64_t{pixel(x+3)}<<32));
    }
    // 64*32 / 4 = 512 IMAGE qwords; DIRECT count = 519, total including VIF = 520.
    a.la(t0,packet); a.la(t1,0xB0005000u); a.addiu(t2,zero,520);
    a.label("gif"); a.lq(t3,0,t0); a.sq(t3,0,t1); a.addiu(t0,t0,16);
    a.addiu(t2,t2,0xFFFF); a.bne(t2,zero,"gif"); a.nop();
    a.addiu(t4,zero,14000); a.label("wait"); a.addiu(t4,t4,0xFFFF); a.bne(t4,zero,"wait"); a.nop();
    const auto stop=a.here(); a.brk();
    return {{{text,5,le_words(a.finish())},{data,6,std::move(bytes)}},stop,{},{}};
}
inline Program exceptions() {
    Assembler a(text);
    std::vector<std::uint32_t> roots;
    a.addiu(t0,zero,0x10); a.mtc0(t0,Status); // user mode, no CU0
    const auto syscall=a.here(); a.raw(0x0000000Cu); roots.push_back(a.here());
    a.addiu(v0,zero,7);
    a.beq(zero,zero,"target"); a.raw(0x0000000Cu); roots.push_back(a.here());
    a.addiu(v0,v0,11); // handler skips the trapped branch+slot, cancels its target
    a.label("target"); a.brk(); roots.push_back(a.here());
    a.addiu(v0,v0,13);
    a.i(0x15,zero,zero,0); a.raw(0x0000000Cu); // false BNEL annuls the SYSCALL
    roots.push_back(a.here()); a.addiu(v0,v0,17);
    a.la(t0,data+32); a.lq(t1,0,t0);
    a.raw(0x700006E9u|(t1<<16u)|(t1<<11u)); // PCPYH t1,t1, aliasing
    a.sq(t1,16,t0);
    a.raw(0x70000389u|(t1<<21u)|(t1<<16u)|(t1<<11u)); // PCPYLD t1,t1,t1
    a.sq(t1,32,t0);
    const auto stop=a.here(); a.brk();
    Assembler h(0x80000180u);
    h.la(k0,data); h.lw(t0,0,k0); h.addiu(t1,t0,1); h.sw(t1,0,k0);
    h.r(0,t0,t0,3,0); h.addu(k0,k0,t0);
    h.mfc0(27,Cause); h.sw(27,4,k0); h.mfc0(t2,14); h.sw(t2,8,k0);
    h.r(0,27,t3,31,2); h.r(0,t3,t3,2,0); h.addiu(t3,t3,4);
    h.addu(t2,t2,t3); h.mtc0(t2,14); h.eret();
    std::vector<std::uint8_t> records(80);
    put(records,28,syscall); // independent test's expected first EPC
    put(records,32,0xFEDCBA9876543210ull,8); put(records,40,0x0123456789ABCDEFull,8);
    return {{{text,5,le_words(a.finish())},{data,6,std::move(records)},
             {0x00000180u,5,le_words(h.finish())}},stop,{},std::move(roots)};
}
inline Program timer() {
    Assembler a(text);
    a.la(t0,0xB2000010u);a.la(t1,1u<<17u);a.sd(t1,0,t0); // SINT: known, held sync
    store(a,0xB000F000u,0x7FFF);
    store(a,0xB0000020u,16);store(a,0xB0000000u,0);
    store(a,0xB000F010u,1u<<9u);store(a,0xB0000010u,0xDC0);
    a.la(t0,0x00010401u);a.mtc0(t0,Status);
    const auto resume=a.here();
    a.la(t0,data);a.label("wait");a.lw(t1,0,t0);a.beq(t1,zero,"wait");a.nop();
    a.addiu(v0,zero,42);
    a.raw(0x00006028u);a.nop();a.nop();a.nop(); // MFSA t4
    a.raw(0x01800029u);a.nop();a.nop();a.nop(); // MTSA t4
    a.la(t0,data+16);a.lq(t1,0,t0);
    a.raw(0x70000229u|(t1<<21u));a.raw(0x70000269u|(t1<<21u));
    a.raw(0x70000209u|(t2<<11u));a.raw(0x70000249u|(t3<<11u));
    a.sq(t2,16,t0);a.sq(t3,32,t0);
    a.raw(0x700003A9u|(t2<<21u)|(t3<<16u)|(t4<<11u));a.sq(t4,48,t0);
    a.raw(0x70000488u|(t2<<21u)|(t3<<16u)|(t4<<11u));a.sq(t4,64,t0);
    a.raw(0x700004A8u|(t2<<21u)|(t3<<16u)|(t4<<11u));a.sq(t4,80,t0);
    a.raw(0x700004C8u|(t2<<21u)|(t3<<16u)|(t4<<11u));a.sq(t4,96,t0);
    a.raw(0x70000004u|(t2<<21u)|(t4<<11u));a.sq(t4,112,t0);
    const auto stop=a.here();a.brk();
    Assembler h(0x80000200u);
    h.la(k0,data);h.mfc0(27,Cause);h.sw(27,4,k0);
    h.la(t2,0xB000F000u);h.lw(t3,0,t2);h.sw(t3,8,k0);
    h.la(t2,0xB0000010u);h.addiu(t3,zero,0x400);h.sw(t3,0,t2); // stop/clear EQUF
    h.la(t2,0xB000F000u);h.addiu(t3,zero,1u<<9u);h.sw(t3,0,t2);
    h.addiu(27,zero,1);h.sw(27,0,k0);h.eret();
    std::vector<std::uint8_t> bytes(144);
    put(bytes,16,0x0123456789ABCDEFull,8);put(bytes,24,0xFEDCBA9876543210ull,8);
    return {{{text,5,le_words(a.finish())},{data,6,std::move(bytes)},
             {0x200u,5,le_words(h.finish())}},stop,{},{resume}};
}
inline Program unaligned() {
    Assembler a(text);a.la(s0,data);
    unsigned index=0;
    for(unsigned width:{4u,8u})for(unsigned offset=0;offset<width;++offset,++index) {
        a.lq(t2,32,s0);
        a.i(width==8 ? 0x1A : 0x22,s0,t2,offset+width-1);
        a.i(width==8 ? 0x1B : 0x26,s0,t2,offset);
        a.sq(t2,128+index*32,s0);
        a.lq(t3,16,s0);a.la(t4,data+512+index*32);
        a.i(width==8 ? 0x2C : 0x2A,t4,t3,offset+width-1);
        a.i(width==8 ? 0x2D : 0x2E,t4,t3,offset);
    }
    a.lq(t2,16,s0);a.lq(t3,32,s0);a.mmi(t2,t3,t4,9,8);a.sq(t4,960,s0);
    const auto stop=a.here();a.brk();
    std::vector<std::uint8_t> bytes(1024,0xCC);
    for(unsigned i=0;i<32;++i)bytes[i]=static_cast<std::uint8_t>(0x80+i);
    put(bytes,32,0xFFEEDDCCBBAA9988ull,8);put(bytes,40,0x0123456789ABCDEFull,8);
    return {{{text,5,le_words(a.finish())},{data,6,std::move(bytes)}},stop,{}, {}};
}
inline Program build(const std::string& mode) {
    if(mode=="fpu")return fpu();
    if(mode=="cpu") return cpu();
    if(mode=="dma") return dma();
    if(mode=="sif0") return sif0();
    if(mode=="frame") return frame();
    if(mode=="exceptions") return exceptions();
    if(mode=="timer") return timer();
    if(mode=="unaligned") return unaligned();
    if(mode=="dynamic") {
        Assembler a(text);
        a.la(t0,data);a.la(t1,dynamic_address);a.addiu(t2,zero,3);
        a.label("copy");a.lw(t3,0,t0);a.sw(t3,0,t1);a.addiu(t0,t0,4);a.addiu(t1,t1,4);
        a.addiu(t2,t2,0xFFFF);a.bne(t2,zero,"copy");a.nop();
        a.sync_p();a.beq(zero,zero,"ready");a.nop();a.label("ready");const auto checkpoint=a.here();
        a.la(t9,dynamic_address);a.jalr(t9);a.nop();a.brk();
        return {{{text,5,le_words(a.finish())},{data,6,dynamic_code()}},checkpoint,{},{}};
    }
    throw std::runtime_error("unknown Direct ELF fixture");
}
inline std::vector<std::uint8_t> elf(const Program& p) {
    std::vector<std::uint8_t> b(0x1000u);
    b[0]=0x7F;b[1]='E';b[2]='L';b[3]='F';b[4]=b[5]=b[6]=1;
    put(b,16,2,2);put(b,18,8,2);put(b,20,1);put(b,24,text);put(b,28,52);
    put(b,40,52,2);put(b,42,32,2);put(b,44,p.segments.size(),2);
    for(std::size_t i=0;i<p.segments.size();++i) {
        const auto& s=p.segments[i];const auto off=b.size();b.insert(b.end(),s.bytes.begin(),s.bytes.end());
        const auto h=52+i*32;put(b,h,1);put(b,h+4,off);put(b,h+8,s.address);put(b,h+12,s.address);
        put(b,h+16,s.bytes.size());put(b,h+20,s.bytes.size()+s.bss);put(b,h+24,s.flags);put(b,h+28,4);
    }
    if(!p.roots.empty()) {
        while(b.size()%4) b.push_back(0);
        const auto syms=b.size();b.resize(syms+16*(p.roots.size()+1));
        for(std::size_t i=0;i<p.roots.size();++i) {
            put(b,syms+(i+1)*16+4,p.roots[i]);b[syms+(i+1)*16+12]=0x12;
            put(b,syms+(i+1)*16+14,1,2);
        }
        const auto strings=b.size();b.push_back(0);
        while(b.size()%4)b.push_back(0);
        const auto sections=b.size();b.resize(sections+3*40);
        put(b,32,sections);put(b,46,40,2);put(b,48,3,2);
        put(b,sections+44,2);put(b,sections+56,syms);put(b,sections+60,16*(p.roots.size()+1));
        put(b,sections+64,2);put(b,sections+68,1);put(b,sections+72,4);put(b,sections+76,16);
        put(b,sections+84,3);put(b,sections+96,strings);put(b,sections+100,1);put(b,sections+112,1);
    }
    return b;
}
} // namespace direct_test
