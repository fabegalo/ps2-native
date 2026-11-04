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
}
int main() try {
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
    refuses([&]{ipu.write_command(0x50000001);});
    refuses([&]{ipu.write_command(0x10000000);});
    check(!input(ipu,0)); check(ipu.write_command(127));
    check(ipu.read_position()==127);
    refuses([&]{ipu.write_command(0x60000000);});
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
