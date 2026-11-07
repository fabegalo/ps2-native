#include "ps2native/ipu.hpp"
#include <iostream>
#include <stdexcept>
using namespace ps2native;
namespace {
void check(bool value) {if(!value) throw std::runtime_error("IPU table assertion failed");}
template<class F> void refuses(F f) {try {f();} catch(const DeviceRefusal&) {return;} throw std::runtime_error("IPU expected refusal");}
bool input(IpuRegisters& ipu,unsigned start) {
    std::uint64_t low=0,high=0;
    for(unsigned i=0;i<8;++i) {low|=std::uint64_t(start+i)<<(i*8);high|=std::uint64_t(start+i+8)<<(i*8);}
    return ipu.write_fifo(low,high);
}
void bit_addressed_tables() {
    // MSB-first rotations of 0xA5: independent expected wire bytes.
    constexpr std::array<std::uint8_t,8> expected{0xA5,0x4B,0x96,0x2D,0x5A,0xB4,0x69,0xD2};
    for(unsigned bp=0;bp<128;++bp) for(unsigned fb=0;fb<64;++fb) {
        IpuRegisters ipu;
        check(ipu.write_command(bp));
        check(!ipu.write_command(0x58000000u|fb));
        const auto qwords=(bp+fb+512u+127u)/128u;
        for(unsigned q=0;q<qwords;++q) {
            const bool complete=ipu.write_fifo(0xA5A5A5A5A5A5A5A5ull,0xA5A5A5A5A5A5A5A5ull);
            check(complete==(q+1==qwords));
            check(ipu.quantization_table(1).has_value()==complete);
            check(bool(ipu.observe_control().value&0x80000000u)==!complete);
        }
        for(auto byte:*ipu.quantization_table(1)) check(byte==expected[(bp+fb)&7u]);
        check(!ipu.quantization_table(0));
        check(ipu.buffered_bytes()==(((bp+fb)&127u) ? 16u : 0u));
    }
    // A pending replacement retains the old table, including across a copy.
    IpuRegisters ipu;
    check(ipu.write_command(3));
    check(!ipu.write_command(0x50000001u));
    for(unsigned q=0;q<5;++q)
        check(ipu.write_fifo(0xF0DEBC9A78563412ull,0xF0DEBC9A78563412ull)==(q==4));
    constexpr std::array<std::uint8_t,8> shifted{0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0x01};
    for(unsigned i=0;i<64;++i) check((*ipu.quantization_table(0))[i]==shifted[i&7]);
    const auto old=ipu.quantization_table(0);
    check(!ipu.write_command(0x50000000u));
    check(ipu.quantization_table(0)==old);
    auto resumed=ipu;
    for(auto* target:{&ipu,&resumed}) {
        for(unsigned q=0;q<4;++q)
            check(target->write_fifo(0xA5A5A5A5A5A5A5A5ull,0xA5A5A5A5A5A5A5A5ull)==(q==3));
    }
    check(ipu.quantization_table(0)==resumed.quantization_table(0));
    IpuRegisters crossing;
    check(crossing.write_command(127));check(!crossing.write_command(0x5000003Fu));
    for(unsigned q=0;q<6;++q) check(input(crossing,q*16)==(q==5));
    constexpr std::array<std::uint8_t,8> crossing_prefix{0xC6,0x06,0x46,0x86,0xC7,0x07,0x47,0x87};
    for(unsigned i=0;i<8;++i) check((*crossing.quantization_table(0))[i]==crossing_prefix[i]);
    check((*crossing.quantization_table(0))[31]==0x8D && (*crossing.quantization_table(0))[32]==0xCE &&
          (*crossing.quantization_table(0))[63]==0x95 && crossing.buffered_bytes()==16);
    // SETVQ starts at the current BP too, without an FB option.
    for(unsigned bp=0;bp<128;++bp) {
        IpuRegisters vq;
        check(vq.write_command(bp));check(!vq.write_command(0x60000000u));
        for(unsigned q=0;q<(bp ? 3u : 2u);++q)
            check(vq.write_fifo(0xA5A5A5A5A5A5A5A5ull,0xA5A5A5A5A5A5A5A5ull)==(q==(bp ? 2u : 1u)));
        for(auto byte:*vq.vector_table()) check(byte==expected[bp&7]);
    }
}
}
int main() try {
    bit_addressed_tables();
    IpuRegisters ipu;
    refuses([&]{ipu.write_command(0x50000000);}); // Unknown bit position is not alignment.
    check(ipu.write_command(0));
    for(unsigned i=0;i<8;++i) check(!input(ipu,i*16));
    check(ipu.buffered_bytes()==128 && (ipu.observe_control().unknown&15)==15);
    refuses([&]{(void)ipu.read_position();}); // Internal prefetch timing is not fabricated.
    refuses([&]{input(ipu,128);});
    check(ipu.write_command(0x50000000));
    check(ipu.write_command(0x58000000));
    for(unsigned i=0;i<64;++i) {check((*ipu.quantization_table(0))[i]==i);check((*ipu.quantization_table(1))[i]==64+i);}
    check(ipu.read_position()==0);
    check(!ipu.write_command(0x60000000));
    check(ipu.observe_control().value&0x80000000u);
    refuses([&]{ipu.write_command(0);});
    check(!input(ipu,0)); check(input(ipu,16));
    check(!(ipu.observe_control().value&0x80000000u));
    for(unsigned i=0;i<32;++i) check((*ipu.vector_table())[i]==i);
    check(ipu.write_command(0x910001FF));
    check(ipu.thresholds()==0x010001FF);
    refuses([&]{ipu.write_command(0x92000000);});
    refuses([&]{ipu.write_command(0x50000040);});
    refuses([&]{ipu.write_command(0x10000000);});
    check(!input(ipu,0)); check(ipu.write_command(127));
    check(ipu.read_position()==127);
    check(!ipu.write_command(0x60000000));
    check(ipu.write_control(0x40000000));
    check(ipu.write_command(0)); check(!ipu.write_command(0x50000000));
    check(ipu.write_control(0x40000000)); // Reset aborts a pending command.
    check(!ipu.write_control(0x40000000));
    check(!ipu.quantization_table(0) && !ipu.vector_table());
    check(!(ipu.observe_control().value&0x800000FFu));
    refuses([&]{(void)ipu.read_position();});
    refuses([&]{(void)ipu.read_control();}); // Still no invented reset/status bits.
    std::cout<<"IPU table bytes, FIFO, pending completion and reset passed\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
