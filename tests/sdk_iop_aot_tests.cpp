#include "iop_compat_test_support.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include "emulator/services/iop_aot_registry.h"

using namespace iop_test;
using namespace ps2x::iop::detail;

namespace {
unsigned nativeCalls = 0;
std::vector<uint32_t> bases;
// Compiled three-instruction IRX fixture: addiu v0,zero,0; jr ra; nop.
bool returnZero(IopCpuState& cpu, IopMemory&, uint32_t base) {
    require(!cpu.pendingLoad && !cpu.branchPending, "Unexpected initial pipeline state");
    ++nativeCalls;
    bases.push_back(base);
    cpu.gpr[2] = 0;
    cpu.pc += 4;
    return true;
}
bool jumpRa(IopCpuState& cpu, IopMemory&, uint32_t) {
    ++nativeCalls;
    cpu.branchPending = true;
    cpu.branchTarget = cpu.gpr[31];
    cpu.pc += 4;
    return true;
}
bool delayNop(IopCpuState& cpu, IopMemory&, uint32_t) {
    ++nativeCalls;
    require(cpu.branchPending, "Lost native branch delay slot");
    cpu.pc = cpu.branchTarget;
    cpu.branchPending = false;
    return true;
}
template<class F> void rejects(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
}

int main() {
    try {
        Irx image(0);
        image.bytes[16] = 1; // ET_REL: LOADCORE chooses the runtime load address.
        image.words(0, {0x24020000u, 0x03E00008u, 0u});
        registerIopAotModule({image.bytes, {{0, returnZero, 0x24020000u}, {4, jumpRa, 0x03E00008u}, {8, delayNop, 0u}}});
        rejects([&] { registerIopAotModule({image.bytes, {{0, returnZero}}}); }, "Duplicate identity accepted");
        rejects([&] { registerIopAotModule({{1}, {{0, returnZero}, {0, jumpRa}}}); }, "Duplicate offset accepted");

        // Actual module loader, two relocations of identical content, one preparation.
        Host host;
        IopSubsystem iop(host);
        image.install(host);
        const auto first = iop.loadModuleBuffer(0x1000);
        const auto second = iop.loadModuleBuffer(0x1000);
        require(first.moduleId > 0 && second.moduleId > 0 && first.startResult == 0 && second.startResult == 0,
                "Native IRX entry did not return through the existing executor");
        require(nativeCalls == 6 && bases.size() == 2 && bases[0] != bases[1], "Same prepared module not reused after relocation");
        require(iop.stopModule(first.moduleId), "Native module unload failed");
        iop.reset();
        image.install(host);
        require(iop.loadModuleBuffer(0x1000).startResult == 0 && nativeCalls == 9,
                "Reset did not release and rebind native module addresses");
        Host otherHost;
        IopSubsystem other(otherHost);
        image.install(otherHost);
        require(other.loadModuleBuffer(0x1000).startResult == 0 && nativeCalls == 12,
                "Second console could not share immutable prepared content");

        // Corruption and incomplete preparation must never run stale native code.
        IopMemory memory;
        IopAotRegistry registry;
        const uint32_t code[] = {0x24020000u, 0x03E00008u, 0u};
        require(memory.writeRam(0x10000, code, sizeof(code)), "Cannot install native code fixture");
        auto different = image.bytes;
        different.back() ^= 1;
        require(registry.bind(different, 0x10000, 0x100, true, memory) == 0, "Different content bound to stale translation");
        rejects([&] { registry.bind(image.bytes, 0x10000, 0x100, false, memory); }, "Incomplete relocation accepted");
        rejects([&] { registry.bind(image.bytes, 0x10000, 4, true, memory); }, "Entry outside loaded image accepted");
        memory.write32(0x10004, 0u);
        rejects([&] { registry.bind(image.bytes, 0x10000, 0x100, true, memory); }, "Wrong instruction accepted before binding");
        memory.write32(0x10004, code[1]);
        require(registry.bind(image.bytes, 0x10000, 0x100, true, memory) == 3, "Failed bind left partial entries");
        rejects([&] { registry.bind(image.bytes, 0x10000, 0x100, true, memory); }, "Overlapping bindings accepted");
        IopCpuState cpu;
        cpu.pc = 0x80010000;
        require(registry.execute(cpu, memory).value_or(false), "Cached/uncached address alias missed native code");
        memory.write32(0x10004, 0u);
        rejects([&] { registry.execute(cpu, memory); }, "Modified code executed stale native translation");
        registry.eraseRange(0x10000, 0x100);
        require(!registry.execute(cpu, memory).has_value(), "Unload left stale native entry");
        memory.write32(0x10004, code[1]);
        require(registry.bind(image.bytes, 0x10000, 0x100, true, memory) == 3, "Reload failed");
        registry.clear();
        require(!registry.execute(cpu, memory).has_value(), "Reset left stale native entry");
        require(memory.writeRam(0x1ffc, code, sizeof(code)) && memory.writeRam(0x2010, code, sizeof(code)),
                "Cannot install cross-page module fixtures");
        require(registry.bind(image.bytes, 0x1ffc, 0xcu, true, memory) == 3 &&
                registry.bind(image.bytes, 0x2010, 0xcu, true, memory) == 3, "Cross-page bindings failed");
        registry.eraseRange(0x1ffc, 0xcu);
        cpu = {}; cpu.pc = 0x2010;
        require(registry.execute(cpu, memory).value_or(false), "Unload removed a neighboring module in the same page");
        cpu = {}; cpu.pc = 0x2000;
        require(!registry.execute(cpu, memory).has_value(), "Cross-page unload left a stale entry");
        std::cout << "PASS IOP AOT content identity, relocation, loader, consoles, reset, unload and invalidation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
