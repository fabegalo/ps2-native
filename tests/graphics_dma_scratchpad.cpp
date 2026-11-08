// Synthetic DMA sources exercise the shared RAM/SPR bytes, never firmware.
#include "boot_checks.hpp"
#include "ps2native/iop.hpp"
#include "ee_native_fixture.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr std::uint32_t spr=0x80000000u;
struct Fixture {
    Iop dma;Vif v0{Vif::Unit::Vu0},v1;Gif gif;GsRegisters gs;
    std::vector<std::uint8_t> ram=std::vector<std::uint8_t>(4096),scratch=std::vector<std::uint8_t>(16384);
    std::unordered_map<std::uint32_t,std::uint32_t> unknown;
    Fixture(){dma.reset({},nullptr);dma.attach_ee_ram(ram,&unknown,scratch);gs.reset(0);}
    void word(std::uint32_t at,std::uint32_t value) {
        auto& storage=(at&spr) ? scratch : ram;
        for(unsigned b=0;b<4;++b)storage.at((at&~spr)+b)=static_cast<std::uint8_t>(value>>(8u*b));
    }
    void tag(std::uint32_t at,std::uint32_t header,std::uint32_t address=0,
             std::uint32_t a=0,std::uint32_t b=0) {word(at,header);word(at+4,address);word(at+8,a);word(at+12,b);}
    void start(unsigned channel,std::uint32_t at,bool chain=true,bool tte=false,unsigned count=0) {
        const auto base=0x10008000u+channel*0x1000u;
        dma.ee_dma_write(0x1000E000u,1);dma.ee_dma_write(base+0x10,at);
        dma.ee_dma_write(base+0x20,count);dma.ee_dma_write(base+0x30,at);
        dma.ee_dma_write(base,chain ? (tte ? 0x145u : 0x105u) : 0x101u);
    }
    void advance(unsigned cycle){dma.advance_vif_dma(cycle,v0,v1,gif,gs);dma.advance_gif_dma(cycle,gif,gs);}
};
void source_selection() {
    for(unsigned channel=0;channel<2;++channel) {
        Fixture f;f.tag(spr|0x100,0x70000001u,0,0,0x30000000u);
        for(unsigned w=0;w<4;++w)f.word((spr|0x110)+w*4u,0x12345670u+w);
        f.start(channel,spr|0x100,true,true);f.advance(2);
        require(f.dma.ee_dma_read(0x10008020u+channel*0x1000u)==1,"SPR tag has its own bus beat");
        f.advance(4);const auto& vif=channel ? f.v1 : f.v0;
        for(unsigned w=0;w<4;++w)require(vif.read_register(0x100+w*16u)==0x12345670u+w,"TTE and payload use actual SPR bytes");
        require(f.dma.ee_dma_read(0x10008010u+channel*0x1000u)==(spr|0x120) &&
                (f.dma.ee_dma_read(0x1000E010u)&(1u<<channel)),"SPR MADR retains selection and completion follows payload");
    }
    for(bool tag_spr:{false,true}) {
        Fixture f;const auto at=tag_spr ? spr|0x100u : 0x100u;
        const auto payload=tag_spr ? 0x300u : spr|0x300u;
        f.tag(at,0x00000001u,payload);f.tag(payload,0x07001234u);f.start(1,at);
        f.advance(2);rejects([&]{(void)f.v1.read_register(0x30);},"before write/command");
        f.advance(4);require(f.v1.read_register(0x30)==0x1234 && (f.dma.ee_dma_read(0x1000E010u)&2u),
            "REFE tag and payload independently select RAM or SPR");
    }
    Fixture next;next.tag(spr|0x100,0x20000001u,0x200);next.tag(spr|0x110,0x07001111u);
    next.tag(0x200,0x20000000u,spr|0x300);next.tag(spr|0x300,0x70000001u);
    next.tag(spr|0x310,0x07002222u);next.start(1,spr|0x100);next.advance(4);
    require(next.v1.read_register(0x30)==0x1111 && next.dma.ee_dma_read(0x10009030u)==0x200,
        "NEXT sends inline SPR data before switching tag storage to RAM");
    next.advance(10);require(next.v1.read_register(0x30)==0x2222 && (next.dma.ee_dma_read(0x1000E010u)&2u),
        "NEXT may switch back to a real SPR END tag");
    Fixture cnt;cnt.tag(spr|0x100,0x10000001u);cnt.tag(spr|0x110,0x07003333u);
    cnt.tag(spr|0x120,0x70000000u);cnt.start(1,spr|0x100);cnt.advance(4);
    require(cnt.dma.ee_dma_read(0x10009030u)==(spr|0x120),"CNT updates SPR TADR as bytes cross the bus");
    cnt.advance(6);require(cnt.v1.read_register(0x30)==0x3333 && (cnt.dma.ee_dma_read(0x1000E010u)&2u),"CNT follows real SPR chain");
}
void gif_and_boundaries() {
    Fixture f;f.tag(spr|0x3FE0,0x8001u,0x10000000u,0xEu); // packed A+D
    f.tag(spr|0x3FF0,0x12345678u,0xFFFFFFFFu,0x62u); // LABEL
    f.start(2,spr|0x3FE0,false,false,2);f.advance(2);
    require(f.gif.packets().empty() && !(f.dma.ee_dma_read(0x1000E010u)&4u),"last SPR packet waits for actual payload");
    f.advance(4);require(f.gif.packets().size()==1 && f.gs.read(kGsSiglblid+4,4)==0x12345678u &&
        f.dma.ee_dma_read(0x1000A010u)==(spr|0x4000),"GIF reads exactly through final SPR qword");
    Fixture absent;absent.dma.attach_ee_ram(absent.ram,&absent.unknown);
    rejects([&]{absent.start(1,spr|0x100,false,false,1);},"scratchpad range");
    Fixture crossing;rejects([&]{crossing.start(1,spr|0x3FF0,false,false,2);},"scratchpad range");
    Fixture alias;rejects([&]{alias.start(1,spr|0x4000,false,false,1);},"scratchpad range");
    Fixture empty;empty.tag(spr|0x3FF0,0x70000000u);empty.start(1,spr|0x3FF0);empty.advance(2);
    require(empty.dma.ee_dma_read(0x1000E010u)&2u,"zero-payload final tag fits at exact SPR edge");
    Fixture bad;bad.tag(spr|0x3FF0,0x70000001u);bad.start(1,spr|0x3FF0);
    rejects([&]{bad.advance(2);},"payload outside");require(!(bad.dma.ee_dma_read(0x1000E010u)&2u),"unsupported crossing cannot fabricate completion");
    for(const bool tag:{false,true}) {
        Fixture u;u.tag(spr|0x100,0x70000001u);u.tag(spr|0x110,0x07007777u);
        u.unknown[spr|(tag ? 0x100u : 0x110u)]=1;u.start(1,spr|0x100);
        rejects([&]{u.advance(4);},"unknown source bits");
        rejects([&]{(void)u.v1.read_register(0x30);},"before write/command");
        require(!(u.dma.ee_dma_read(0x1000E010u)&2u),"unknown SPR bytes cannot fabricate completion");
    }
    Fixture preserved;preserved.tag(spr|0x100,0x70000001u);preserved.tag(spr|0x110,0x07005555u);
    const std::vector<std::uint8_t> nop(4);preserved.dma.load_program(nop,0,0);
    preserved.start(1,spr|0x100);preserved.advance(4);
    require(preserved.v1.read_register(0x30)==0x5555,"loading an IOP development image preserves shared EE SPR attachment");
}
void memory_attachment() {
    Memory memory;std::vector<std::uint8_t> rom(4u*1024u*1024u);reset_native_ee_fixture(memory,rom);
    std::uint64_t clock=0;memory.set_clock(&clock);
    memory.tlb_write(0,{0,0x70000000u,0x80000007u,7u});
    memory.set_translation_context(0,false);
    memory.write32(0x70000100u,0x70000001u);memory.write32(0x70000104u,0);
    memory.write32(0x70000110u,0x07006666u);
    memory.write32(0xB000E000u,1);memory.write32(0xB0009020u,0);
    memory.write32(0xB0009030u,spr|0x100);memory.write32(0xB0009000u,0x105);
    clock=4;
    require(memory.read32(0xB0003C30u)==0x6666,"Memory CPU stores and graphics DMA use one actual scratchpad allocation");
}
void mfifo_transport() {
    for(unsigned channel:{1u,2u}) {
        Fixture f;VuControl vu;
        const auto drain=channel==1u ? 0x10009000u : 0x1000A000u;
        f.dma.ee_dma_write(0x1000E040u,0x70); // eight qwords
        f.dma.ee_dma_write(0x1000E050u,0x200);
        f.dma.ee_dma_write(0x1000E000u,1u|((channel+1u)<<2));
        f.dma.ee_dma_write(0x1000D010u,0x260);
        f.dma.ee_dma_write(drain+0x20,0);f.dma.ee_dma_write(drain+0x30,0x260);
        f.dma.ee_dma_write(drain,channel==1u ? 0x145 : 0x105);
        f.dma.advance_graphics(2,f.v0,f.v1,f.gif,f.gs,vu);
        require((f.dma.ee_dma_read(0x1000E010u)&0x4000u) &&
            (f.dma.ee_dma_read(drain)&0x100u) && f.gif.packets().empty(),
            "an empty MFIFO holds STR and raises MEIS without reading stale RAM");
        f.dma.ee_dma_write(0x1000E010u,0x40004000u);
        require(!f.dma.ee_dma_interrupt_pending(),"acknowledging MFIFO empty does not fabricate an interrupt");
        if(channel==2u) {
            f.tag(spr|0x3FF0u,0x70000002u);
            f.tag(spr,0x8001u,0x10000000u,0xEu);
            f.tag(spr|0x10u,0x12345678u,0xFFFFFFFFu,0x62u);
        } else {
            f.tag(spr|0x3FF0u,0x70000002u,0,0,0x30000000u); // TTE STROW
            f.tag(spr,0x11111111u,0x22222222u,0x33333333u,0x44444444u);
            f.tag(spr|0x10u,0,0,0,0);
        }
        f.dma.ee_dma_write(0x1000D020u,3);f.dma.ee_dma_write(0x1000D080u,0x3FF0);
        f.dma.ee_dma_write(0x1000D000u,0x100);
        rejects([&]{f.dma.ee_dma_write(0x1000E040u,0x30);},"ring changed");
        f.dma.advance_graphics(8,f.v0,f.v1,f.gif,f.gs,vu);
        require(f.dma.ee_dma_read(0x1000D010u)==0x210 && f.dma.ee_dma_read(0x1000D080u)==0x20 &&
            !f.dma.ee_dma_read(0x1000D020u) && (f.dma.ee_dma_read(0x1000E010u)&0x100u),
            "fromSPR copies three real qwords with independent scratchpad and ring wrapping");
        require(f.dma.ee_dma_read(drain+0x20)==0 && f.gif.packets().empty(),
            "producer completion does not fabricate drain progress");
        f.dma.advance_graphics(14,f.v0,f.v1,f.gif,f.gs,vu);
        require(!(f.dma.ee_dma_read(drain)&0x100u) && f.dma.ee_dma_read(drain+0x10)==0x210 &&
            (f.dma.ee_dma_read(0x1000E010u)&(1u<<channel)),"MFIFO END completes only after actual wrapped payload");
        if(channel==2u)require(f.gif.packets().size()==1 && f.gs.read(kGsSiglblid+4,4)==0x12345678u,
            "MFIFO GIF consumes the original LABEL packet");
        else for(unsigned w=0;w<4;++w)require(f.v1.read_register(0x100+w*16u)==0x11111111u*(w+1u),
            "MFIFO VIF1 receives TTE and exact payload words");
    }
    Fixture unknown;VuControl vu;
    unknown.tag(spr|0x3FF0u,1,2,3,4);unknown.unknown[spr|0x3FF4u]=0x80;
    unknown.dma.ee_dma_write(0x1000E000u,1);unknown.dma.ee_dma_write(0x1000D010u,0x200);
    unknown.dma.ee_dma_write(0x1000D020u,1);unknown.dma.ee_dma_write(0x1000D080u,0x3FF0);
    unknown.dma.ee_dma_write(0x1000D000u,0x100);unknown.dma.set_ee_dma_suspended(true);
    unknown.dma.advance_graphics(100,unknown.v0,unknown.v1,unknown.gif,unknown.gs,vu);
    require(unknown.dma.ee_dma_read(0x1000D020u)==1,"CPND holds fromSPR without consuming a qword");
    unknown.dma.set_ee_dma_suspended(false);unknown.dma.advance_graphics(102,unknown.v0,unknown.v1,unknown.gif,unknown.gs,vu);
    require(unknown.unknown.at(0x204)==0x80 && unknown.unknown.at(spr|0x3FF4u)==0x80 &&
        unknown.ram[0x204]==2 && unknown.dma.ee_dma_read(0x1000D080u)==0,
        "fromSPR transports unknown masks without consuming or inventing source certainty");
    Fixture partial;VuControl pv;
    partial.tag(spr,0x70000002u);partial.tag(spr|0x10u,0x8001u,0x10000000u,0xEu);
    partial.tag(spr|0x20u,0xABCDu,0xFFFFFFFFu,0x62u);
    for(const auto [at,value]:std::initializer_list<std::pair<unsigned,unsigned>>{
        {0x1000E040,0x70},{0x1000E050,0x200},{0x1000E000,0xD},{0x1000D010,0x200},
        {0x1000A020,0},{0x1000A030,0x200},{0x1000A000,0x105},
        {0x1000D020,1},{0x1000D080,0},{0x1000D000,0x100}})partial.dma.ee_dma_write(at,value);
    partial.dma.advance_graphics(6,partial.v0,partial.v1,partial.gif,partial.gs,pv);
    require((partial.dma.ee_dma_read(0x1000A000)&0x100u) && partial.dma.ee_dma_read(0x1000A020)==2 &&
        (partial.dma.ee_dma_read(0x1000E010)&0x4000u) && partial.gif.packets().empty(),
        "a published END tag cannot consume an unpublished inline payload or complete early");
    partial.dma.ee_dma_write(0x1000E010,0x4000);
    partial.dma.ee_dma_write(0x1000D020,2);partial.dma.ee_dma_write(0x1000D000,0x100);
    partial.dma.advance_graphics(14,partial.v0,partial.v1,partial.gif,partial.gs,pv);
    require(partial.gif.packets().size()==1 && partial.gs.read(kGsSiglblid+4,4)==0xABCDu &&
        (partial.dma.ee_dma_read(0x1000E010)&4u),"publishing the remaining bytes resumes the held END payload");
    Fixture strict;
    strict.dma.ee_dma_write(0x1000A030u,0x100);
    rejects([&]{strict.dma.ee_dma_write(0x1000A000u,0x105);},"QWC is unknown");
    require(!(strict.dma.ee_dma_observe(0x1000A000u).value&0x100u),"unknown initial QWC cannot arm GIF");
}
void numeric_packet_transport() {
    for(const bool retired_path:{false,true})for(const bool certified:{false,true}) {
        Memory memory;std::vector<std::uint8_t> rom(4u*1024u*1024u);reset_native_ee_fixture(memory,rom);
        std::uint64_t clock=0;memory.set_clock(&clock);memory.tlb_write(0,{0,0x70000000u,0x80000007u,7u});memory.set_translation_context(0,false);
        const ObservedWord scale{0x447FFE00u,0x1FFu};
        memory.store128_observed(0x70003A00u,{{scale.value,scale.unknown},{0x3F80000000000000ull,0}});
        if(certified)memory.remember_fpu_relation(0x70003A00u,scale,
            {memory.allocate_fpu_origin(),0,136,false,scale.value,scale.unknown,0x447FFEFFu,0x447FFF00u});
        EEState state;state.set_reg32(1,0x70003A00u);state.set_reg32(3,0x70000110u);
        const auto instruction=[&](std::uint32_t word) {
            state.pc=0x80001000u;memory.write32(state.pc,word);std::uint64_t retired=0;
            if(retired_path){ExecutionContext ctx{state,memory,retired,100,0xFFFFFFFFu};compiled_fixture_step(ctx);}
            else if(word==0x78220000u)ee::exec<MipsOpcode::Lq>(state,memory,state.pc,word);
            else ee::exec<MipsOpcode::Sq>(state,memory,state.pc,word);
        };
        instruction(0x78220000u);instruction(0x7C620000u);
        require(state.gpr[2].high==0x3F80000000000000ull && state.unknown_gpr_bits[2]==0x1FFu,
            "LQ/SQ preserve all raw lanes and their uncertain bits");
        const auto copied=memory.fpu_relation(0x70000110u,scale);
        require(certified ? copied.origin && copied.minimum==0x447FFEFFu && copied.maximum==0x447FFF00u : !copied.origin,
            "native helper and reference LQ/SQ copy only proven numeric domains");
        memory.write128(0x700000F0u,0x70000003u,0);
        memory.write128(0x70000100u,0x0500000001000101ull,0x6C01000000000000ull);
        memory.write128(0x70000120u,0x14000000u,0);
        memory.write128(0xB100C010u,0xC0400000u,0); // -3.0
        memory.write128(0xB100C020u,0x45000000u,0); // +2048.0
        constexpr auto nop=0x8000033Cu,unop=0x2FFu;
        const std::uint32_t lower[]{0x01E10001u,0x01E20000u,0x01E40002u,nop,nop,nop,0x03002803u,nop,nop};
        const std::uint32_t upper[]{unop,unop,unop,0x010208EAu,0x010418E8u,0x0105197Du,unop,unop|0x40000000u,unop};
        for(unsigned n=0;n<9;++n)memory.write64(0xB1008000u+n*8u,lower[n]|(std::uint64_t{upper[n]}<<32u));
        memory.write32(0xB000E040u,0x70);memory.write32(0xB000E050u,0x2000);memory.write32(0xB000E000u,9);
        memory.write32(0xB000D010u,0x2000);memory.write32(0xB000D020u,4);memory.write32(0xB000D080u,0xF0);
        memory.write32(0xB0009020u,0);memory.write32(0xB0009030u,0x2000);memory.write32(0xB0009000u,0x105);memory.write32(0xB000D000u,0x100);
        clock=200;require(memory.read32(0xB0009020u)==0 && !(memory.read32(0xB0009000u)&0x100u),"real fromSPR/MFIFO/VIF transfers finish");
        const auto raw=memory.load32_observed(0xB100C000u),fixed=memory.load32_observed(0xB100C030u);
        require(raw.value==scale.value && raw.unknown==scale.unknown,"UNPACK retains uncertain source bits instead of choosing a float");
        require(certified ? fixed.value==0xFFFFC001u && !fixed.unknown : fixed.unknown!=0,
            "only the transported proof makes both possible inputs produce the same independently expected fixed coordinate");
        if(certified) {
            const ObservedWord other{0x3F800000u,1};memory.store32_observed(0xA0002100u,other);
            memory.remember_fpu_relation(0xA0002100u,other,{memory.allocate_fpu_origin(),0,127,false,other.value,other.unknown,other.value,other.value+1u});
            memory.store128_observed(0x70000200u,{{scale.value,scale.unknown},{0,0}});
            memory.write32(0xB000D010u,0x2020);memory.write32(0xB000D020u,1);memory.write32(0xB000D080u,0x200);memory.write32(0xB000D000u,0x100);
            clock=204;(void)memory.read32(0xB000D020u);
            require(!memory.fpu_relation(0xA0002020u,scale).origin && memory.fpu_relation(0xA0002100u,other).origin,
                "DMA invalidates a replaced word even for an identical bit hull while preserving unrelated RAM provenance");
            memory.store32_observed(0x70000110u,scale);
            require(!memory.fpu_relation(0x70000110u,scale).origin,"CPU replacement also invalidates matching-byte scratchpad provenance");
        }
    }
}

}
int main() try {source_selection();gif_and_boundaries();memory_attachment();mfifo_transport();numeric_packet_transport();
    std::cout<<"Graphics DMA transfers shared SPR/RAM bytes, tags, TTE, boundaries and unknown data\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
