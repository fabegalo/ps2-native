#include "ps2native/engine/console.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace ps2native::engine;
static void check(bool v,const char* why) {if(!v)throw std::runtime_error(why);}
static std::array<std::uint8_t,16384> image;
static std::array<VuPairFn,2048> functions;
struct Result {std::array<std::uint32_t,4> data;std::uint64_t tick;std::uint32_t pc,mac,status;};
static Result run(unsigned n,bool native) {
    Console c;c.initialize_sif();
    std::copy_n(image.data(),n ? 16384 : 4096,n ? c.vu1_code.data() : c.vu0_code.data());
    const VuProgram program={n,image.data(),n ? 16384u : 4096u,functions.data()};
    if(native)register_vu_programs(&program,1);
    const auto data=n ? 0x1100c000u : 0x11004000u;
    const std::uint32_t input[]={0x3f800000,0x40000000,0x40400000,0x40800000};
    for(unsigned k=0;k<4;++k)ps2::st32(data+4*k,input[k]);
    // A real IOP store occurs while VU load/arithmetic/store pairs wait on dependencies.
    c.iop_write(0x1000,0xac040900,4);c.iop_write(0x1004,0x03e00008,4);c.iop_write(0x1008,0,4);
    c.iop.pc=0x1000;c.iop.gpr[4]=0x55;c.iop.gpr[31]=0xfffffff0;c.iop_active=true;c.iop_next_tick=8;
    c.vu[n].unit.top=37;c.vu[n].unit.itop=91;
    c.start_vu(n,0);c.wait_vu(n);
    check(c.vu[n].unit.top==37 && c.vu[n].unit.itop==91,"Starting a microprogram preserves VIF TOP/ITOP latches");
    check(c.iop_read(0x900,4)==0x55,"IOP progresses on the common clock during VU execution");
    Result out{};for(unsigned k=0;k<4;++k)out.data[k]=ps2::ld32(data+16+4*k);
    const std::array<std::uint32_t,4> expected={0x40000000,0x40800000,0x40c00000,0x41000000};
    check(out.data==expected,"Original VU load/add/store semantics update the EE-visible data RAM");
    const auto& v=c.vu[n];const auto& r=n ? c.vu1_regs : c.ee.vu0;
    check(v.pairs==4 && (native ? v.aot_pairs==4 && !v.interpreted_pairs : v.interpreted_pairs==4 && !v.aot_pairs),"Explicit execution mode counters");
    check(c.ticks==10,"VU dependency stalls advance the shared logical clock");
    out.tick=c.ticks;out.pc=r.tpc;out.mac=r.mac;out.status=r.status;
    // Byte accesses use the same VU synchronization/invalidation boundary as word accesses.
    c.ee.budget-=1;(void)ps2::slow_ld8(data);check(c.ticks==11 && c.ee.budget==100000,"VU byte reads synchronize the logical clock");
    const auto code=n ? 0x11008000u : 0x11000000u;
    ps2::slow_st8(code,0x3c);check(c.vu_selected[n]==nullptr,"VU byte code writes invalidate native selection");
    ps2::slow_st8(code,0);
    // The source bytes, not an entry address alone, select native code.
    ps2::st32(n ? 0x11008000 : 0x11000000,0x8000033c);
    c.start_vu(n,0);check(c.vu_selected[n]==nullptr,"Changed microcode invalidates its native translation");
    bool refused=false;try {ps2::ctc2(c.ee,28,2);}catch(const std::runtime_error&) {refused=true;}
    check(refused,"Running-unit reset is explicit until its control side effects are integrated");
    return out;
}
int main() {
    try {
        const std::uint32_t words[]={0x01e10000,0x000002ff,0x8000033c,0x01e108a8,0x03e01001,0x400002ff,0x8000033c,0x000002ff};
        std::memcpy(image.data(),words,sizeof(words));
        functions.fill(&native_vu_pair<0,0>);
        functions[0]=&native_vu_pair<0x01e10000,0x000002ff>;
        functions[1]=&native_vu_pair<0x8000033c,0x01e108a8>;
        functions[2]=&native_vu_pair<0x03e01001,0x400002ff>;
        functions[3]=&native_vu_pair<0x8000033c,0x000002ff>;
        for(unsigned n=0;n<2;++n) {
            const auto a=run(n,false),b=run(n,true);
            check(a.data==b.data && a.tick==b.tick && a.pc==b.pc && a.mac==b.mac && a.status==b.status,"Native/interpreted matched-boundary state and timing");
        }
        {
            Console c;
            ps2::st32(0x11000000,0x7e000000);ps2::st32(0x11000004,0x000002ff);
            c.start_vu(0,0);
            bool failed=false;try {c.wait_vu(0);}catch(const std::runtime_error&) {failed=true;}
            check(failed,"Unknown VU instructions must stop, not fabricate completion");
        }
        std::puts("engine-vu: native/interpreted VU0/VU1, shared EE/IOP memory/clock and invalidation PASS");
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
