#include "fixture.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include "emulator/services/iop_aot_registry.h"
#include "emulator/services/iop_module_loader.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace ps2x::iop::detail;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool equal(const IopCpuState& a, const IopCpuState& b) {
    return a.gpr == b.gpr && a.hi == b.hi && a.lo == b.lo && a.pc == b.pc && a.cop0 == b.cop0 &&
        a.pendingLoadReg == b.pendingLoadReg && a.pendingLoadValue == b.pendingLoadValue &&
        a.pendingLoad == b.pendingLoad && a.branchPending == b.branchPending && a.branchTarget == b.branchTarget &&
        a.stopped == b.stopped && a.yielded == b.yielded && a.exception == b.exception;
}
}
int main() {
    try {
        registerFixture();
        IopMemory reference, native;
        const auto refModule = IopModuleLoader::load(kImage, reference, 0x10000);
        const auto nativeModule = IopModuleLoader::load(kImage, native, 0x10000);
        require(refModule && nativeModule && refModule.base == nativeModule.base && nativeModule.relocationsComplete,
                "Relocatable fixture did not load");
        IopAotRegistry registry;
        require(registry.bind(kImage, nativeModule.base, nativeModule.size, true, native) == kInstructionCount,
                "Generated module did not bind all executable instructions");
        IopCpuCore core(reference);
        unsigned compared = 0;
        for (unsigned index = 0; index < kComparableInstructionCount; ++index) {
            for (unsigned seed = 0; seed < 32; ++seed) {
                IopCpuState cpu;
                for (unsigned r = 1; r < 32; ++r) cpu.gpr[r] = 0x9e3779b9u * (seed + r);
                cpu.gpr[1] = 0x18000 + (seed & 3);
                if (seed < 4) {
                    cpu.gpr[2] = std::array<uint32_t,4>{0, 1, 0xffffffff, 0x80000000}[seed];
                    if ((reference.read32(nativeModule.base + index * 4) >> 26) < 0x20)
                        cpu.gpr[1] = std::array<uint32_t,4>{0x80000000, 0x7fffffff, 0x80000000, 1}[seed];
                }
                cpu.pc = nativeModule.base + index * 4;
                cpu.hi = 0xabcdef01;
                cpu.lo = 0x12345678;
                cpu.cop0[12] = seed * 0x11111111;
                cpu.cop0[13] = 0x8000ff00;
                cpu.pendingLoad = seed & 1;
                cpu.pendingLoadReg = 2;
                cpu.pendingLoadValue = 0x11223344;
                cpu.branchPending = seed & 2;
                cpu.branchTarget = 0x11000;
                auto actual = cpu;
                for (unsigned off = 0x17ff0; off < 0x18010; off += 4) {
                    reference.write32(off, 0xfedcba98u ^ off);
                    native.write32(off, 0xfedcba98u ^ off);
                }
                const bool running = core.executeInstruction(cpu);
                const auto emitted = registry.execute(actual, native);
                if (!emitted || *emitted != running || !equal(cpu, actual)) {
                    std::cerr << "Mismatch index=" << index << " seed=" << seed << " word=0x" << std::hex
                              << reference.read32(nativeModule.base + index * 4) << '\n';
                    throw std::runtime_error("Emitted instruction diverged from IOP core");
                }
                for (unsigned off = 0x17ff0; off < 0x18010; off += 4)
                    require(reference.read32(off) == native.read32(off), "Emitted store changed RAM differently");
                ++compared;
            }
        }
        for (unsigned index = kComparableInstructionCount; index < kInstructionCount; ++index) {
            IopCpuState unsupported;
            unsupported.pc = nativeModule.base + index * 4;
            unsupported.branchPending = true;
            const auto untouched = unsupported;
            bool rejected = false;
            try { (void)registry.execute(unsupported, native); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected && equal(unsupported, untouched), "Mutable decoding fields executed guessed code");
        }
        // Hardware-contract checks independent of the differential reference.
        IopCpuState cpu;
        cpu.pc = 0x10000; cpu.gpr[1] = 0x12340; cpu.gpr[31] = 0xabcdef;
        reference.write32(cpu.pc, (1u << 21) | 9u); // jalr zero,r1
        require(core.executeInstruction(cpu) && cpu.gpr[31] == 0xabcdef && cpu.branchTarget == 0x12340,
                "JALR zero overwrote the return register");
        cpu = {}; cpu.pc = 0x10000; cpu.gpr[1] = 1; cpu.gpr[2] = 2;
        reference.write32(0x10000, 0x10220001); // beq r1,r2: not taken
        reference.write32(0x10004, 0x0000000c); // syscall in delay slot
        require(core.executeInstruction(cpu) && core.executeInstruction(cpu) &&
                cpu.cop0[14] == 0x10000 && (cpu.cop0[13] & 0x80000000), "Untaken branch lost BD/EPC");
        cpu = {}; cpu.pc = 0x10000; cpu.gpr[1] = 0x18000;
        reference.write32(0x10000, 0x88220004); // lwl r2,4(r1)
        reference.write32(0x10004, 0x98220001); // lwr r2,1(r1)
        reference.write32(0x10008, 0);
        reference.write32(0x18000, 0x44332211); reference.write32(0x18004, 0x88776655);
        require(core.executeInstruction(cpu) && core.executeInstruction(cpu) && core.executeInstruction(cpu) &&
                cpu.gpr[2] == 0x55443322, "Paired unaligned loads lost forwarded bytes");
        std::cout << "PASS " << compared << " emitted IOP state/RAM comparisons and relocation/exception/load-delay contracts\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
