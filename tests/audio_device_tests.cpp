#include "pcsx2_audio.h"
#include "kzspu2.h"
#include "emulator/core/iop_memory.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        for (int session = 0; session < 2; ++session) {
            PCSX2Audio audio;
            bool rejected = false;
            try { PCSX2Audio duplicate; }
            catch (const std::logic_error&) { rejected = true; }
            require(rejected, "Duplicate global SPU2 accepted");
            ps2x::iop::detail::IopMemory memory;
            uint64_t cycle = 0;
            memory.attachSpu2(&audio, &cycle);
            const auto started = std::chrono::steady_clock::now();
            for (int reboot = 0; reboot < 4; ++reboot) {
                memory.reset();
                cycle = 0;
                require(kzspu2GetStatus().iopCycle == 0 && kzspu2AvailableFrames() == 0,
                        "Reset retained the old clock or queued PCM");
                require(audio.takeEvents() == 0, "Reset retained interrupts");
                uint32_t events = 0;
                for (uint32_t core = 0; core < 2; ++core) {
                    const uint32_t regs = 0xbf900000u + core * 0x400u;
                    memory.write16(regs + 0x19c, 0);
                    memory.write16(regs + 0x19e, 0x5008); // IRQ inside the DMA destination range.
                    memory.write16(regs + 0x19a, 0x8040); // Enable SPU2 and this core's IRQ.
                    memory.write16(regs + 0x1a8, 0);
                    memory.write16(regs + 0x1aa, 0x5000);
                    require(memory.read16(regs + 0x1aa) == 0x5000, "IOP register readback failed");
                    const uint32_t dma = core ? 0x1f801500u : 0x1f8010c0u;
                    memory.write32(0x10000u + core * 0x100u, 0x12345678u + core);
                    memory.write32(dma, 0x10000u + core * 0x100u);
                    memory.write32(dma + 4, 0x00010010);
                    memory.write32(dma + 8, 0x01000201);
                    cycle += 128 * 768;
                    const auto coreEvents = memory.advanceSpu2();
                    require(coreEvents & 1u, "Real SPU2 IRQ missing from an enabled core");
                    events |= coreEvents;
                    memory.write16(regs + 0x19a, 0x8000); // Acknowledge before testing the other core.
                }
                for (int block = 2; block < 100; ++block) {
                    cycle += 128 * 768;
                    events |= memory.advanceSpu2();
                }
                require((events & 6u) == 6u, "Real DMA completion missing from an SPU2 core");
                require(kzspu2Memory()[0x5000] == 0x5679, "DMA did not read the shared IOP RAM");
                require(!(memory.read32(0x1f8010c8u) & 0x01000000u) &&
                        !(memory.read32(0x1f801508u) & 0x01000000u), "IOP DMA remained busy");
                require(kzspu2GetStatus().framesDropped == 0, "SDL backpressure dropped PCM");
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            std::printf("session=%d frames=51200 elapsed_ms=%lld dropped=0\n", session, (long long)ms);
            require(ms >= 700 && ms < 5000, "Audio pacing did not follow the output clock");
        }
        std::puts("SDL/SPU2/IOP lifecycle passed (dummy output)");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
