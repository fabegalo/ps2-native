#pragma once
#include "ps2native/execution.hpp"
#include "ps2native/aot_runtime.hpp"
#include <algorithm>

// Test-only lookup of immutable, compiler-specialized synthetic words.
// Missing entries fail; there is no decoder or instruction fallback here.
void compiled_fixture_step(ps2native::ExecutionContext&,
                           ps2native::SystemEventObserver* = nullptr);
void reset_native_ee_fixture(ps2native::Memory&,std::span<const std::uint8_t> rom);
void seed_native_test_iop(ps2native::Memory&);

namespace ps2native { const AotModule& boot_payload_aot_module(); }
struct NativeBootFixture {
    std::vector<ps2native::AotBlock> blocks;
    std::vector<std::uint32_t> entries;
    ps2native::AotModule module;
    explicit NativeBootFixture(const ps2native::AotModule& rom) :module(rom) {
        const auto& payload=ps2native::boot_payload_aot_module();
        blocks.assign(rom.blocks,rom.blocks+rom.block_count);
        blocks.insert(blocks.end(),payload.blocks,payload.blocks+payload.block_count);
        std::sort(blocks.begin(),blocks.end(),[](auto a,auto b){return a.address<b.address;});
        entries.assign(rom.indirect_entries,rom.indirect_entries+rom.indirect_entry_count);
        entries.insert(entries.end(),payload.indirect_entries,payload.indirect_entries+payload.indirect_entry_count);
        std::sort(entries.begin(),entries.end());entries.erase(std::unique(entries.begin(),entries.end()),entries.end());
        module.blocks=blocks.data();module.block_count=blocks.size();module.indirect_entries=entries.data();module.indirect_entry_count=entries.size();
    }
};
