#include "boot_checks.hpp"
#include "bios_program.hpp"
#include "ee_native_fixture.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;
namespace {
void initialize(Memory& m) {
    reset_native_ee_fixture(m,m20::build_firmware().rom);
    m.diagnostic_data_cache();
    m.set_cache_config(0x10443u);
    m.set_cpu_context(0,0);
}
ObservedWord op(Memory& m,unsigned command,std::uint32_t address,ObservedWord tag={}) {
    return m.data_cache_operation(command,address,tag);
}
void visibility_and_tags() {
    Memory m;initialize(m);
    auto tag=op(m,0x10,0);
    require(tag.value==0 && tag.unknown==0xFFFFFF87u,"reset state bits known, PFN and reserved tag bits unknown");
    op(m,0x12,0,{0,0});
    require(op(m,0x10,0).unknown==0xF87u,"guest tag initialization defines PFN only");
    m.write32(0xA0002000u,0x11223344u);
    require(m.fetch32(0x80002000u)==0x11223344u,"instruction fetch reads backing storage");
    require(m.code_matches(0x80002000u,0x11223344u),"warm native executable identity");
    require(m.read32(0x80002000u)==0x11223344u,"cache miss refills real RAM");
    tag=op(m,0x10,0);
    require(tag.value==0x2030u && tag.unknown==0xF87u,"refill records physical tag, valid and LRF");
    m.write16(0x80002002u,0xABCDu);
    require(m.fetch32(0x80002000u)==0x11223344u,"instruction fetch does not see a dirty D-cache store");
    require(m.code_matches(0x80002000u,0x11223344u),"dirty data cache does not invalidate backing code bytes");
    const auto prior_tag=op(m,0x10,0);
    require(m.diagnostic_ram(0x2000,4).value==0x11223344u &&
            op(m,0x10,0).value==prior_tag.value,"diagnostic reads backing RAM without cache refill/writeback");
    require(m.read32(0xA0002000u)==0x11223344u,"cached store is invisible to backing bus");
    require(m.read32(0x80002000u)==0xABCD3344u,"cached store preserves other byte lanes");
    require(op(m,0x10,0).value==0x2070u,"store dirties actual line");
    m.write32(0xA0002000u,0x87654321u);
    require(m.read32(0x80002000u)==0xABCD3344u,"external bus write does not snoop cache");
    op(m,0x1C,0x80003000u);
    require(m.read32(0xA0002000u)==0x87654321u,"hit writeback miss does nothing");
    op(m,0x1C,0x80002000u);
    require(m.fetch32(0x80002000u)==0xABCD3344u,"instruction fetch sees cache writeback immediately");
    require(!m.code_matches(0x80002000u,0x11223344u) && m.code_matches(0x80002000u,0xABCD3344u),
            "writeback invalidates old native identity and validates new bytes");
    require(m.read32(0xA0002000u)==0xABCD3344u && op(m,0x10,0).value==0x2030u,
            "hit writeback publishes dirty data and preserves valid/PFN/LRF");
    m.write32(0x80002000u,7);
    op(m,0x16,0);
    require(m.read32(0xA0002000u)==0xABCD3344u && op(m,0x10,0).value==0x2010u,
            "index invalidate discards dirty data, keeps PFN/LRF");
    require(m.read32(0x80002000u)==0xABCD3344u,"invalidation forces refill");
    m.write128(0x80002030u,0x1122334455667788ull,0x99AABBCCDDEEFF00ull);
    op(m,0x18,0x80002000u);
    require(m.read64(0xA0002030u)==0x1122334455667788ull &&
            m.read64(0xA0002038u)==0x99AABBCCDDEEFF00ull,"SQ writeback includes entire cache line");
}
void replacement_and_knowledge() {
    Memory m;initialize(m);
    m.store32_observed(0xA0004000u,{0xF0000100u,0x0F0000FFu});
    m.store32_observed(0xA0004004u,{0x12345678u,0x0000FF00u});
    const auto diagnostic=m.diagnostic_ram(0x4001,6);
    require(diagnostic.value==0x340078F00001ull && diagnostic.unknown==0x00FF000F0000ull,
            "diagnostic unaligned range preserves knowledge across words");
    rejects([&]{(void)m.diagnostic_ram(Memory::kMainRamBytes-1,2);},"outside backing");
    rejects([&]{(void)m.diagnostic_ram(0x10000000u,4);},"outside backing");
    auto data=m.load32_observed(0x80004000u);
    require(data.value==0xF0000100u && data.unknown==0x0F0000FFu,"refill retains per-bit RAM knowledge");
    m.store16_observed(0x80004002u,{0x1122u,0x00F0u});
    op(m,0x14,0);
    data=m.load32_observed(0xA0004000u);
    require(data.value==0x11020100u && data.unknown==0x00F000FFu,"writeback retains masked byte knowledge");
    rejects([&]{(void)m.read32(0x80004000u);},"unknown D-cache");
    // LRF selects way 0, then 1, then 0 even if way 1 is invalidated.
    initialize(m);
    m.write32(0x80001000u,1);
    m.write32(0x80002000u,2);
    op(m,0x16,1);
    m.write32(0x80003000u,3);
    require(m.read32(0xA0001000u)==1 && m.read32(0xA0002000u)==0,
            "LRF eviction writes way 0; invalidated way 1 loses dirty data");
    require(op(m,0x10,0).value==0x3060u,"LRF flips on refill, not hits");
    op(m,0x13,0,{0xA0B0C0D0u,0x00FF0000u});
    data=op(m,0x11,0);
    require(data.value==0xA000C0D0u && data.unknown==0x00FF0000u,"index data transport preserves unknown bits");
}
void mapping_and_locks() {
    Memory m;initialize(m);
    m.tlb_write(0,{0,0x00400000u,0x81Fu,0x85Fu}); // PFN 0x20, mode 3
    m.write32(0x00400000u,0x1234u);
    require((op(m,0x10,0).value&0xFFFFF000u)==0x20000u,"tag uses physical PFN, not virtual page");
    require(m.read32(0x80020000u)==0x1234u,"cached physical alias hits same line");
    m.set_cache_config(0x443u);
    require(m.read32(0x80020000u)==0,"DCE off bypasses without flushing");
    op(m,0x14,0);
    require(m.read32(0x80020000u)==0x1234u,"CACHE operates with DCE off");
    m.set_cache_config(0x10442u);
    require(m.read32(0x80020000u)==0x1234u,"K0 uncached bypass");
    rejects([&]{op(m,0x18,0x80020000u);},"cached address");
    m.set_cache_config(0x10443u);
    m.set_cpu_context(kStatusERL,0);
    m.write32(0x00020000u,9);
    require(m.read32(0xA0020000u)==9,"ERL kuseg bypasses cache");
    op(m,0x12,0,{0x5028u,0}); // locked, valid, clean
    op(m,0x13,0,{0x12345678u,0});
    m.write32(0x80005000u,0xABCDu);
    require((op(m,0x10,0).value&0x78u)==0x28u,"locked stores do not set dirty");
    m.write32(0x80006000u,1);m.write32(0x80007000u,2);
    require(m.read32(0x80005000u)==0xABCDu,"locked way survives other-way replacement");
    rejects([&]{op(m,0x12,1,{0x8028u,0});},"locking both");
    rejects([&]{op(m,0x12,0,{0x5028u,0});},"relock");
    rejects([&]{op(m,0x12,1,{0,0x20u});},"unknown tag");
    reset_native_ee_fixture(m,m20::build_firmware().rom);
    require(!m.data_cache_enabled(),"reset removes opt-in cache experiment");
    rejects([&]{op(m,0x10,0);},"explicit functional cache");
    m.diagnostic_data_cache(true);m.set_cache_config(0x10443u);
    m.write32(0x80004000u,0x12345678u);
    op(m,0x18,0xA0004000u);
    require(m.read32(0xA0004000u)==0x12345678u,"explicit uncached-hit hypothesis translates to real dirty physical line");
}
void compiled_fixture_and_shared_memory() {
    Memory m;initialize(m);
    EEState s;s.pc=0xA0010000u;s.set_cp0(Cop0::Config,0x10443u);
    std::uint64_t executed=0;ExecutionContext ctx{s,m,executed,100,0xFFFFFFFFu};
    m.write32(s.pc,0xBC100000u); // DXLTG index 0
    m.write32(s.pc+4,0x4002E000u); // MFC0 v0,TagLo
    compiled_fixture_step(ctx);compiled_fixture_step(ctx);
    require(s.unknown_gpr_bits[2]==0xFFFFFFFFFFFFFF87ull,"unknown PFN sign propagates through MFC0");
    op(m,0x12,0,{0,0});
    m.write32(0x80008000u,0x11223344u);
    s.pc=0xA0010000u;compiled_fixture_step(ctx);compiled_fixture_step(ctx);
    require(s.gpr[2].low==0x8070u && s.unknown_gpr_bits[2]==0xF87u,"MFC0 exposes actual cache tag and undefined reserved bits");
    // The AOT's shared load/store semantics access this same cache instance.
    s.set_reg32(8,0x80008000u);s.set_reg32(9,0x12345678u);
    ee::exec<MipsOpcode::Sw>(s,m,s.pc,0xAD090000u);
    ee::exec<MipsOpcode::Lw>(s,m,s.pc,0x8D0A0000u);
    require(s.reg32(10)==0x12345678u && m.read32(0xA0008000u)==0,"shared AOT semantics see dirty cached data");
    op(m,0x14,0);require(m.read32(0xA0008000u)==0x12345678u,"guest cache maintenance publishes AOT stores");
}
void gif_dma_visibility() {
    Memory m;m.load_direct_elf({});m.diagnostic_data_cache();m.set_cache_config(0x10443u);
    std::uint64_t cycles=0;m.set_clock(&cycles);
    m.write128(0x80002000u,1ull|(1ull<<15)|(1ull<<60),0xE); // packed A+D, EOP
    m.write128(0x80002010u,0xFFFFFFFF12345678ull,0x62); // GS LABEL
    const auto submit=[&] {
        m.write32(0xB000E000u,1);m.write32(0xB000A010u,0x2000);
        m.write32(0xB000A020u,2);m.write32(0xB000A000u,0x101);
        cycles+=4;m.sync_iop();
    };
    submit();require(m.gif().packets().empty(),"GIF DMA cannot see dirty CPU packet");
    op(m,0x18,0x80002000u);
    submit();require(m.gif().packets().size()==1,"GIF DMA sees real packet after cache writeback");
    require(m.read32(0xB2001084u)==0x12345678u,"GS LABEL came from flushed DMA source bytes");
}
}
void cached_provenance() {
    Memory memory;initialize(memory);const ObservedWord word{0x45000000u,0x00FFFFFFu};
    memory.store32_observed(0x80000300u,word);
    const auto origin=memory.allocate_fpu_origin();
    const FpuAffine relation{origin,0,0,false,word.value,word.unknown,0x457FFFFFu,0x45800000u,
        std::make_shared<const std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0x457FFFFFu,0x45800000u})};
    memory.remember_fpu_relation(0x80000300u,word,relation);
    require(memory.fpu_relation(0x80000300u,memory.load32_observed(0x80000300u)).origin==origin,
        "CPU cache reads retain the certificate of the actual dirty cached word");
    memory.store32_observed(0xA0000300u,word);
    require(memory.fpu_relation(0x80000300u,memory.load32_observed(0x80000300u)).origin==origin,
        "an uncached write does not replace the stale resident cache word or its origin");
    (void)op(memory,0x1A,0x80000300u);
    const auto refilled=memory.load32_observed(0x80000300u);
    require(refilled.value==word.value && refilled.unknown==word.unknown && !memory.fpu_relation(0x80000300u,refilled).origin,
        "invalidate/refill cannot resurrect an older cached certificate from matching bytes and masks");
}
void provenance_visibility_and_ways() {
    Memory memory;initialize(memory);const ObservedWord word{0x3F800000u,1};
    const auto remember=[&](std::uint32_t address) {
        memory.store32_observed(address,word);const auto origin=memory.allocate_fpu_origin();
        memory.remember_fpu_relation(address,word,{origin,0,127,false,word.value,word.unknown,word.value,word.value|1u,{}});
        return origin;
    };
    const auto certificate=[&](std::uint32_t address){return memory.fpu_relation(address,memory.load32_observed(address)).origin;};
    const auto first=remember(0x80000300u),second=remember(0x80001300u);
    op(memory,0x1A,0x80002300u);
    require(certificate(0x80000300u)==first && certificate(0x80001300u)==second,
        "a cache hit-operation miss leaves both resident origins intact");
    op(memory,0x16,0x300u);
    require(certificate(0x80001300u)==second,"index invalidation preserves the other cache way's origin");
    op(memory,0x18,0x80001300u);
    require(certificate(0xA0001300u)==second && certificate(0x80001300u)==second,
        "writeback publishes the dirty origin to RAM and refill retains that exact identity");
    op(memory,0x1A,0x80001300u);memory.store32_observed(0xA0001300u,word);
    require(!certificate(0x80001300u),"an independent matching-byte RAM write cannot resurrect a discarded origin");
    const auto resident=remember(0x80001300u);
    (void)remember(0x80002300u);(void)remember(0x80003300u);
    require(certificate(0x80001300u)==resident,"replacement publishes or preserves both ways without broad certificate invalidation");
}
void native_byte_lanes() {
    // Independent byte oracle for RAM, cache-line boundaries and scratchpad.
    for (const auto base : {0xA0003038u,0x80003038u,0x70000038u}) {
        Memory memory;initialize(memory);
        if (base==0x70000038u) memory.tlb_write(0,{0,0x70000000u,0x80000000u,0});
        std::array<std::uint8_t,16> bytes{},masks{};
        memory.write64(base,0);memory.write64(base+8,0);
        struct Write { unsigned offset,width;std::uint64_t value,unknown; };
        constexpr std::array writes{
            Write{0,8,0xF1E2D3C4B5A69788ull,0x8001020408102040ull},
            Write{8,8,0xFFEEDDCCBBAA9988ull,0x1020408001020408ull},
            Write{2,2,0x1234,0x8001},Write{7,1,0xEF,0x10},
            Write{4,4,0x87654321,0},Write{8,4,0x12345678,0xFF00000Fu},
            Write{0,1,0x55,0},Write{14,2,0xCAFE,0x0F00},
            Write{8,8,0x123456789ABCDEF0ull,0}
        };
        for (const auto& write : writes) {
            const auto address=base+write.offset;
            if (write.width==8) memory.store64_observed(address,{write.value,write.unknown});
            else if (write.width==4) memory.store32_observed(address,{static_cast<std::uint32_t>(write.value),static_cast<std::uint32_t>(write.unknown)});
            else if (write.width==2) memory.store16_observed(address,{static_cast<std::uint32_t>(write.value),static_cast<std::uint32_t>(write.unknown)});
            else memory.store8_observed(address,{static_cast<std::uint32_t>(write.value),static_cast<std::uint32_t>(write.unknown)});
            for (unsigned i=0;i<write.width;++i) {
                masks[write.offset+i]=static_cast<std::uint8_t>(write.unknown>>(i*8u));
                bytes[write.offset+i]=static_cast<std::uint8_t>((write.value & ~write.unknown)>>(i*8u));
            }
            for (unsigned offset=0;offset<16;offset+=8) {
                const auto observed=memory.load64_observed(base+offset);
                for (unsigned i=0;i<8;++i)
                    require(((observed.value>>(i*8u))&255u)==bytes[offset+i] &&
                            ((observed.unknown>>(i*8u))&255u)==masks[offset+i],
                            "native accesses preserve byte lanes and independent unknown masks");
            }
        }
    }
}
int main() try {
    visibility_and_tags();replacement_and_knowledge();mapping_and_locks();compiled_fixture_and_shared_memory();gif_dma_visibility();cached_provenance();provenance_visibility_and_ways();native_byte_lanes();
    std::cout<<"EE D-cache: visibility, tags, LRF, knowledge, mapping, locks and shared CPU semantics passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
