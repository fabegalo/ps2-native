#include "ps2native/engine/console.hpp"
#include "ps2native/elf.hpp"
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <format>
#include <stdexcept>
#include <algorithm>

namespace ps2native::engine {
namespace { Console* active{}; }
Console& active_console() {
    if (!active) throw std::runtime_error("No active console");
    return *active;
}
Console::Console() : ram(ps2::kRamSize), scratchpad(ps2::kScratchpadSize), iop_ram(2u << 20) {
    if (active) throw std::runtime_error("The AOT ABI supports one active console per process");
    active = this;
    ps2::g_live_tlb_enabled=false;
    ee.budget = 100000;
    ps2::g_live_ram = ram.data();
    ps2::g_live_spr = scratchpad.data();
}
Console::~Console() { ps2::g_live_tlb_enabled=false; active = nullptr; ps2::g_live_ram = ps2::g_live_spr = nullptr; }
void Console::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open ELF");
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    const auto elf = parse_ee_elf(bytes);
    std::fill(ram.begin(), ram.end(), 0);
    for (const auto& s : elf.segments) std::copy_n(bytes.data() + s.file_offset, s.file_size, ram.data() + s.vaddr);
    ee = {};
    threads.clear(); semaphores.clear(); handlers.clear();
    aot.clear(); aot_modules.clear(); code_words.clear(); syscall_handlers.clear(); dma_registers.clear(); sif_registers.clear();
    output.clear(); current=1; order=0; ticks=0; stack_base=0; heap_end=0;
    exited=false; exit_code=0; intc_mask=0; intc_status=0; dmac_status=0;
    for (auto& timer : timers) timer = {};
    for (auto& entry : tlb) entry = {};
    ps2::g_live_tlb_enabled=false;
    initialize_sif();
    ee.pc = elf.entry;
    ee.budget = 100000;
    ee.r[29].w[0] = ps2::kRamSize - 4096;
}
// Adapted from Memory::translate_tlb before b2959c8; one console owns mappings and backing bytes.
std::uint32_t Console::translate(std::uint32_t address, std::size_t size, bool write) const {
    if (address>=0x80000000 && address<0xc0000000) return address&0x1fffffff;
    const TlbEntry* match=nullptr;
    bool covered=false;
    for (const auto& entry : tlb) {
        if (!entry.defined) continue;
        const auto mask=(entry.entry_lo0&0x80000000) ? ~0x3fffu : ~(entry.page_mask|0x1fffu);
        const bool global=(entry.entry_lo0&entry.entry_lo1&1)!=0;
        if ((address^entry.entry_hi)&mask) continue;
        covered=true;
        if (!global && (entry.entry_hi&255)!=(ee.cop0[10]&255)) continue;
        if (match) throw std::runtime_error("TLB multiple match exception is not integrated");
        match=&entry;
    }
    if (match) {
        if (match->entry_lo0&0x80000000) {
            if (size>0x4000-(address&0x3fff)) throw std::runtime_error("TLB scratchpad span crosses its end");
            return ps2::kScratchpadBase+(address&0x3fff);
        }
        const auto half=((match->page_mask|0x1fffu)+1)>>1;
        if (size>half-(address&(half-1))) throw std::runtime_error("TLB access crosses a page boundary");
        const auto lo=(address&half) ? match->entry_lo1 : match->entry_lo0;
        if (!(lo&2)) throw std::runtime_error("TLB invalid exception is not integrated");
        if (write && !(lo&4)) throw std::runtime_error("TLB modified exception is not integrated");
        const auto frame=((lo>>6)&0xfffff)<<12;
        return (frame&~(half-1))|(address&(half-1));
    }
    if (covered || (ps2::g_live_tlb_enabled && address>=ps2::kScratchpadBase && address-ps2::kScratchpadBase<ps2::kScratchpadSize))
        throw std::runtime_error("TLB refill exception is not integrated");
    // The standalone ELF HLE environment provides direct RAM/IO and uncached aliases before firmware initializes TLB.
    if ((address>>28)==2 || (address>>28)==3) return address&0xfffffff;
    if (address<0x80000000) return address;
    throw std::runtime_error("TLB refill exception is not integrated");
}
std::uint64_t Console::now() const { return ticks + static_cast<std::uint64_t>(100000ll - ee.budget); }
void Console::account() {
    const auto before=ticks;
    ticks = now();
    if (std::getenv("PS2N_ENGINE_TRACE") && ticks/1000000 != before/1000000)
        std::fprintf(stderr,"[cost] t%u pc=%08X cost=%llu\n",current,ee.pc,static_cast<unsigned long long>(ticks));
    ee.budget = 100000;
    if (ticks > 100000000) throw std::runtime_error("EE execution limit reached");
    for (const auto& timer : timers)
        if (timer.due <= ticks) throw std::runtime_error("Timer IRQ delivery is not integrated");
}
void Console::schedule() {
    account();
    reschedule_requested=false;
    auto& old = threads.at(current);
    ee.pc &= ~ps2::kUnwind;
    old.context = ee;
    if (old.state == ThreadState::Running) { old.state = ThreadState::Ready; old.order = ++order; }
    auto best = threads.end();
    for (auto it = threads.begin(); it != threads.end(); ++it)
        if (it->second.state == ThreadState::Ready && (best == threads.end() ||
            std::pair(it->second.priority, it->second.order) < std::pair(best->second.priority, best->second.order))) best = it;
    if (best == threads.end()) {
        std::string detail="EE scheduler deadlock:";
        for (const auto& [id,t] : threads) detail += std::format(" t{} state={} pc={:08X} wait={}",id,static_cast<int>(t.state),t.context.pc,t.wait);
        throw std::runtime_error(detail);
    }
    current = best->first;
    best->second.state = ThreadState::Running;
    ee = best->second.context;
}
void Console::run() {
    threads.emplace(1, Thread{.state=ThreadState::Running, .priority=64});
    while (!exited) {
        ee.pc &= ~ps2::kUnwind;
        ps2::dispatch(ee, 0, 0);
        ee.pc &= ~ps2::kUnwind;
        service_sif_irq();
        if (!exited && reschedule_requested) schedule();
        else account();
    }
}
}

namespace ps2 {
// EE kernel context frame, as used by the upstream kernel.cpp and PS2SDK startup ABI.
static constexpr std::uint32_t context_reserve = 0x2a0;
std::uint8_t *g_live_ram{}, *g_live_spr{};
bool g_live_tlb_enabled{};
bool g_watch_on{};
std::uint32_t g_watch_base{}, g_watch_end{};
using ps2native::engine::active_console;
[[noreturn]] static void unsupported(const char* what, std::uint32_t at) {
    throw std::runtime_error(std::format("Unsupported {} at 0x{:08X}", what, at));
}
void watch_hit(std::uint32_t a, std::uint32_t, const char*) { unsupported("write watch", a); }
void watch_span(std::uint32_t a, std::uint32_t, const char*) { unsupported("write watch", a); }
void init_watch() {}
static std::uint8_t* mapped_physical(std::uint32_t physical, std::size_t size) {
    auto& c=active_console();
    if (physical < c.ram.size() && size <= c.ram.size()-physical) return c.ram.data()+physical;
    if (physical>=kScratchpadBase && physical-kScratchpadBase<c.scratchpad.size() && size<=c.scratchpad.size()-(physical-kScratchpadBase))
        return c.scratchpad.data()+physical-kScratchpadBase;
    unsupported("memory/MMIO",physical);
}
static std::uint8_t* mapped(std::uint32_t address, std::size_t size, bool write=false) {
    return mapped_physical(active_console().translate(address,size,write),size);
}
#define LOAD(bits, type) type slow_ld##bits(std::uint32_t a) { return load<type>(mapped(a, sizeof(type))); }
LOAD(8, std::uint8_t)
LOAD(16, std::uint16_t)
static bool dma_register(std::uint32_t a) {
    constexpr std::uint32_t bases[] = {0x10008000,0x10009000,0x1000a000,0x1000b000,0x1000b400,0x1000c000,0x1000c400,0x1000c800,0x1000d000,0x1000d400};
    for (auto base : bases) if (a >= base && a <= base + 0x80 && !(a & 15)) {
        const auto offset = a - base;
        return offset <= 0x50 || offset == 0x80;
    }
    return a >= 0x1000e000 && a <= 0x1000e060 && !(a & 15);
}
static std::uint32_t read32_physical(std::uint32_t a) {
    auto& s = active_console();
    if (a == 0x1000e010) return s.dmac_status;
    if (a == 0x1000f000) return s.intc_status;
    if (a == 0x1000f010) return s.intc_mask;
    if (dma_register(a)) return s.dma_registers[a];
    if (a >= 0x10000000 && a < 0x10002000 && !(a & 15) && (a & 0x7ff) < 0x40) {
        auto& t = active_console().timers[(a >> 11) & 3];
        switch (a & 0x30) {
        case 0:
            if (!(t.mode & 0x80)) return t.count;
            if ((t.mode & 3) == 3 || (t.mode & 0x7c)) unsupported("gated/HBlank timer", a);
            if (t.due <= s.now()) unsupported("timer IRQ delivery", a);
            return static_cast<std::uint32_t>((t.count + (s.now() - t.base) / (2ull << (4 * (t.mode & 3)))) & 0xffff);
        case 0x10: return t.mode;
        case 0x20: return t.compare;
        default: return t.hold;
        }
    }
    return load<std::uint32_t>(mapped_physical(a, 4));
}
std::uint32_t slow_ld32(std::uint32_t a) { return read32_physical(active_console().translate(a,4,false)); }
std::uint64_t slow_ld64(std::uint32_t a) {
    a=active_console().translate(a,8,false);
    if ((a >= 0x10000000 && a < 0x10002000) || dma_register(a) || a == 0x1000f000 || a == 0x1000f010) return read32_physical(a);
    return load<std::uint64_t>(mapped_physical(a,8));
}
#undef LOAD
void slow_ld128(std::uint32_t a, Reg128& v) { std::memcpy(&v, mapped(a, 16), 16); }
#define STORE(bits, type) void slow_st##bits(std::uint32_t a, type v) { store(mapped(a, sizeof(type),true), v); }
STORE(8, std::uint8_t)
STORE(16, std::uint16_t)
// Timer register masks follow exoteric33 hw.cpp (MIT), adapted to console-owned state.
static void write32_physical(std::uint32_t a, std::uint32_t v) {
    auto& s = active_console();
    if (a == 0x1000e010) { s.dmac_status &= ~(v & 0xffff); s.dmac_status ^= v & 0xffff0000; return; }
    if (a == 0x1000f000) { s.intc_status &= ~v; return; }
    if (a == 0x1000f010) { s.intc_mask ^= v & 0x7fff; return; }
    if (dma_register(a)) {
        if (a < 0x1000e000 && !(a & 0xff) && (v & 0x100)) unsupported("DMA transfer", a);
        if ((a & 0xff) == 0x20 && a < 0x1000e000) v &= 0xffff;
        s.dma_registers[a] = v; return;
    }
    if (a >= 0x10000000 && a < 0x10002000 && !(a & 15) && (a & 0x7ff) < 0x40) {
        auto& t = active_console().timers[(a >> 11) & 3];
        const auto count = read32_physical(a & ~0x30u);
        t.count = count;
        switch (a & 0x30) {
        case 0: t.count = v & 0xffff; break;
        case 0x10: t.count = count; t.mode = (t.mode & 0xc00 & ~(v & 0xc00)) | (v & 0x3ff); break;
        case 0x20: t.compare = v & 0xffff; break;
        default: t.hold = v & 0xffff; break;
        }
        t.base = s.now();
        t.due = ~0ull;
        if (t.mode & 0x80) {
            if ((t.mode & 3) == 3 || (t.mode & 0x7c)) unsupported("gated/HBlank timer", a);
            const auto divider = 2ull << (4 * (t.mode & 3));
            if (t.mode & 0x200) t.due = t.base + (0x10000u - t.count) * divider;
            if (t.mode & 0x100) {
                auto delta = (t.compare - t.count) & 0xffff; if (!delta) delta = 0x10000;
                t.due = std::min<std::uint64_t>(t.due, t.base + delta * divider);
            }
        }
        return;
    }
    store(mapped_physical(a, 4), v);
}
void slow_st32(std::uint32_t a, std::uint32_t v) { write32_physical(active_console().translate(a,4,true),v); }
void slow_st64(std::uint32_t a, std::uint64_t v) {
    a=active_console().translate(a,8,true);
    if ((a >= 0x10000000 && a < 0x10002000) || dma_register(a) || a == 0x1000f000 || a == 0x1000f010) { write32_physical(a, static_cast<std::uint32_t>(v)); return; }
    store(mapped_physical(a,8), v);
}
#undef STORE
void slow_st128(std::uint32_t a, const Reg128& v) { std::memcpy(mapped(a,16,true), &v, 16); }
void register_units(const UnitInfo* units, std::uint32_t count) {
    auto& table = active_console().aot;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& u = units[i];
        if (!u.function || !u.entries || !u.size || (u.base & 3) || (u.size & 3) || u.base >= kRamSize || u.size > kRamSize - u.base) throw std::runtime_error("Invalid AOT unit");
        for (std::uint32_t offset = 0; offset < u.size; offset += 4)
            if (u.entries[offset / 4] && !table.emplace(u.base + offset, &u).second) throw std::runtime_error("Duplicate AOT entry");
    }
}
static bool module_matches(const ModuleInfo& m) {
    for (std::uint32_t i=0; i<m.check_count; ++i)
        if (ld32(m.fixed_base+m.checks[i*2]) != m.checks[i*2+1]) return false;
    return true;
}
void register_modules(const ModuleInfo* modules, std::uint32_t count) {
    for (std::uint32_t i=0; i<count; ++i) {
        const auto& m=modules[i];
        const auto physical=m.fixed_base & 0x1fffffff;
        if (!m.fixed_base || (m.fixed_base&3) || m.size<4 || (m.size&3) || m.check_count>m.size/4 || physical >= kRamSize || m.size > kRamSize-physical || !m.check_count || !m.checks || !m.units)
            throw std::runtime_error("Invalid fixed AOT module");
        for (std::uint32_t k=0; k<m.check_count; ++k)
            if ((m.checks[k*2]&3) || m.checks[k*2] > m.size-4) throw std::runtime_error("AOT module check outside image");
        for (std::uint32_t k=0; k<m.count; ++k) {
            const auto& u=m.units[k];
            if (!u.function || !u.entries || !u.size || (u.base&3) || (u.size&3) || u.base>=m.size || u.size>m.size-u.base)
                throw std::runtime_error("AOT module unit outside image");
        }
        active_console().aot_modules.push_back(&m);
    }
}
void add_modules(const ModuleInfo* modules, std::uint32_t count) { register_modules(modules,count); }
bool window_module_in_place(std::uint32_t pc) {
    for (const auto* m : active_console().aot_modules)
        if (pc>=m->fixed_base && pc-m->fixed_base<m->size && module_matches(*m)) return true;
    return false;
}
void dispatch(Cpu& c, std::uint32_t, std::uint32_t depth) {
    if (c.pc == 0xfffffff0u) {
        active_console().threads.at(active_console().current).state = ps2native::engine::Console::ThreadState::Dormant;
        active_console().reschedule_requested=true;
        c.pc |= kUnwind;
        return;
    }
    const auto& table = active_console().aot;
    const auto it = table.find(c.pc);
    if (it == table.end()) {
        for (const auto* module : active_console().aot_modules) {
            if (c.pc < module->fixed_base || c.pc-module->fixed_base >= module->size || !module_matches(*module)) continue;
            const auto offset=c.pc-module->fixed_base;
            for (std::uint32_t i=0; i<module->count; ++i) {
                const auto& u=module->units[i];
                if (offset<u.base || offset-u.base>=u.size || !u.entries[(offset-u.base)/4]) continue;
                for (std::uint32_t k=0; k<module->check_count; ++k)
                    active_console().code_words[module->fixed_base+module->checks[k*2]]=module->checks[k*2+1];
                c.base=module->fixed_base;
                u.function(c,u.entries[(offset-u.base)/4],depth);
                return;
            }
        }
        throw ps2native::engine::MissingAotEntry(c.pc);
    }
    const auto& u = *it->second;
    u.function(c, u.entries[(c.pc - u.base) / 4], depth);
}
void call(Cpu& c, std::uint32_t target, std::uint32_t depth) { c.pc = target; dispatch(c, 0, depth); }
void bad_entry(Cpu& c, std::uint32_t) { unsupported("AOT unit entry", c.pc); }
bool preempt(Cpu& c) {
    active_console().account();
    c.budget = 100000;
    return active_console().sif_irq_ready();
}
void syscall(Cpu& c, std::uint32_t pc) {
    auto& s = active_console();
    const auto a0 = c.r[4].w[0], a1 = c.r[5].w[0], a2 = c.r[6].w[0];
    const auto raw = c.r[3].w[0];
    auto n = (raw & 0x80000000) ? 0u - raw : raw;
    if (n == 0x43) n = 0x42;
    if (n == 0x68) n = 0x64;
    if (std::getenv("PS2N_ENGINE_TRACE"))
        std::fprintf(stderr,"[sys] t%u %02X pc=%08X a=%08X %08X %08X\n",s.current,n,pc,a0,a1,a2);
    if (const auto custom=s.syscall_handlers.find(n); custom != s.syscall_handlers.end() && custom->second) {
        const auto resume=c.pc;
        const auto return_address=c.r[31];
        c.pc=custom->second; c.r[31].w[0]=0xffffffe4;
        try {
            do {
                c.pc &= ~kUnwind;
                dispatch(c,0,0);
                if (s.reschedule_requested) throw std::runtime_error("Blocking custom syscall is not supported");
            } while ((c.pc & ~kUnwind) != 0xffffffe4);
        } catch (...) { c.r[31]=return_address; c.pc=resume; throw; }
        c.r[31]=return_address; c.pc=resume;
        return;
    }
    std::int64_t result{};
    switch (n) {
    case 0x04: s.exited = true; s.exit_code = static_cast<int>(a0); c.pc = 0; break;
    case 0x10: case 0x12: {
        if (a0 >= 15 || !s.aot.contains(a1)) { result = -1; break; }
        std::uint32_t id = 1; while(s.handlers.contains(id)) ++id;
        s.handlers.emplace(id, ps2native::engine::Console::InterruptHandler{a0 | (n == 0x12 ? 0x100u : 0u),a1,c.r[28].w[0]});
        result = id; break;
    }
    case 0x11: case 0x13: {
        const auto it=s.handlers.find(a1);
        const auto cause=a0 | (n == 0x13 ? 0x100u : 0u);
        if (it == s.handlers.end() || it->second.cause != cause) { result=-1; break; }
        s.handlers.erase(it); result=a1; break;
    }
    case 0x14: case 0x15: {
        if (a0 >= 15) { result = -1; break; }
        const auto mask = 1u << a0;
        result = (s.intc_mask & mask) ? 1 : 0;
        if (n == 0x14) s.intc_mask |= mask; else s.intc_mask &= ~mask;
        break;
    }
    case 0x16: case 0x17: {
        if (a0 >= 10) { result = -1; break; }
        const auto mask = 1u << (a0 + 16);
        result = (s.dmac_status & mask) ? 1 : 0;
        if (n == 0x16) s.dmac_status |= mask; else s.dmac_status &= ~mask;
        break;
    }
    case 0x20: {
        const auto entry = ld32(a0 + 4), stack = ld32(a0 + 8), size = ld32(a0 + 12), gp = ld32(a0 + 16), priority = ld32(a0 + 20);
        if (!s.aot.contains(entry) || stack > kRamSize || size > kRamSize - stack || size < context_reserve || priority < 1 || priority > 127) {
            if (std::getenv("PS2N_ENGINE_TRACE")) std::fprintf(stderr,"[thread] rejected entry=%08X aot=%d stack=%08X size=%08X priority=%u\n",entry,s.aot.contains(entry),stack,size,priority);
            result = -1; break;
        }
        std::uint32_t id = 1; while (id < 256 && s.threads.contains(id)) ++id;
        if (id == 256) { result = -1; break; }
        auto& t = s.threads[id];
        t.entry = entry; t.stack = stack; t.size = size; t.gp = gp; t.priority = priority;
        result = id; break;
    }
    case 0x22: {
        const auto it = s.threads.find(a0);
        if (it == s.threads.end() || it->second.state != ps2native::engine::Console::ThreadState::Dormant) { result = -1; break; }
        auto& t = it->second;
        t.context = {};
        t.context.pc = t.entry; t.context.budget = 100000;
        t.context.r[4].w[0] = a1; t.context.r[28].w[0] = t.gp;
        t.context.r[29].w[0] = (t.stack + t.size - context_reserve) & ~15u;
        t.context.r[31].w[0] = 0xfffffff0u;
        t.state = ps2native::engine::Console::ThreadState::Ready; t.order = ++s.order;
        result = a0; s.reschedule_requested=true; c.pc |= kUnwind; break;
    }
    case 0x23: case 0x24:
        s.threads.at(s.current).state = ps2native::engine::Console::ThreadState::Dormant;
        s.reschedule_requested=true; c.pc |= kUnwind; break;
    case 0x2f: result = s.current; break;
    case 0x40: {
        const auto count = ld32(a0 + 8), maximum = ld32(a0 + 4);
        if (std::getenv("PS2N_ENGINE_TRACE")) std::fprintf(stderr,"[sema] %u %u %u %u %u %u\n",count,maximum,ld32(a0+8),ld32(a0+12),ld32(a0+16),ld32(a0+20));
        if (!maximum || count > maximum || maximum > 0x7fffffff) { result = -1; break; }
        std::uint32_t id = 1; while (id < 256 && s.semaphores.contains(id)) ++id;
        if (id == 256) { result = -1; break; }
        s.semaphores.emplace(id, ps2native::engine::Console::Semaphore{static_cast<int>(count), static_cast<int>(maximum), {}});
        result = id; break;
    }
    case 0x41: {
        const auto it=s.semaphores.find(a0);
        if (it == s.semaphores.end()) { result=-1; break; }
        for (auto id : it->second.waiters) {
            auto& t=s.threads.at(id);
            t.wait=0; t.context.r[2].sd[0]=-425;
            t.state=ps2native::engine::Console::ThreadState::Ready; t.order=++s.order;
        }
        s.semaphores.erase(it); result=a0;
        if (!s.sif.in_interrupt) { s.reschedule_requested=true; c.pc |= kUnwind; }
        break;
    }
    case 0x42: case 0x44: case 0x45: {
        const auto it = s.semaphores.find(a0);
        if (it == s.semaphores.end()) { result = -1; break; }
        auto& sema = it->second;
        if (n == 0x42) {
            if (!sema.waiters.empty()) {
                auto& t = s.threads.at(sema.waiters.front());
                sema.waiters.erase(sema.waiters.begin());
                t.context.r[2].sd[0] = a0; t.wait = 0;
                t.state = ps2native::engine::Console::ThreadState::Ready; t.order = ++s.order;
                if (!s.sif.in_interrupt) { s.reschedule_requested=true; c.pc |= kUnwind; }
            } else if (sema.count < sema.maximum) ++sema.count;
            else { result = -420; break; }
        } else if (sema.count) --sema.count;
        else if (n == 0x45) { result = -419; break; }
        else {
            auto& t = s.threads.at(s.current);
            t.state = ps2native::engine::Console::ThreadState::Waiting; t.wait = a0;
            sema.waiters.push_back(s.current); s.reschedule_requested=true; c.pc |= kUnwind;
        }
        result = a0; break;
    }
    case 0x3c: {
        const auto top = a1 == 0xffffffff ? kRamSize - 4096 : a1 + a2;
        if (a2 > top || top > kRamSize || top < context_reserve) throw std::runtime_error("Invalid thread stack");
        s.stack_base = top - a2;
        result = top - context_reserve;
        break;
    }
    case 0x3d: s.heap_end = a1 == 0xffffffff ? s.stack_base : a0 + a1; result = s.heap_end; break;
    case 0x3e: result = s.heap_end; break;
    case 0x4a: s.osd_config=ld32(a0); break;
    case 0x4b: st32(a0,s.osd_config); break;
    case 0x64: // Unified host memory: reject stale translations before completing FlushCache.
        for (const auto& [pc, word] : s.code_words)
            if (ld32(pc) != word) unsupported("modified AOT instruction", pc);
        break;
    case 0x74:
        if (a0 >= 256 || (a1&3)) throw std::runtime_error("Invalid SetSyscall slot/address");
        result=s.syscall_handlers[a0]; s.syscall_handlers[a0]=a1; break;
    case 0x79: s.sif_setreg(a0,a1); break;
    case 0x7a: result=s.sif_getreg(a0); break;
    case 0x77: result=s.sif_dma(a0,a1); break;
    case 0x76: result=(a0 && a0 <= s.sif.completed_id) ? -1 : 0; break;
    case 0x78:
        s.sif.armed=true; s.dma_registers[0x1000c000]=0x184; s.sif_pump(); break;
    case 0x6b:
        s.sif.armed=false;
        for (const auto base : {0x1000c000u,0x1000c400u}) s.dma_registers[base] &= ~0x100u;
        break;
    case 0x7f: result = kRamSize; break;
    default: throw std::runtime_error(std::format("Unsupported syscall 0x{:02X} at 0x{:08X} args {:08X} {:08X} {:08X}", n, pc, a0, a1, a2));
    }
    c.r[2].sd[0] = result;
    if (s.sif_irq_ready()) c.pc |= kUnwind;
}
void stop(Cpu&, std::uint32_t pc, const char* what) { unsupported(what, pc); }
void trap(Cpu&, std::uint32_t pc) { unsupported("trap", pc); }
void breakpoint(Cpu&, std::uint32_t pc) { unsupported("break", pc); }
void probe(Cpu&, std::uint32_t pc) { unsupported("probe", pc); }
void tlbwi(Cpu& c) {
    auto& s=active_console();
    const auto index=c.cop0[0]&63u;
    if (index>=48) unsupported("TLB index",index);
    // Masks and entry layout reused from this repository's pre-migration Memory::tlb_write.
    s.tlb[index]={c.cop0[5]&0x01ffe000u,c.cop0[10]&0xffffe0ffu,c.cop0[2]&0x83ffffffu,c.cop0[3]&0x03ffffffu,true};
    g_live_tlb_enabled=true;
}
void set_interrupts(Cpu& c, bool enabled) { c.cop0[12] = (c.cop0[12] & ~0x10000u) | (enabled ? 0x10000u : 0); }
std::uint32_t mfc0(Cpu& c, std::uint32_t reg, std::uint32_t sel) { if (sel) unsupported("COP0 select", reg); return c.cop0[reg]; }
void mtc0(Cpu& c, std::uint32_t reg, std::uint32_t value) { c.cop0[reg] = value; }
std::uint32_t cfc1(Cpu& c, std::uint32_t reg) { if(reg != 31) unsupported("CFC1", reg); return c.fcr31; }
void ctc1(Cpu& c, std::uint32_t reg, std::uint32_t v) { if(reg != 31) unsupported("CTC1", reg); c.fcr31 = v; }
}
