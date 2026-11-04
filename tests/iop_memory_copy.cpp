// Byte-oracle tests for real R3000A copy instructions and unknown source data.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> image(m20::Assembler& a) {
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
void idle(m20::Assembler& a) {a.label("idle");a.beq(0,0,"idle");a.nop();}
}
int main() try {
    for(unsigned source_offset=0;source_offset<4;++source_offset)
    for(unsigned dest_offset=0;dest_offset<4;++dest_offset)
    for(unsigned unknown=0;unknown<=4;++unknown)
    for(const bool reversed:{false,true})for(const bool old_register_known:{false,true}) {
        m20::Assembler a(kEeResetVector);a.la(8,0xA0000300u+source_offset);a.la(10,0xA0000400u+dest_offset);
        if(old_register_known)a.la(9,0xDEADBEEFu);
        if(reversed){a.raw((0x26u<<26)|(8u<<21)|(9u<<16));a.raw((0x22u<<26)|(8u<<21)|(9u<<16)|3u);}
        else {a.raw((0x22u<<26)|(8u<<21)|(9u<<16)|3u);a.raw((0x26u<<26)|(8u<<21)|(9u<<16));}
        a.nop();
        if(reversed){a.raw((0x2Eu<<26)|(10u<<21)|(9u<<16));a.raw((0x2Au<<26)|(10u<<21)|(9u<<16)|3u);}
        else {a.raw((0x2Au<<26)|(10u<<21)|(9u<<16)|3u);a.raw((0x2Eu<<26)|(10u<<21)|(9u<<16));}
        a.label("copied");a.addiu(11,9,1);a.la(10,0xA0000500u);a.sw(11,0,10);idle(a);
        const auto checkpoint=(a.at("copied")-kEeResetVector)/4u;const auto rom=image(a);Iop iop;iop.reset(rom,nullptr);
        constexpr std::uint8_t data[]{0x31u,0x62u,0x93u,0xC4u};
        for(unsigned n=0;n<4;++n)if(n!=unknown)iop.ee_write(0x300u+source_offset+n,1,data[n]);
        iop.ee_write(0x3FFu+dest_offset,1,0x5Au);iop.ee_write(0x404u+dest_offset,1,0xA5u);
        iop.run_until(checkpoint);
        require(!iop.stopped(),"unaligned loads/stores transport opaque bytes before their values are consumed");
        for(unsigned n=0;n<4;++n) {
            if(n==unknown)rejects([&]{(void)iop.ee_read(0x400u+dest_offset+n,1);},"has not been written");
            else require(iop.ee_read(0x400u+dest_offset+n,1)==data[n],"all alignments and merge orders preserve each independently known source byte");
        }
        require(iop.ee_read(0x3FFu+dest_offset,1)==0x5Au && iop.ee_read(0x404u+dest_offset,1)==0xA5u,
                "unaligned stores leave both neighboring bytes untouched");
        iop.run_until(checkpoint+5u);
        if(unknown<4)require(iop.stopped(),"later full arithmetic cannot turn an opaque source byte into a fabricated value");
        else require(!iop.stopped() && iop.ee_read(0x500,4)==0xC4936232u,
                     "the complete merge pair replaces every prior unknown register bit, including load forwarding");
    }
    {
        m20::Assembler a(kEeResetVector);a.la(8,0xA0000300u);a.la(10,0xA0000400u);
        a.lw(9,0,8);a.nop();a.sw(9,0,10);a.sb(9,0x100,10);a.sh(9,0x102,10);
        a.lbu(9,1,8);a.nop();a.r(0,9,9,8,0x02u);a.sw(9,0x200,10);
        a.raw((0x21u<<26)|(8u<<21)|(9u<<16)|0x10u);a.nop();a.r(0,9,9,16,0x02u);a.sw(9,0x204,10);
        a.lhu(9,0x10,8);a.nop();a.r(0,9,9,16,0x02u);a.sw(9,0x208,10);
        a.label("copied");a.raw((0x20u<<26)|(8u<<21)|(9u<<16)|0x12u);a.nop();a.addiu(9,9,1);idle(a);
        const auto checkpoint=(a.at("copied")-kEeResetVector)/4u;const auto rom=image(a);Iop iop;iop.reset(rom,nullptr);
        iop.ee_write(0x300,1,0x12);iop.ee_write(0x302,1,0x34);iop.ee_write(0x303,1,0x80);iop.ee_write(0x311,1,0x80);
        iop.run_until(checkpoint);
        require(!iop.stopped() && iop.ee_read(0x400,1)==0x12 && iop.ee_read(0x402,2)==0x8034u &&
            iop.ee_read(0x500,1)==0x12 && iop.ee_read(0x502,1)==0x12,"LW/SW and narrow stores retain their known byte lanes");
        rejects([&]{(void)iop.ee_read(0x401,1);},"has not been written");
        rejects([&]{(void)iop.ee_read(0x503,1);},"has not been written");
        require(iop.ee_read(0x600,4)==0 && iop.ee_read(0x604,4)==0xFFFFu && iop.ee_read(0x608,4)==0,
                "unsigned extensions and a known sign byte are established even when other source bytes are unknown");
        iop.run_until(checkpoint+3u);require(iop.stopped(),"an unknown signed byte retains its unknown sign and data");
    }
    std::cout<<"IOP opaque-byte copies, all alignments/orders, load forwarding and sign/zero extension passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
