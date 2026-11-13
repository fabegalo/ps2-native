#include "branch/fixture.h"
#include "likely/fixture.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "runtime/ee_scheduler.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void execute(const EeAotModule& module, PS2Runtime& runtime) {
    auto& context = runtime.cpu();
    for (unsigned step = 0; step < 8 && context.pc != 0x1000; ++step) {
        require(runtime.hasFunction(context.pc), "Generated module omitted a reachable branch target");
        runtime.lookupFunction(context.pc)(runtime.memory().getRDRAM(), &context, &runtime);
    }
    require(context.pc == 0x1000, "Generated module did not return");
}

template<class F> static void rejects(F action, const char* message) {
    bool failed = false;
    try { action(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, message);
}

static uint32_t word(const uint8_t* data) {
    uint32_t result;
    std::memcpy(&result, data, 4);
    return result;
}

static struct {
    bool unwound = false, reached = false, oldTail = false;
    uint32_t count = 0, iopAddress = 0;
    uint64_t cycle = 0, vsync = 0, deadline = 0;
    int thread = 1, mode = 0;
} execState;

static void exec_target(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    auto& scheduler = runtime->eeScheduler();
    require(execState.unwound && !execState.oldTail, "ExecPS2 retained the previous native stack");
    require(scheduler.currentThreadId() == execState.thread && scheduler.mainThreadId() == execState.thread,
            "ExecPS2 changed the retained thread identity");
    require(getRegU32(context, 28) == 0x55667788 && getRegU32(context, 4) == 2 &&
            getRegU32(context, 5) == 0x4000 && context->fcr31 == 0x01000001,
            "ExecPS2 lost GP, entry arguments or FPU initialization");
    require(scheduler.readCop0Count() >= execState.count && scheduler.currentVSyncTick() == execState.vsync,
            "ExecPS2 restarted the EE clock or physical VBlank sequence");
    scheduler.publishSnapshot();
    const auto snapshot = scheduler.snapshot();
    require(snapshot.eeCycle >= execState.cycle && snapshot.nextEventCycle == execState.deadline,
            "ExecPS2 changed the physical event phase");
    require(snapshot.threads.size() == 1 && snapshot.semaphores.empty() && snapshot.eventFlags.empty(),
            "ExecPS2 retained obsolete EE kernel objects");
    uint8_t iopByte = 0;
    require(runtime->readIopMemory(execState.iopAddress, &iopByte, 1) && iopByte == 0x6a,
            "ExecPS2 reset the shared IOP memory");
    require(runtime->memory().getGSVRAM()[0x100] == 0x7b, "ExecPS2 cleared GS VRAM");
    R5900Context query;
    for (uint32_t reg : {0x80000001u, 0x80000002u, 0x80000000u}) {
        SET_GPR_U32(&query, 4, reg);
        ps2_stubs::sceSifGetReg(ram, &query, runtime);
        require(getRegU32(&query, 2) == (reg == 0x80000000u ? execState.iopAddress : 0u),
                "ExecPS2 retained stale EE SIF state or discarded the IOP endpoint");
    }
    SET_GPR_U32(context, 4, 0x55667788);
    SET_GPR_U32(context, 5, 0xffffffff);
    SET_GPR_U32(context, 6, 0x1000);
    SET_GPR_U32(context, 7, 0x5000);
    ps2_syscalls::SetupThread(ram, context, runtime);
    require(word(ram + 0x5000) == 2 && std::strcmp(reinterpret_cast<char*>(ram + 0x5044), "rom0:OSDSYS") == 0 &&
            std::strcmp(reinterpret_cast<char*>(ram + 0x5050), "second argument") == 0,
            "ExecPS2 did not carry argv through the new CRT");
    ps2_syscalls::InitThread(ram, context, runtime);
    require(getRegU32(context, 2) == static_cast<uint32_t>(execState.thread), "InitThread used the previous main ID");
    execState.reached = true;
    runtime->requestStop();
}

static void exec_source(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    struct Unwind { ~Unwind() { execState.unwound = true; } } unwind;
    runtime->eeScheduler().accountCycles(12345);
    runtime->eeScheduler().publishSnapshot();
    const auto snapshot = runtime->eeScheduler().snapshot();
    execState.count = runtime->eeScheduler().readCop0Count();
    execState.cycle = snapshot.eeCycle;
    execState.deadline = snapshot.nextEventCycle;
    execState.vsync = runtime->eeScheduler().currentVSyncTick();
    SET_GPR_U32(context, 4, branch_module().linkedBase);
    SET_GPR_U32(context, 5, 0x55667788);
    SET_GPR_U32(context, 6, execState.mode == 1 ? 17 : 2);
    SET_GPR_U32(context, 7, 0x4000);
    ps2_syscalls::dispatchNumericSyscall(7, ram, context, runtime);
    execState.oldTail = true;
    runtime->requestStop();
}

static void exec_test(int thread, int mode = 0) {
    execState = {};
    execState.thread = thread; execState.mode = mode;
    auto runtime = std::make_unique<PS2Runtime>();
    require(runtime->memory().initialize(), "Exec fixture RAM initialization");
    auto* ram = runtime->memory().getRDRAM();
    std::memcpy(ram + branch_module().linkedBase, branch_module().image, branch_module().imageSize);
    const EeAotBinding entry{0, 1, exec_target};
    EeAotModule module = branch_module();
    module.bindings = &entry; module.bindingCount = 1;
    auto& scheduler = runtime->eeScheduler();
    scheduler.registerAotModule(module);
    scheduler.setFunctionOverride(0x1200, exec_source);
    runtime->cpu().pc = thread == 1 ? 0x1200 : 0;
    scheduler.reset(ram, runtime->cpu());
    require(scheduler.createSemaphore(0, 1, 0, 0) > 0 && scheduler.createEventFlag(0, 0, 0) > 0,
            "Exec fixture kernel object creation");
    if (thread != 1) {
        EeThreadCreateParams params;
        params.entry = 0x1200; params.priority = 1; params.stack = 0x180000; params.stackSize = 0x1000;
        const auto id = scheduler.createThread(params);
        require(id == thread && scheduler.startThread(id, 0, runtime->cpu(), false) == 0,
                "Exec fixture secondary thread creation");
    }
    const uint32_t pointers[] = {0x4100, 0x4200};
    std::memcpy(ram + 0x4000, pointers, sizeof(pointers));
    std::memcpy(ram + 0x4100, "rom0:OSDSYS", 12);
    std::memcpy(ram + 0x4200, "second argument", 16);
    execState.iopAddress = runtime->allocateIopMemory(64);
    const uint8_t iopByte = 0x6a;
    require(execState.iopAddress && runtime->writeIopMemory(execState.iopAddress, &iopByte, 1), "Exec fixture IOP allocation");
    for (const auto [reg, value] : {std::pair{0x80000000u, execState.iopAddress},
            std::pair{0x80000001u, 0x123400u}, std::pair{0x80000002u, 1u}}) {
        SET_GPR_U32(&runtime->cpu(), 4, reg);
        SET_GPR_U32(&runtime->cpu(), 5, value);
        ps2_stubs::sceSifSetReg(ram, &runtime->cpu(), runtime.get());
    }
    runtime->memory().getGSVRAM()[0x100] = 0x7b;
    scheduler.accountCycles(9000000);
    if (mode == 2) {
        runtime->memory().writeIORegister(0x1000e000, 0);
        runtime->memory().writeIORegister(0x10008000, 0x100);
    }
    if (mode) {
        rejects([&] { scheduler.run(); }, "Invalid ExecPS2 returned success");
        require(execState.unwound && !execState.reached && !execState.oldTail &&
                !scheduler.snapshot().semaphores.empty(), "Rejected ExecPS2 changed the old kernel");
    } else {
        scheduler.run();
        require(execState.reached, "Prepared ExecPS2 did not enter the new native program");
        require(runtime->allocateIopMemory(64) != execState.iopAddress, "ExecPS2 discarded IOP allocation ownership");
    }
}

static void argument_tests(PS2Runtime& runtime) {
    auto& scheduler = runtime.eeScheduler();
    auto* ram = runtime.memory().getRDRAM();
    constexpr uint32_t address = PS2_RAM_SIZE - 0x144;
    scheduler.writeExecutableArguments(ram, address, "cdrom0:\\ORIGINAL;1");
    require(word(ram + address) == 1 && std::strcmp(reinterpret_cast<char*>(ram + address + 0x44), "cdrom0:\\ORIGINAL;1") == 0,
            "Default game argument changed");
    scheduler.setExecutableArguments({"rom0:OSDSYS", "argument with spaces"});
    auto& context = runtime.cpu();
    SET_GPR_U32(&context, 4, 0);
    SET_GPR_U32(&context, 5, 0xffffffff);
    SET_GPR_U32(&context, 6, 0x1000);
    SET_GPR_U32(&context, 7, address | 0x80000000u);
    ps2_syscalls::SetupThread(ram, &context, &runtime);
    require(word(ram + address) == 2 && word(ram + address + 4) == (address | 0x80000000u) + 0x44,
            "SetupThread lost argc or guest pointer alias");
    require(std::strcmp(reinterpret_cast<char*>(ram + address + 0x44), "rom0:OSDSYS") == 0 &&
            std::strcmp(reinterpret_cast<char*>(ram + address + 0x50), "argument with spaces") == 0,
            "SetupThread replaced the executable arguments");
    std::array<uint8_t, 0x144> before;
    std::memcpy(before.data(), ram + address, before.size());
    rejects([&] { scheduler.writeExecutableArguments(ram, address + 4, ""); }, "Straddling CRT args were accepted");
    require(std::memcmp(before.data(), ram + address, before.size()) == 0, "Rejected args changed RAM");
    rejects([&] { scheduler.writeExecutableArguments(ram, 0x10003000, ""); }, "MMIO CRT args were accepted");
    rejects([&] { scheduler.setExecutableArguments(std::vector<std::string>(17, "")); }, "Too many arguments accepted");
    rejects([&] { scheduler.setExecutableArguments({std::string(255, 'a'), ""}); }, "Oversized payload accepted");
    rejects([&] { scheduler.setExecutableArguments({std::string("a\0b", 3)}); }, "Embedded NUL accepted");
    scheduler.writeExecutableArguments(ram, address | 0x80000000u, "");
    require(std::memcmp(before.data(), ram + address, before.size()) == 0, "Rejected arguments replaced the valid set");
    scheduler.setExecutableArguments({std::string(254, 'a'), ""});
    scheduler.writeExecutableArguments(ram, address, "");
    require(word(ram + address) == 2 && ram[PS2_RAM_SIZE - 1] == 0, "Exact payload limit failed");
    scheduler.setExecutableArguments({});
    scheduler.writeExecutableArguments(ram, address, "ignored");
    require(word(ram + address) == 0 && word(ram + address + 4) == 0, "Empty argv reused the default path");
}

int main() {
    try {
        auto runtime = std::make_unique<PS2Runtime>();
        require(runtime->memory().initialize(), "Fixture RAM initialization");
        auto* ram = runtime->memory().getRDRAM();
        auto& context = runtime->cpu();
        auto& scheduler = runtime->eeScheduler();
        scheduler.registerAotModule(branch_module());
        scheduler.registerAotModule(likely_module());
        scheduler.registerAotModule(branch_module());
        const auto reset = [&](const EeAotModule& module) {
            std::memcpy(ram + module.linkedBase, module.image, module.imageSize);
            scheduler.bindAotModule(module, module.linkedBase, ram);
            context = {};
            context.pc = module.linkedBase + module.entryOffset;
            SET_GPR_U32(&context, 31, 0x1000);
            SET_GPR_U32(&context, 2, 5);
        };
        reset(branch_module());
        auto second = std::make_unique<PS2Runtime>();
        require(!second->hasFunction(context.pc), "EE module binding leaked to another console");
        require(scheduler.executableModule(context.pc, ram) == &branch_module(), "Prepared module was not matched to live RAM");
        const uint32_t hole = branch_module().linkedBase + 8;
        struct RestoreDenseBinding {
            uint32_t slot;
            PS2Runtime::RecompiledFunction previous;
            ~RestoreDenseBinding() { g_ps2RecompiledFunctionTable[slot] = previous; }
        } restore{hole / 4, g_ps2RecompiledFunctionTable[hole / 4]};
        g_ps2RecompiledFunctionTable[hole / 4] = runtime->lookupFunction(context.pc);
        require(!runtime->hasFunction(branch_module().linkedBase + 8), "Untranslated data acquired a native binding");
        require(second->hasFunction(hole), "Dense table negative control did not expose the old binding");
        const auto entryFunction = runtime->lookupFunction(context.pc);
        rejects([&] { scheduler.bindAotModule(branch_module(), branch_module().linkedBase + 0x1000, ram); }, "Unrelocatable EE module moved silently");
        ram[branch_module().linkedBase + 8] ^= 1;
        require(!scheduler.executableModule(context.pc, ram), "Changed image matched the native catalog");
        rejects([&] { scheduler.bindAotModule(branch_module(), branch_module().linkedBase, ram); }, "Changed image was bound");
        require(runtime->lookupFunction(context.pc) == entryFunction, "Rejected binding changed the active module");
        reset(branch_module());
        EeAotModule invalid = branch_module();
        EeAotBinding overlapping[] = {{0, 2, entryFunction}, {4, 1, entryFunction}};
        invalid.bindings = overlapping; invalid.bindingCount = 2;
        rejects([&] { scheduler.registerAotModule(invalid); }, "Overlapping native bindings accepted");
        invalid = branch_module(); invalid.imageSize = PS2_RAM_SIZE;
        rejects([&] { scheduler.registerAotModule(invalid); }, "Module beyond physical RAM accepted");
        invalid = branch_module();
        rejects([&] { scheduler.registerAotModule(invalid); }, "Conflicting module identity accepted");
        execute(branch_module(), *runtime);
        require(getRegU32(&context, 2) == 42, "Branch crossed embedded data or lost its target");
        reset(branch_module());
        ram[branch_module().linkedBase] ^= 1;
        bool changed = false;
        try { execute(branch_module(), *runtime); } catch (const std::runtime_error& error) {
            changed = std::string_view(error.what()).find("EE AOT code changed") != std::string_view::npos;
        }
        require(changed && getRegU32(&context, 2) == 5, "Modified entry executed stale native code");
        reset(branch_module());
        ram[branch_module().linkedBase + 4] = 1;
        changed = false;
        try { execute(branch_module(), *runtime); } catch (const std::runtime_error& error) {
            changed = std::string_view(error.what()).find("EE AOT code changed") != std::string_view::npos;
        }
        require(changed, "Modified NOP delay slot executed stale native code");
        reset(likely_module());
        SET_GPR_U32(&context, 4, 1);
        SET_GPR_U32(&context, 5, 2);
        ram[likely_module().linkedBase + 4] ^= 1;
        execute(likely_module(), *runtime);
        require(getRegU32(&context, 2) == 5, "Annulled delay slot was checked or executed");
        reset(likely_module());
        SET_GPR_U32(&context, 4, 1);
        SET_GPR_U32(&context, 5, 1);
        execute(likely_module(), *runtime);
        require(getRegU32(&context, 2) == 42, "Taken likely branch lost its native continuation");
        argument_tests(*runtime);
        require(second->memory().initialize(), "Second fixture RAM initialization");
        second->eeScheduler().writeExecutableArguments(second->memory().getRDRAM(), 0x4000, "default");
        require(word(second->memory().getRDRAM() + 0x4000) == 1, "Executable argv leaked to another console");
        exec_test(1);
        exec_test(2);
        exec_test(1, 1);
        exec_test(1, 2);
        std::cout << "PASS console-owned EE binding, live image validation, native execution and CRT arguments\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
