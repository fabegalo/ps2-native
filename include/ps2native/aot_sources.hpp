#pragma once
#include "ps2native/aot.hpp"
#include <filesystem>

namespace ps2native {
struct AotSource {
    std::string name;
    std::string text;
};
struct AotSources {
    std::string dispatcher;
    std::vector<AotSource> partitions;
};
// Address buckets never rebalance when unrelated functions are discovered.
// Each block has one body; unchanged partitions keep their content-addressed
// filename and object file. module_key isolates symbols of linked modules.
[[nodiscard]] AotSources generate_aot_sources(const ElfImage& image, const AotOptions& options,
    const std::string& module_key, std::uint32_t partition_bytes = 65536);
// Write the dispatcher, independent C++ units and a non-executable .sources
// manifest. Identical files retain their mtimes. No original input is copied.
bool write_aot_sources(const std::filesystem::path& dispatcher, const AotSources& sources);
// Verifies every unit, including the dispatcher. Throws on missing/corrupt units.
[[nodiscard]] std::string aot_sources_sha256(const std::filesystem::path& dispatcher);
} // namespace ps2native
