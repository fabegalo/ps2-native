#include "runtime/ps2_memory.h"
#include "runtime/ee_scheduler.h"
#include "emulator/core/iop_memory.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/imports/iop_usbd.h"
#include "Kernel/Syscalls/Dispatcher.h"
#include "runtime/guest_executable_request.h"
#include "Kernel/Stubs/SIF.h"
#include "ps2_iop_transport.h"

#include <iostream>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <new>
#include <memory>
#include <span>

int device_boundary_test() {
    struct Spu final : ps2x::iop::IopSpu2Backend {
        uint16_t regs[1024]{};
        uint32_t reads = 0, writes = 0, address = 0, count = 0, events = 0;
        uint16_t* source = nullptr;
        void reset(uint8_t*, uint32_t, uint64_t) override {}
        uint16_t read16(uint32_t a, uint64_t) override { ++reads; return regs[(a & 0x7ffu) / 2u]; }
        void write16(uint32_t a, uint16_t v, uint64_t) override { ++writes; regs[(a & 0x7ffu) / 2u] = v; }
        void dma(int core, uint16_t* p, uint32_t n, uint32_t a, bool write, uint64_t) override {
            if (core != 0 || !write) throw std::runtime_error("Wrong DMA routing");
            source = p; count = n; address = a;
        }
        uint32_t madr(int) override { return address; }
        void advance(uint64_t c) override { if (c >= 100 && source) { address += count * 2; source = nullptr; events = 2; } }
        uint64_t nextEvent() override { return source ? 100 : UINT64_MAX; }
        uint32_t takeEvents() override { auto result = events; events = 0; return result; }
    } spu;
    ps2x::iop::detail::IopMemory memory;
    uint64_t clock = 0;
    memory.attachSpu2(&spu, &clock);
    memory.write16(0xbf900180u, 0x1234);
    if (spu.writes != 1 || memory.read16(0x1f900180u) != 0x1234 || spu.reads != 1) return 1;
    memory.write32(0x1f900184u, 0x11223344);
    if (spu.writes != 3 || memory.read32(0x1f900184u) != 0x11223344) return 1;
    memory.write32(0x1f8010c0u, 0x10000);
    memory.write32(0x1f8010c4u, 0x00020004);
    memory.write32(0x1f8010c8u, 0x01000201);
    if (spu.count != 16 || !spu.source || !(memory.read32(0x1f8010c8u) & 0x1000000u)) return 1;
    clock = 99;
    if (memory.advanceSpu2() || !(memory.read32(0x1f8010c8u) & 0x1000000u)) return 1;
    clock = 100;
    if (memory.advanceSpu2() != 2 || (memory.read32(0x1f8010c8u) & 0x1000000u) ||
        memory.read32(0x1f8010c0u) != 0x10020u) return 1;
    return 0;
}

int csr_test() {
    PS2Memory memory;
    if (!memory.initialize()) return 1;
    constexpr uint32_t csr = 0x12001000u;
    const auto empty = [&] {
        return (memory.read64(csr) & 0xC000u) == 0x4000u &&
               (memory.read32(csr) & 0xC000u) == 0x4000u &&
               (memory.readIORegister(csr) & 0xC000u) == 0x4000u;
    };
    if (!empty()) { std::cerr << "Idle GS FIFO must report empty\n"; return 1; }
    for (const auto value : {0u, 0xC000u, 0x200u}) {
        memory.write32(csr, value);
        if (!empty()) { std::cerr << "32-bit CSR write changed read-only FIFO status\n"; return 1; }
        memory.write64(csr, value);
        if (!empty()) { std::cerr << "64-bit CSR write changed read-only FIFO status\n"; return 1; }
        memory.writeIORegister(csr, value);
        if (!empty()) { std::cerr << "IO CSR write changed read-only FIFO status\n"; return 1; }
    }
    memory.gs().csr.fetch_or(3u);
    memory.write32(csr, 1u);
    if ((memory.read64(csr) & 3u) != 2u || !empty()) return 1;
    memory.write64(csr, 2u);
    if ((memory.read64(csr) & 3u) != 0u || !empty()) return 1;
    memory.write32(csr + 4u, 0x12345678u);
    if (memory.read32(csr + 4u) != 0x12345678u || !empty()) return 1;
    return 0;
}

void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

void vif_stream_test() {
    PS2Memory whole, split;
    require(whole.initialize() && split.initialize(), "VIF memory initialization");
    std::vector<uint32_t> words{0x01000402u, 0x30000000u, 11u, 22u, 33u, 44u,
                                0x6C060000u}; // fill cycle: four source vectors, six writes
    for (uint32_t i = 0; i < 16u; ++i) words.push_back(100u + i);
    words.insert(words.end(), {0x4A020008u, 0x12345678u, 0x9ABCDEF0u, 0x11223344u, 0x55667788u,
                               0x20000000u, 0x76543210u, 0x31000000u, 1u, 2u, 3u, 4u});
    const auto *bytes = reinterpret_cast<const uint8_t *>(words.data());
    const uint32_t size = static_cast<uint32_t>(words.size() * 4u);
    whole.processVIF1Data(bytes, size);
    uint32_t first = 0;
    std::memcpy(&first, whole.getVU1Data(), 4u);
    require(first == 100u, "VIF1 UNPACK did not write its payload");
    std::memcpy(&first, whole.getVU1Code() + 64u, 4u);
    require(first == 0x12345678u, "VIF1 MPG did not upload its instructions");
    for (uint32_t i = 0; i < size; ++i) split.processVIF1Data(bytes + i, 1u);
    require(std::memcmp(whole.getVU1Data(), split.getVU1Data(), PS2_VU1_DATA_SIZE) == 0, "Fragmented VIF1 UNPACK differs");
    require(std::memcmp(whole.getVU1Code(), split.getVU1Code(), PS2_VU1_CODE_SIZE) == 0, "Fragmented VIF1 MPG differs");
    whole.processVIF0Data(bytes, size);
    for (uint32_t i = 0; i < size; ++i) split.processVIF0Data(bytes + i, 1u);
    require(std::memcmp(whole.getVU0Data(), split.getVU0Data(), PS2_VU0_DATA_SIZE) == 0, "Fragmented VIF0 UNPACK differs");
    require(std::memcmp(whole.getVU0Code(), split.getVU0Code(), PS2_VU0_CODE_SIZE) == 0, "Fragmented VIF0 MPG differs");
    require(split.initialize(), "Reset fragmented VIF session");
    split.processVIF1Data(bytes, 2u);
    split.writeIORegister(0x10003C10u, 1u);
    const uint32_t mark = 0x07001234u;
    split.processVIF1Data(reinterpret_cast<const uint8_t *>(&mark), 4u);
    require(split.readIORegister(0x10003C30u) == 0x1234u, "VIF reset retained a partial command");
    require(split.readIORegister(0x10003C60u) == 0u, "Completed VIF command retained NUM");
}

void vif_image_continuation_test() {
    for (uint32_t fragment : {1u, 4u, 64u}) {
        PS2Memory memory;
        require(memory.initialize(), "VIF image memory initialization");
        std::vector<uint8_t> received;
        memory.setGifPacketCallback([&](const uint8_t* data, uint32_t bytes) {
            received.insert(received.end(), data, data + bytes);
        });
        const std::array<uint32_t, 12> stream{
            0x50000001u, 0x00008001u, 0x08000000u, 0u, 0u, // DIRECT: IMAGE tag without its data
            0x0700ABCDu, 0x50000001u, // MARK then the next DIRECT; neither is a pixel
            0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u,
            0x07001234u};
        const auto* bytes = reinterpret_cast<const uint8_t*>(stream.data());
        for (uint32_t offset = 0; offset < sizeof(stream); offset += fragment)
            memory.processVIF1Data(bytes + offset, std::min<uint32_t>(fragment, sizeof(stream) - offset));
        require(received.size() == 48u, "VIF image continuation lost its GIF payload");
        require(std::memcmp(received.data() + 32u, stream.data() + 7u, 16u) == 0,
                "VIFcodes were consumed as texture pixels");
        require(memory.readIORegister(0x10003C30u) == 0x1234u, "VIFcode after image continuation was lost");
    }
}

void mfifo_test(uint32_t mode, uint32_t disabled) {
    PS2Memory memory;
    require(memory.initialize(), "MFIFO memory initialization");
    constexpr uint32_t base = 0x20000u, spr = 0x1000D000u;
    const uint32_t drain = mode == 2u ? 0x10009000u : 0x1000A000u;
    std::vector<uint8_t> received;
    memory.setGifPacketCallback([&](const uint8_t *data, uint32_t size) {
        received.insert(received.end(), data, data + size);
    });
    const std::array<uint32_t, 12> stream{
        0x70000002u, 0u, 0u, 0x50000002u, // END with two payload qwords; TTE supplies DIRECT
        0x00008001u, 0x10000000u, 0xEu, 0u, // GIF PACKED, A+D, one loop
        0u, 0u, 0x61u, 0u}; // actual FINISH register packet, not a fabricated completion
    std::memcpy(memory.getScratchpad(), stream.data(), sizeof(stream));
    memory.writeIORegister(0x1000E040u, 0x30u); // 64-byte ring
    memory.writeIORegister(0x1000E050u, base);
    memory.writeIORegister(spr + 0x10u, base + 48u);
    memory.writeIORegister(spr + 0x80u, 0u);
    memory.writeIORegister(drain + 0x30u, base + 48u);
    memory.writeIORegister(0x1000E010u, (1u << 30u) | (1u << 17u) | (1u << 24u));
    memory.writeIORegister(0x1000E000u, (mode << 2u) | (disabled == 1u ? 0u : 1u));
    if (disabled == 2u) memory.writeIORegister(0x1000F590u, 0x10000u);
    memory.writeIORegister(drain, mode == 2u ? 0x145u : 0x105u);
    require((memory.readIORegister(drain) & 0x100u) != 0u, "Empty MFIFO lost its armed consumer");
    memory.writeIORegister(spr + 0x20u, disabled ? 3u : 1u);
    memory.writeIORegister(spr, 0x100u);
    if (disabled) {
        require(received.empty(), "Disabled DMAC transferred data");
        if (disabled == 1u) memory.writeIORegister(0x1000E000u, (mode << 2u) | 1u);
        else {
            require(memory.readIORegister(0x1000F520u) & 0x10000u, "D_ENABLER does not reflect pause");
            memory.writeIORegister(0x1000F590u, 0u);
        }
    } else {
        require(received.empty(), "Incomplete MFIFO packet was emitted");
        require(memory.readIORegister(drain + 0x20u) == 2u, "MFIFO did not preserve pending QWC");
        memory.writeIORegister(spr + 0x20u, 2u);
        memory.writeIORegister(spr, 0x100u);
    }
    require(received.size() == 32u && std::memcmp(received.data(), stream.data() + 4u, 32u) == 0,
            "MFIFO wrap/TTE changed the GIF packet");
    require((memory.readIORegister(drain) & 0x100u) == 0u, "END did not complete the consumer");
    require(memory.readIORegister(spr + 0x10u) == base + 32u, "MFIFO producer did not wrap");
    const uint32_t status = memory.readIORegister(0x1000E010u);
    require((status & ((1u << 14u) | (1u << 8u) | (1u << (mode == 2u ? 1u : 2u)))) ==
            ((1u << 14u) | (1u << 8u) | (1u << (mode == 2u ? 1u : 2u))), "MFIFO completion/empty status missing");
    memory.writeIORegister(0x1000E010u, 1u << 14u);
    require(!(memory.readIORegister(0x1000E010u) & (1u << 14u)), "MFIFO empty status is not W1C");
}

void dma_chain_test() {
    constexpr uint32_t base = 0x20000u, spr = 0x1000D000u, vif = 0x10009000u;
    for (uint32_t id : {0u, 3u, 4u, 5u, 1u, 2u}) {
        PS2Memory memory;
        require(memory.initialize(), "DMA chain initialization");
        const bool ref = id == 0u || id == 3u || id == 4u;
        const bool scratch = id == 0u;
        const uint32_t mark = 0x0700ABCDu;
        std::array<uint32_t, 24> ring{};
        ring[0] = (id << 28u) | (id == 2u ? 0u : 1u);
        ring[1] = ref ? (scratch ? 0x80000040u : 0x50000u) : base + 64u;
        ring[4] = ref ? 0x70000000u : mark;
        ring[8] = 0x70000000u;
        ring[16] = id == 5u ? 0x60000000u : 0x70000000u;
        std::memcpy(memory.getRDRAM() + base, ring.data(), sizeof(ring));
        if (ref) {
            auto *destination = scratch ? memory.getScratchpad() + 64u : memory.getRDRAM() + 0x50000u;
            std::memset(destination, 0, 16u);
            std::memcpy(destination + 12u, &mark, 4u);
        }
        memory.writeIORegister(0x1000E040u, 0xF0u);
        memory.writeIORegister(0x1000E050u, base);
        memory.writeIORegister(spr + 0x10u, base + (ref ? (id == 0u ? 16u : 32u) : 96u));
        memory.writeIORegister(vif + 0x30u, base);
        memory.writeIORegister(0x1000E000u, 9u);
        memory.writeIORegister(vif, 0x105u);
        require(!(memory.readIORegister(vif) & 0x100u), "MFIFO chain did not complete");
        require(((memory.readIORegister(vif) >> 4u) & 3u) == 0u, "CALL/RET left its address stack active");
        if (id != 2u) require(memory.readIORegister(0x10003C30u) == 0xABCDu, "MFIFO reference/inline data missing");
        if (scratch) require(memory.readIORegister(vif + 0x10u) == 0x80000050u, "Scratchpad MADR lost its hardware selector");
        if (id == 5u) require(memory.readIORegister(vif + 0x40u) == base + 32u, "MFIFO CALL return address");
    }
    PS2Memory normal;
    require(normal.initialize(), "Normal DMA initialization");
    normal.readIORegister(spr); // Must not create a disabled D_CTRL while reading status.
    require(normal.readIORegister(0x1000E000u) == 1u, "Guest sees a disabled DMAC at ELF entry");
    normal.writeIORegister(0x1000E000u, 0x101u);
    require(normal.readIORegister(0x1000E000u) == 0x101u, "D_CTRL RELE was cleared as a channel STR");
    const uint32_t value = 0x12345678u;
    std::memcpy(normal.getScratchpad(), &value, 4u);
    normal.writeIORegister(spr + 0x10u, 0x50000u);
    normal.writeIORegister(spr + 0x20u, 1u);
    normal.writeIORegister(spr, 0x100u);
    require(normal.read32(0x50000u) == value, "Normal scratchpad DMA regressed");
}

void dma_resume_test() {
    PS2Memory memory;
    require(memory.initialize(), "Resume memory initialization");
    constexpr uint32_t base = 0x20000u, spr = 0x1000D000u, vif = 0x10009000u;
    memory.writeIORegister(0x1000E040u, 0xF0u);
    memory.writeIORegister(0x1000E050u, base);
    memory.writeIORegister(spr + 0x10u, base);
    memory.writeIORegister(0x1000E000u, 9u);
    memory.write32(0x50000u, 0x0700ABCDu);
    memory.writeIORegister(vif + 0x10u, 0x50000u);
    memory.writeIORegister(vif + 0x20u, 0xFFFF0001u);
    memory.writeIORegister(vif + 0x30u, base);
    memory.writeIORegister(vif, 0x105u); // resumed REFE, no new tag to fetch
    require(!(memory.readIORegister(vif) & 0x100u) && memory.readIORegister(vif + 0x20u) == 0u,
            "Resumed REFE did not complete");
    require(memory.readIORegister(0x10003C30u) == 0xABCDu, "Resumed REFE payload missing");

    const std::array<uint32_t, 16> ring{
        0x90000001u, 0u, 0u, 0u, 0x0700ABCDu, 0u, 0u, 0u,
        0x70000001u, 0u, 0u, 0u, 0x07001234u, 0u, 0u, 0u};
    std::memcpy(memory.getRDRAM() + base, ring.data(), sizeof(ring));
    memory.writeIORegister(spr + 0x10u, base + 64u);
    memory.writeIORegister(vif + 0x30u, base);
    memory.writeIORegister(vif, 0x185u); // TIE stops after the first tag's payload
    require(!(memory.readIORegister(vif) & 0x100u) && memory.readIORegister(0x10003C30u) == 0xABCDu,
            "Tag IRQ/TIE did not stop at its payload boundary");
    require(memory.readIORegister(vif + 0x30u) == base + 32u, "CNT did not advance TADR with its payload");
}

void transfer_lifetime_test() {
    alignas(PS2Memory) std::array<unsigned char, sizeof(PS2Memory)> storage{};
    auto *memory = new (storage.data()) PS2Memory;
    require(memory->initialize(), "First transfer lifetime initialization");
    const uint32_t row = 0x30000000u;
    memory->processVIF1Data(reinterpret_cast<const uint8_t *>(&row), 4u);
    memory->~PS2Memory();
    memory = new (storage.data()) PS2Memory;
    require(memory->initialize(), "Reused transfer lifetime initialization");
    const uint32_t mark = 0x07004321u;
    memory->processVIF1Data(reinterpret_cast<const uint8_t *>(&mark), 4u);
    require(memory->readIORegister(0x10003C30u) == 0x4321u,
            "Reused memory address retained previous transfer state");
    memory->~PS2Memory();
}

void cop0_count_test() {
    auto owner = std::make_unique<PS2Runtime>();
    auto &runtime = *owner;
    require(runtime.memory().initialize(), "Count memory initialization");
    R5900Context context;
    context.cop0_count = 7u;
    auto &scheduler = runtime.eeScheduler();
    scheduler.reset(runtime.memory().getRDRAM(), context);
    require(runtime.readCop0Count() == 7u, "Count lost its initial value");
    scheduler.accountCycles(18810u);
    require(runtime.readCop0Count() == 18817u, "Count did not advance with EE cycles");
    runtime.writeCop0Count(0xFFFFFFF0u);
    scheduler.accountCycles(32u);
    require(runtime.readCop0Count() == 16u, "Count write/rollover differs");
    auto other = std::make_unique<PS2Runtime>();
    other->writeCop0Count(123u);
    require(other->readCop0Count() == 123u && runtime.readCop0Count() == 16u,
            "Independent consoles share Count state");
    scheduler.reset(runtime.memory().getRDRAM(), context);
    require(runtime.readCop0Count() == 7u, "Scheduler reset retained Count offset");
}

void disconnected_usb_test() {
    using namespace ps2x::iop::detail;
    IopMemory memory;
    IopUsbd usb(memory);
    IopCpuState cpu;
    cpu.gpr[28] = 0x76543210u;
    require(!usb.dispatchImport(4u, cpu), "Unloaded USB service accepted an import");
    usb.setActive(true);
    const auto call = [&](uint16_t ordinal, uint32_t argument) {
        cpu.gpr[4] = argument;
        require(usb.dispatchImport(ordinal, cpu), "USB contract import missing");
        return cpu.gpr[2];
    };
    constexpr uint32_t first = 0x1000u, second = 0x1040u;
    memory.write32(first + 8u, 0x400u);
    memory.write32(second + 8u, 0x400u);
    require(call(4u, 0u) == 0x104u && call(4u, IopMemory::RamSize - 4u) == 0x104u,
            "Invalid USB driver was accepted");
    require(call(4u, first) == 0u && memory.read32(first + 44u) == cpu.gpr[28], "USB driver GP lost");
    require(call(4u, first) == 0x121u, "Duplicate USB driver was accepted");
    require(call(4u, second) == 0u, "Second USB driver registration failed");
    require(memory.read32(first) == second && memory.read32(second + 4u) == first,
            "USB driver list differs");
    require(call(5u, first) == 0u && memory.read32(second + 4u) == 0u, "USB driver unlink failed");
    require(call(5u, first) == 0x104u, "Absent USB driver unregister succeeded");
    for (uint16_t ordinal : {6u, 8u}) require(call(ordinal, 1u) == 0u, "USB reported an attached device");
    for (uint16_t ordinal : {7u, 13u, 17u}) require(call(ordinal, 1u) == 0x101u, "USB accepted absent device");
    for (uint16_t ordinal : {9u, 12u}) require(call(ordinal, 1u) == 0xFFFFFFFFu, "USB opened an absent endpoint");
    memory.write32(0x800u, 0x12345678u);
    cpu.gpr[5] = 0x800u;
    for (uint16_t ordinal : {10u, 11u, 18u}) require(call(ordinal, 1u) == 0x102u, "USB accepted absent pipe");
    require(memory.read32(0x800u) == 0x12345678u, "Rejected USB transfer changed its payload");
    require(call(14u, first) == 0x131u, "Unsupported USB operation reported success");
    require(!usb.dispatchImport(99u, cpu), "Unknown USB import was hidden");
    usb.setActive(false);
    require(!usb.dispatchImport(4u, cpu), "Stopped USB service accepted an import");
    usb.setActive(true);
    require(call(4u, second) == 0u, "USB reload retained driver registry");
}

void yield_at_entry(uint8_t *, R5900Context *context, PS2Runtime *runtime) {
    runtime->postEeEvent(EeEvent{EeEventType::ExternalWake, 0u, 0u});
    require(runtime->eeCheckpointDue(), "Synthetic entry loop did not yield");
    context->pc = 0x1000u;
}

void jump_to_entry(uint8_t *ram, R5900Context *context, PS2Runtime *runtime) {
    (void)runtime->dispatchGuestBranch(ram, context, 0x1000u, 0x1004u, 0u,
                                     PS2Runtime::GuestBranchKind::DirectJump, "Synthetic tail jump");
}

void legacy_entry(uint8_t *, R5900Context *, PS2Runtime *) {}

void yield_at_recursive_return(uint8_t *, R5900Context *context, PS2Runtime *runtime) {
    runtime->postEeEvent(EeEvent{EeEventType::ExternalWake, 0u, 0u});
    require(runtime->eeCheckpointDue(), "Synthetic recursive frame did not yield");
    // A deeper recursive frame can pause at the same numeric PC as its caller's return site.
    context->pc = 0x2008u;
}

void dispatcher_resume_test() {
    auto runtime = std::make_unique<PS2Runtime>();
    require(runtime->memory().initialize(), "Dispatcher memory initialization");
    R5900Context context;
    const auto invoke = [&](PS2Runtime::RecompiledFunction function) {
        runtime->eeScheduler().reset(runtime->memory().getRDRAM(), context);
        g_ps2RecompiledFunctionTable[0x1000u >> 2u] = function;
        return runtime->dispatchGuestBranch(runtime->memory().getRDRAM(), &context, 0x1000u, 0x2000u, 0x2008u,
                                             PS2Runtime::GuestBranchKind::DirectCall, "Synthetic entry call");
    };
    require(!invoke(yield_at_entry) && context.pc == 0x1000u,
            "Yield at callee entry was mistaken for a completed call");
    require(!invoke(jump_to_entry) && context.pc == 0x1000u,
            "Tail jump at callee entry was mistaken for a completed call");
    require(!invoke(yield_at_recursive_return) && context.pc == 0x2008u,
            "Yield in a deeper frame was mistaken for its caller's return");
    require(invoke(legacy_entry) && context.pc == 0x2008u,
            "Legacy handler without a transfer lost its return convention");
    g_ps2RecompiledFunctionTable[0x1000u >> 2u] = nullptr;
}

void unsupported_exec_test() {
    auto runtime = std::make_unique<PS2Runtime>();
    auto& context = runtime->cpu();
    context.pc = 0x100100;
    const std::array<uint32_t, 4> arguments{0x200000, 0x3456, 3, 0x1000};
    for (unsigned i = 0; i < arguments.size(); ++i) context.r[4 + i] = _mm_set_epi32(0, 0, 0, arguments[i]);
    const auto original = context;
    for (const auto syscall : {6u, 7u}) {
        bool stopped = false;
        try {
            ps2_syscalls::dispatchNumericSyscall(syscall, runtime->memory().getRDRAM(), &context, runtime.get());
        } catch (const GuestExecutableRequest& error) {
            require(error.syscall == syscall && error.sourcePc == original.pc,
                    "Executable request lost its operation or source PC");
            require(error.arguments == arguments, "Executable request lost its arguments");
            stopped = std::string_view(error.what()).find("[guest-exec:unsupported]") != std::string_view::npos;
        }
        require(stopped, "Unimplemented executable handoff returned success");
        require(std::memcmp(&context, &original, sizeof(context)) == 0, "Rejected handoff changed CPU state");
    }
}

void console_function_overrides_test() {
    constexpr uint32_t address = 0x4000;
    const auto original = g_ps2RecompiledFunctionTable[address / 4];
    g_ps2RecompiledFunctionTable[address / 4] = legacy_entry;
    {
        auto first = std::make_unique<PS2Runtime>();
        auto second = std::make_unique<PS2Runtime>();
        require(first->memory().initialize(), "Override fixture RAM initialization");
        require(first->replaceFunction(address, jump_to_entry), "Console override registration failed");
        require(first->lookupFunction(address) == jump_to_entry, "Console lost its native override");
        require(second->lookupFunction(address) == legacy_entry &&
                g_ps2RecompiledFunctionTable[address / 4] == legacy_entry, "Native override leaked into another console");
        first->eeScheduler().reset(first->memory().getRDRAM(), first->cpu());
        require(first->lookupFunction(address) == jump_to_entry, "Kernel reset discarded native bindings");
        require(first->replaceFunction(address, nullptr) && !first->hasFunction(address), "Null override did not disable this console's binding");
        require(second->hasFunction(address) && second->lookupFunction(address) == legacy_entry,
                "Disabled function leaked into another console");
    }
    auto fresh = std::make_unique<PS2Runtime>();
    require(fresh->lookupFunction(address) == legacy_entry, "Destroyed console retained its function overrides");
    g_ps2RecompiledFunctionTable[address / 4] = original;
}

static unsigned sifIrqCalls = 0;
static constexpr uint32_t sifCommandData = 0x5000, sifReceive = 0x6000, sifSreg = 0x7000;

static void sif_pause(uint8_t*, R5900Context* context, PS2Runtime* runtime) {
    context->pc = 0;
    runtime->requestStop();
}

static void sif_command_irq(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    require(getRegU32(context, 4) == 5, "SIF command arrived on the outgoing DMA channel");
    uint32_t packet[6]{};
    std::memcpy(packet, ram + sifReceive, sizeof(packet));
    require(packet[0] == 24 && packet[2] == 0x80000001 && packet[4] == 0 && packet[5] == 1,
            "IOP RPC initialization did not deliver a SET_SREG response");
    std::memcpy(ram + sifSreg, &packet[5], 4);
    std::memset(ram + sifReceive, 0, 4);
    ++sifIrqCalls;
    ps2_stubs::sceSifSetDChain(ram, context, runtime);
    runtime->eeScheduler().accountCycles(32);
    context->pc = 0;
    if (sifIrqCalls == 2) runtime->requestStop();
}

static void native_sif_command_test() {
    auto runtime = std::make_unique<PS2Runtime>();
    auto other = std::make_unique<PS2Runtime>();
    require(runtime->memory().initialize() && other->memory().initialize(), "SIF fixture RAM initialization");
    auto* ram = runtime->memory().getRDRAM();
    auto& context = runtime->cpu();
    auto& scheduler = runtime->eeScheduler();
    scheduler.reset(ram, context);
    const auto call = [&](auto function, uint32_t a0, uint32_t a1 = 0) {
        context.r[4] = _mm_set_epi32(0, 0, 0, a0);
        context.r[5] = _mm_set_epi32(0, 0, 0, a1);
        function(ram, &context, runtime.get());
        return getRegU32(&context, 2);
    };
    require((call(ps2_stubs::sceSifGetReg, 4) & 0x20000) != 0, "IOP SIF command service did not initialize");
    const auto mailbox = call(ps2_stubs::sceSifGetReg, 2);
    require(mailbox && runtime->isIopMemoryRange(mailbox, 128), "Ready SIF endpoint has no physical IOP buffer");
    require(call(ps2_stubs::sceSifGetReg, 2) == mailbox, "Repeated SIF query allocated a second endpoint");
    call(ps2_stubs::sceSifSetReg, 0x80000001, sifCommandData);
    require(call(ps2_stubs::sceSifGetReg, 0x80000001) == sifCommandData, "SIF register lost its console value");
    require(PS2IopTransport::readSifRegister(other.get(), 0x80000001) == 0, "SIF register leaked across consoles");
    const uint32_t commandData[] = {sifReceive, 0, mailbox, 0, 0, 0, 0, sifSreg};
    std::memcpy(ram + sifCommandData, commandData, sizeof(commandData));
    std::memset(ram + mailbox, 0xa5, 128);
    scheduler.setFunctionOverride(0x3000, sif_command_irq);
    scheduler.setFunctionOverride(0x2000, sif_pause);
    const auto arm = [&] {
        context.pc = 0x2000;
        scheduler.reset(ram, context);
        const auto handler = scheduler.addIrqHandler(true, 5, 0x3000, false, 0, 0, 0);
        require(handler > 0 && scheduler.setIrqCauseEnabled(true, 5, true) >= 0, "Cannot register SIF0 interrupt fixture");
        call(ps2_stubs::sceSifSetDChain, 0);
        require((runtime->memory().readIORegister(0x1000c000) & 0x100) != 0,
                "SIF0 status read completed an unreceived DMA");
    };
    arm();
    const auto send = [&](std::span<const uint32_t> packet, uint32_t destination) {
        std::memcpy(ram + 0x9000, packet.data(), packet.size_bytes());
        const uint32_t dma[] = {0x9000, destination, static_cast<uint32_t>(packet.size_bytes()), 0x44};
        std::memcpy(ram + 0xa000, dma, sizeof(dma));
        require(call(ps2_stubs::sceSifSetDma, 0xa000, 1) != 0, "SIF packet DMA failed");
    };
    const uint32_t init[] = {20, 0, 0x80000002, 0, sifReceive};
    send(init, mailbox);
    scheduler.run();
    require(sifIrqCalls == 0, "Outgoing SIF DMA raised a SIF0 interrupt");
    arm();
    const uint32_t rpcInit[] = {16, 0, 0x80000002, 1};
    const auto unrelated = runtime->allocateIopMemory(128);
    send(rpcInit, unrelated);
    const uint32_t malformed[] = {15, 0, 0x80000002, 1};
    send(malformed, mailbox);
    scheduler.run();
    require(sifIrqCalls == 0, "Malformed/non-command SIF DMA produced a reply");
    arm();
    send(rpcInit, mailbox);
    send(rpcInit, mailbox); // The second response must wait for the first receive chain.
    uint32_t flag = 0;
    std::memcpy(&flag, ram + sifSreg, 4);
    require(flag == 0 && sifIrqCalls == 0, "HLE changed the native SREG before the original handler ran");
    for (unsigned i = 0; i < 128; ++i) require(ram[mailbox + i] == 0xa5, "SIF aliased IOP RAM onto the EE");
    uint32_t actual[4]{};
    require(runtime->readIopMemory(mailbox, actual, sizeof(actual)) &&
            std::memcmp(actual, rpcInit, sizeof(actual)) == 0, "SIF reply overwrote the incoming IOP packet");
    sifIrqCalls = 0;
    scheduler.run();
    std::memcpy(&flag, ram + sifSreg, 4);
    if (sifIrqCalls != 2 || flag != 1)
        std::cerr << "SIF callbacks=" << sifIrqCalls << " flag=" << flag << " RX size="
                  << runtime->memory().read32(sifReceive) << " CHCR="
                  << runtime->memory().readIORegister(0x1000c000) << '\n';
    require(sifIrqCalls == 2 && flag == 1, "Native command handler lost a queued handshake response");
    PS2IopTransport::reset(runtime.get());
    require(call(ps2_stubs::sceSifGetReg, 0x80000001) == 0, "IOP reset retained another session's SIF registers");
}

int main() {
    try {
        require(device_boundary_test() == 0, "SPU2 DMA/register boundary regression");
        require(csr_test() == 0, "CSR regression");
        vif_stream_test();
        vif_image_continuation_test();
        mfifo_test(2u, false);
        mfifo_test(3u, false);
        mfifo_test(2u, true);
        mfifo_test(2u, 2u);
        dma_chain_test();
        dma_resume_test();
        transfer_lifetime_test();
        cop0_count_test();
        disconnected_usb_test();
        dispatcher_resume_test();
        unsupported_exec_test();
        console_function_overrides_test();
        native_sif_command_test();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
