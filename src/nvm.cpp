#include "ps2native/nvm.hpp"

#include "ps2native/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ps2native {
namespace {
// MECHACON v5+ EEPROM (byte offsets). Configuration areas, model name, i.LINK
// ID and console ID offsets and the block counts per area are PCSX2's layout
// for "eeproms from bios v1.70 and up"; the model name at 0x1B0 is also what
// psdevwiki lists for Dragon MECHACONs. Behavioural reference only: no code
// is taken from either.
constexpr NvmLayoutInfo kDragon{
    "Dragon (MECHACON v5+)",
    {NvmArea{0x270u, 4u}, NvmArea{0x2B0u, 2u}, NvmArea{0x200u, 7u}},
    0x1B0u,
    0x1E0u,
    0x1F0u,
    "PCSX2 NVM layout for BIOS v1.70+ and psdevwiki's Dragon model name offset",
};

std::string hex_bytes(std::span<const std::uint8_t> bytes) {
    std::ostringstream stream;
    for (const auto byte : bytes)
        stream << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
    return stream.str();
}

[[noreturn]] void refuse(const std::string& reason) { throw std::runtime_error("NVM image refused: " + reason); }
} // namespace

const NvmLayoutInfo& nvm_layout_info(NvmLayout layout) noexcept {
    switch (layout) {
        case NvmLayout::Dragon: return kDragon;
    }
    return kDragon;
}

std::uint16_t NvmImage::word(std::uint32_t address) const {
    if (address >= kNvmWords) throw std::out_of_range("NVM word address out of range");
    return static_cast<std::uint16_t>(bytes[2u * address] | (bytes[2u * address + 1u] << 8));
}

std::span<const std::uint8_t, kNvmConfigBlockBytes> NvmImage::config_block(unsigned area, unsigned index) const {
    const auto& info = nvm_layout_info(layout).config.at(area);
    if (index >= info.blocks) throw std::out_of_range("NVM configuration block out of range");
    return std::span<const std::uint8_t, kNvmConfigBlockBytes>(bytes.data() + info.offset + index * kNvmConfigBlockBytes,
                                                              kNvmConfigBlockBytes);
}

NvmImage load_nvm_image(std::span<const std::uint8_t> bytes, const ConsoleHardware& hardware,
                        const NvmLoadOptions& options) {
    const auto console = std::string(hardware.model) + ":" + std::string(hardware.mainboard);
    if (bytes.size() != kNvmBytes)
        refuse(std::to_string(bytes.size()) + " bytes; a MECHACON NVM dump holds 1024 (512 words read with "
               "sceCdReadNVM, as PS2Ident writes it)");
    NvmImage image;
    image.bytes.assign(bytes.begin(), bytes.end());
    image.sha256 = sha256_hex(bytes);
    if (options.expected_sha256) {
        auto expected = *options.expected_sha256;
        std::transform(expected.begin(), expected.end(), expected.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (expected != image.sha256) refuse("SHA-256 " + image.sha256 + " does not match the expected " + expected);
    }
    if (!hardware.mechacon)
        refuse("console " + console + " has no MECHACON data in its profile, so its NVM layout is unknown");
    const auto& mechacon = *hardware.mechacon;
    image.layout = mechacon.layout;
    const auto& layout = nvm_layout_info(image.layout);

    // Model name: printable ASCII up to the first NUL, within 16 bytes.
    const auto name = bytes.subspan(layout.model_name, 16u);
    const auto end = std::find(name.begin(), name.end(), std::uint8_t{0});
    if (end == name.end() || end == name.begin() ||
        !std::all_of(name.begin(), end, [](std::uint8_t c) { return c >= 0x20u && c < 0x7Fu; }))
        refuse("no NUL-terminated model name at NVM offset 0x1B0 (bytes " + hex_bytes(name) + "); not a " +
               std::string(layout.name) + " NVM, or not initialised");
    image.model_name.assign(name.begin(), end);
    if (image.model_name != hardware.model)
        refuse("the NVM belongs to a " + image.model_name + ", not to the selected " + console +
               "; NVM images are never shared between models");
    const auto model_id = bytes.subspan(layout.ilink_id + 1u, 3u);
    if (!std::equal(model_id.begin(), model_id.end(), mechacon.model_id.begin()))
        refuse("model ID " + hex_bytes(model_id) + " (i.LINK ID bytes 1-3) differs from " +
               hex_bytes(mechacon.model_id) + ", the PS2Ident database value for " + console);
    const auto con_model_id = bytes.subspan(layout.console_id, 2u);
    if (!std::equal(con_model_id.begin(), con_model_id.end(), mechacon.con_model_id.begin()))
        refuse("console model ID " + hex_bytes(con_model_id) + " (console ID bytes 0-1) differs from " +
               hex_bytes(mechacon.con_model_id) + ", the PS2Ident database value for " + console);

    for (unsigned area = 0; area < kNvmConfigAreas; ++area)
        for (unsigned index = 0; index < layout.config[area].blocks; ++index) {
            const auto block = image.config_block(area, index);
            std::uint8_t sum = 0;
            for (std::size_t i = 0; i + 1u < block.size(); ++i) sum = static_cast<std::uint8_t>(sum + block[i]);
            image.config_checksum_ok[area].push_back(sum == block.back());
        }
    return image;
}
} // namespace ps2native
