#include "ps2native/bios.hpp"
// 4 MiB ROM0.
namespace { constexpr std::size_t kBiosRomBytes = 4u * 1024u * 1024u; }
#include "ps2native/sha256.hpp"
#include "ps2native/console.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ps2native {
namespace {
constexpr std::size_t kEntryBytes = 16;
constexpr std::size_t kMaxModules = 1024;

std::string hex32(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}
[[noreturn]] void reject(const std::string& reason) {
    throw std::runtime_error("BIOS ROM0 rejected: " + reason);
}
std::uint16_t u16(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}
std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}
bool name_is(std::span<const std::uint8_t> b, std::size_t at, std::string_view name) {
    for (std::size_t i = 0; i < 10; ++i) {
        const auto expected = i < name.size() ? static_cast<std::uint8_t>(name[i]) : std::uint8_t{0};
        if (b[at + i] != expected) return false;
    }
    return true;
}
bool all_zero(std::span<const std::uint8_t> b, std::size_t at, std::size_t count) {
    return std::all_of(b.begin() + static_cast<std::ptrdiff_t>(at),
                       b.begin() + static_cast<std::ptrdiff_t>(at + count), [](auto v) { return v == 0; });
}
// Printable ASCII, NUL-padded, never empty.
std::string entry_name(std::span<const std::uint8_t> b, std::size_t at) {
    std::string name;
    std::size_t i = 0;
    for (; i < 10 && b[at + i] != 0; ++i) {
        if (b[at + i] < 0x21 || b[at + i] > 0x7E)
            reject("ROMDIR entry at " + hex32(static_cast<std::uint32_t>(at)) + " has a non-printable name");
        name.push_back(static_cast<char>(b[at + i]));
    }
    if (name.empty()) reject("ROMDIR entry at " + hex32(static_cast<std::uint32_t>(at)) + " has an empty name");
    for (; i < 10; ++i)
        if (b[at + i] != 0)
            reject("ROMDIR entry '" + name + "' name is not NUL-padded");
    return name;
}
std::uint32_t align16(std::uint32_t size) { return (size + 15u) & ~15u; }

void parse_extinfo(std::span<const std::uint8_t> rom, std::uint32_t base, BiosModule& module) {
    std::uint32_t at = 0;
    while (at < module.extinfo_size) {
        if (module.extinfo_size - at < 4u)
            reject("EXTINFO of '" + module.name + "' ends inside a field header");
        const auto field = static_cast<std::size_t>(base) + at;
        const auto value = u16(rom, field);
        const auto length = rom[field + 2];
        const auto type = rom[field + 3];
        if ((length & 3u) != 0u || length > module.extinfo_size - at - 4u)
            reject("EXTINFO field of '" + module.name + "' has an invalid length");
        const auto data = field + 4u;
        switch (type) {
            case 0x01: // date
                if (length != 4u || module.date) reject("EXTINFO date of '" + module.name + "' is malformed");
                module.date = u32(rom, data);
                break;
            case 0x02: // version
                if (length != 0u || module.version) reject("EXTINFO version of '" + module.name + "' is malformed");
                module.version = value;
                break;
            case 0x03: { // comment, NUL-terminated inside its 4-byte padded field
                if (length == 0u || !module.comment.empty())
                    reject("EXTINFO comment of '" + module.name + "' is malformed");
                std::size_t n = 0;
                while (n < length && rom[data + n] != 0) ++n;
                if (n == length) reject("EXTINFO comment of '" + module.name + "' is not NUL-terminated");
                module.comment.assign(reinterpret_cast<const char*>(rom.data() + data), n);
                break;
            }
            case 0x7F: // empty marker field
                if (length != 0u) reject("EXTINFO marker of '" + module.name + "' carries data");
                break;
            default:
                reject("EXTINFO of '" + module.name + "' has unknown field type " + std::to_string(type));
        }
        at += 4u + length;
    }
}

BiosVersion parse_romver(std::span<const std::uint8_t> rom, const BiosModule& module) {
    if (module.size < 14u) reject("ROMVER is shorter than 14 bytes");
    std::string text(reinterpret_cast<const char*>(rom.data() + module.offset), 14);
    auto digits = [&](std::size_t from, std::size_t count) {
        unsigned value = 0;
        for (std::size_t i = from; i < from + count; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(text[i]))) reject("ROMVER '" + text + "' is malformed");
            value = value * 10u + static_cast<unsigned>(text[i] - '0');
        }
        return value;
    };
    BiosVersion version;
    version.romver = text;
    version.major = digits(0, 2);
    version.minor = digits(2, 2);
    version.region_code = text[4];
    version.kind_code = text[5];
    const auto year = digits(6, 4);
    const auto month = digits(10, 2);
    const auto day = digits(12, 2);
    if (year < 1999u || month < 1u || month > 12u || day < 1u || day > 31u)
        reject("ROMVER '" + text + "' has an invalid build date");
    switch (version.region_code) {
        case 'J': version.region = "Japan"; break;
        case 'A': version.region = "USA"; break;
        case 'E': version.region = "Europe"; break;
        case 'H': version.region = "Asia"; break;
        case 'C': version.region = "China"; break;
        case 'O': version.region = "Oceania"; break;
        case 'R': version.region = "Russia"; break;
        case 'M': version.region = "Mexico"; break;
        case 'T': version.region = "T10K"; break;
        case 'X': version.region = "Test"; break;
        case 'P': version.region = "Free"; break;
        default: reject("ROMVER '" + text + "' has an unknown region code");
    }
    switch (version.kind_code) {
        case 'C': version.kind = "Console"; break;
        case 'D': version.kind = "Devel"; break;
        case 'Z': version.kind = "Arcade (COH-H)"; break;
        default: reject("ROMVER '" + text + "' has an unknown console kind");
    }
    std::ostringstream date;
    date << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2) << month << '-' << std::setw(2) << day;
    version.date = date.str();
    return version;
}
} // namespace

const BiosModule* BiosImage::find(std::string_view name) const noexcept {
    const auto it = std::find_if(modules.begin(), modules.end(),
                                 [&](const BiosModule& module) { return module.name == name; });
    return it == modules.end() ? nullptr : &*it;
}

static BiosImage load_rom_directory(std::span<const std::uint8_t> rom, const BiosLoadOptions& options, bool primary,bool reboot=false) {
    if (primary && rom.size() != kBiosRomBytes)
        reject("size is " + std::to_string(rom.size()) + " bytes; ROM0 is exactly 4194304 bytes "
               "(partial dumps and concatenated ROM0+ROM1/ROM2/EROM files are not accepted)");
    BiosImage bios;
    bios.sha256 = sha256_hex(rom);
    if (options.expected_sha256) {
        auto expected = *options.expected_sha256;
        std::transform(expected.begin(), expected.end(), expected.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (expected != bios.sha256)
            reject("SHA-256 " + bios.sha256 + " does not match the expected " + expected);
    }

    // The ROMDIR is the 16-byte aligned table that starts with RESET followed by ROMDIR.
    std::optional<std::size_t> romdir;
    for (std::size_t at = 0; at + 2 * kEntryBytes <= rom.size(); at += kEntryBytes) {
        if (name_is(rom, at, "RESET") && name_is(rom, at + kEntryBytes, "ROMDIR")) {
            romdir = at;
            break;
        }
    }
    if (!romdir) reject("no ROMDIR (RESET, ROMDIR, ...) table found");
    bios.romdir_offset = static_cast<std::uint32_t>(*romdir);

    std::uint32_t file_offset = 0;
    std::size_t at = *romdir;
    while (true) {
        if (at + kEntryBytes > rom.size()) reject("ROMDIR runs past the end of the image");
        if (all_zero(rom, at, kEntryBytes)) break; // terminator
        if (bios.modules.size() == kMaxModules) reject("ROMDIR has more than 1024 entries");
        BiosModule module;
        module.name = entry_name(rom, at);
        module.extinfo_size = u16(rom, at + 10);
        module.size = u32(rom, at + 12);
        module.offset = file_offset;
        if (module.size > rom.size() || file_offset > rom.size() - module.size)
            reject("file '" + module.name + "' (" + std::to_string(module.size) + " bytes at " +
                   hex32(file_offset) + ") lies outside the 4 MiB image");
        if ((module.extinfo_size & 3u) != 0u) reject("EXTINFO size of '" + module.name + "' is not 4-byte aligned");
        file_offset += align16(module.size);
        bios.modules.push_back(std::move(module));
        at += kEntryBytes;
    }
    for (const auto* required : {"RESET", "ROMDIR", "EXTINFO", "ROMVER"}) {
        if(!primary && std::string_view(required)=="ROMVER")continue;
        const auto count = std::count_if(bios.modules.begin(), bios.modules.end(),
                                         [&](const BiosModule& m) { return m.name == required; });
        if (count != 1) reject(std::string("ROMDIR must contain exactly one '") + required + "' entry");
    }
    if (bios.modules[2].name != "EXTINFO") reject("EXTINFO must be the third ROMDIR entry");
    const auto& reset = bios.modules[0];
    if (!reboot && reset.size == 0u) reject("RESET is empty: the image has no code at the reset vector");
    const auto& table = bios.modules[1];
    if (table.offset != bios.romdir_offset)
        reject("ROMDIR file offset " + hex32(table.offset) + " disagrees with the table at " +
               hex32(bios.romdir_offset));
    if (table.size != (bios.modules.size() + 1u) * kEntryBytes)
        reject("ROMDIR size " + std::to_string(table.size) + " does not cover its " +
               std::to_string(bios.modules.size()) + " entries and terminator");

    const auto& extinfo = bios.modules[2];
    std::uint32_t extinfo_used = 0;
    for (auto& module : bios.modules) {
        module.extinfo_offset = extinfo_used;
        extinfo_used += module.extinfo_size;
    }
    if (extinfo_used != extinfo.size)
        reject("EXTINFO size " + std::to_string(extinfo.size) + " differs from the " +
               std::to_string(extinfo_used) + " bytes declared by ROMDIR entries");
    const auto extinfo_base = extinfo.offset;
    for (auto& module : bios.modules)
        parse_extinfo(rom, extinfo_base + module.extinfo_offset, module);

    if(primary)bios.version = parse_romver(rom, *bios.find("ROMVER"));
    return bios;
}

BiosImage load_bios_rom0(std::span<const std::uint8_t> rom, const BiosLoadOptions& options) {
    auto bios=load_rom_directory(rom,options,true);bios.rom0.assign(rom.begin(),rom.end());return bios;
}
const BiosModule* IopRebootImage::find(std::string_view name) const noexcept {
    const auto it=std::find_if(modules.begin(),modules.end(),[&](const auto& module){return module.name==name;});
    return it==modules.end()?nullptr:&*it;
}
IopRebootImage load_iop_reboot_image(std::vector<std::uint8_t> bytes,const BiosLoadOptions& options) {
    if(bytes.empty() || bytes.size()>2u*1024u*1024u)
        throw std::runtime_error("IOP reboot image size "+std::to_string(bytes.size())+" exceeds its 2 MiB preparation limit");
    auto directory=load_rom_directory(bytes,options,false,true);
    return {std::move(bytes),std::move(directory.sha256),std::move(directory.modules)};
}

Rom1Image load_bios_rom1(std::span<const std::uint8_t> bytes, const BiosImage& bios,
                        const ConsoleHardware& hardware, const Rom1Identity& identity) {
    const auto fail=[](const char* why) {throw std::runtime_error(std::string("ROM1 rejected: ")+why);};
    if(bytes.empty() || bytes.size()>4u*1024u*1024u || bytes.size()%16u)fail("invalid mapped size");
    if(identity.provenance.empty() || identity.build.empty() || identity.dvd_version.empty())
        fail("explicit compatibility provenance and internal identity are required");
    if(identity.rom0_sha256!=bios.sha256 || sha256_hex(bios.rom0)!=bios.sha256 ||
       identity.romver!=bios.version.romver || hardware.romver!=identity.romver ||
       identity.model!=hardware.model || identity.mainboard!=hardware.mainboard)
        fail("ROM0/ROMVER/console binding mismatch; firmware revisions are never mixed");
    if(identity.sha256.size()!=64u || sha256_hex(bytes)!=identity.sha256)fail("SHA-256 mismatch");
    const auto parsed=load_rom_directory(bytes,{},false);
    if(parsed.find("ROMDIR")->comment!=identity.build)fail("ROMDIR build mismatch");
    const auto* version=parsed.find(std::string("DVDVER")+bios.version.region_code);
    if(!version)version=parsed.find("DVDVER");
    if(!version)fail("DVD version metadata absent");
    std::string text(reinterpret_cast<const char*>(bytes.data()+version->offset),version->size);
    while(!text.empty() && (text.back()=='\0' || text.back()=='\r' || text.back()=='\n'))text.pop_back();
    if(text!=identity.dvd_version)fail("DVD version mismatch");
    return {{bytes.begin(),bytes.end()},identity};
}

} // namespace ps2native
