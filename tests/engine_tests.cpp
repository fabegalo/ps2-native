#include "ps2native/engine/console.hpp"
#include "ps2native/elf.hpp"
#include <cstdio>
#include <functional>
#include <stdexcept>
using ps2native::engine::Console;
static void require(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
static void rejects(const std::function<void()>& action) {
    bool failed = false;
    try { action(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, "Expected explicit rejection");
}
static std::int64_t syscall(Console& s, std::uint32_t n, std::uint32_t a=0, std::uint32_t b=0) {
    s.ee.r[3].w[0] = n; s.ee.r[4].w[0] = a; s.ee.r[5].w[0] = b;
    ps2::syscall(s.ee, 0x1000);
    return s.ee.r[2].sd[0];
}
static void unit(ps2::Cpu& c, std::uint32_t, std::uint32_t) { c.r[2].w[0] = 123; c.pc = c.r[31].w[0]; }
int main() {
    try {
        Console s;
        rejects([] { Console second; });
        ps2::st32(0x1000, 0x12345678);
        require(ps2::ld8(0x1000) == 0x78, "Little endian memory");
        require(ps2::ld32(0x80001000) == 0x12345678, "KSEG0 uses the same RAM");
        ps2::st32(0xa0001000, 0xabcdef01);
        require(ps2::ld32(0x1000) == 0xabcdef01, "KSEG1 shares writes");
        rejects([] { ps2::ld64(ps2::kRamSize-4); });
        rejects([] { ps2::st64(ps2::kRamSize-4, 1); });
        rejects([] { ps2::Reg128 r{}; ps2::ld128(ps2::kScratchpadBase+ps2::kScratchpadSize-8,r); });
        ps2::st32(ps2::kScratchpadBase+ps2::kScratchpadSize-4, 42);
        require(ps2::ld32(ps2::kScratchpadBase+ps2::kScratchpadSize-4) == 42, "Scratchpad final word");
        rejects([] { ps2::slow_ld32(0x10002000); });
        rejects([] { ps2::ld32(0x40001000); });
        s.ee.budget = 100000;
        ps2::slow_st32(0x10000800,0xfffe);
        ps2::slow_st32(0x10000810,0x280);
        s.ee.budget -= 2;
        require(ps2::slow_ld32(0x10000800) == 0xffff, "Bus-clock timer advances at EE/2");
        s.ee.budget -= 2;
        rejects([&] { s.account(); });
        s.timers[1] = {}; s.ticks=0; s.ee.budget=100000;
        ps2::slow_st32(0x1000f010, 4); ps2::slow_st32(0x1000f010, 4);
        require(ps2::slow_ld32(0x1000f010) == 0, "INTC mask uses toggle writes");
        ps2::slow_st32(0x1000e010, 0x20000); ps2::slow_st32(0x1000e010, 0x20000);
        require(ps2::slow_ld32(0x1000e010) == 0, "DMAC mask uses toggle writes");
        rejects([] { ps2::slow_st32(0x1000c000, 0x100); });
        static const std::uint16_t entries[] = {1};
        static const ps2::UnitInfo units[] = {{0x1000,4,unit,entries}};
        ps2::register_units(units,1);
        s.ee.pc=0x1000; s.ee.r[31].w[0]=0x1004;
        ps2::dispatch(s.ee,0,0);
        require(s.ee.r[2].w[0] == 123 && s.ee.pc == 0x1004, "AOT dispatch executes native unit");
        rejects([&] { ps2::dispatch(s.ee,0,0); });
        rejects([&] { syscall(s,0xff); });
        s.code_words[0x1000]=0xabcdef01;
        syscall(s,0x64);
        ps2::st32(0x1000,0);
        rejects([&] { syscall(s,0x64); });
        ps2::st32(0x2000,0); ps2::st32(0x2004,1);
        const auto sema = syscall(s,0x40,0x2000);
        require(sema == 1, "Create semaphore");
        require(syscall(s,0x45,1) < 0, "Empty semaphore poll must fail");
        syscall(s,0x42,1);
        require(syscall(s,0x45,1) == 1, "Signal/poll consumes token");
        require(syscall(s,0x45,1) < 0, "Token consumed exactly once");
        s.threads.emplace(1,Console::Thread{.state=Console::ThreadState::Running,.priority=64});
        s.threads.emplace(2,Console::Thread{.state=Console::ThreadState::Ready,.priority=32});
        s.threads.at(2).context.pc=0x4000;
        s.ee.pc=0x3000;
        syscall(s,0x44,1);
        require(s.threads.at(1).state == Console::ThreadState::Waiting, "Wait blocks without a token");
        s.schedule();
        require(s.current == 2 && s.ee.pc == 0x4000, "Highest priority ready context selected");
        syscall(s,0x42,1);
        require(s.threads.at(1).state == Console::ThreadState::Ready && s.threads.at(1).context.pc == 0x3000, "Signal resumes waiter at saved PC");
        // PS2SDK ee_thread_t layout: status, entry, stack, size, gp, priority.
        ps2::st32(0x5004,0x1000); ps2::st32(0x5008,0x10000); ps2::st32(0x500c,0x8000);
        ps2::st32(0x5010,0x1234); ps2::st32(0x5014,48);
        const auto thread = syscall(s,0x20,0x5000);
        require(thread > 0 && s.threads.at(thread).entry == 0x1000, "CreateThread consumes PS2SDK ABI");
        syscall(s,0x22,thread,99);
        require(s.threads.at(thread).context.r[4].w[0] == 99 && s.threads.at(thread).context.r[29].w[0] == 0x18000 - 0x2a0, "StartThread argument and context reserve");
        const std::uint8_t bad[] = {0,1,2};
        rejects([&] { (void)ps2native::parse_ee_elf(bad); });
        std::puts("engine: memory, bounds, AOT, invalidation, semaphores and scheduling PASS");
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n", e.what()); return 1; }
}
