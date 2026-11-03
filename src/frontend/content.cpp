#include "ps2native/frontend/content.hpp"

#include "ps2native/bios.hpp"
#include "ps2native/console.hpp"
#include "ps2native/io.hpp"
#include "ps2native/nvm.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace ps2native::frontend {
namespace {
constexpr std::uint64_t kRom0Bytes = 4u * 1024u * 1024u;
constexpr std::uint64_t kParserReadLimit = 128u * 1024u * 1024u; // read_file's limit
constexpr std::size_t kIsoSector = 2048;
constexpr std::size_t kRawSector = 2352;

std::string lower_extension(const std::filesystem::path& path) {
    auto text = path_utf8(path.extension());
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
std::string hex(std::uint32_t value, int digits) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return stream.str();
}
std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}
std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
ProbeResult refused(ProbeStatus status, std::string error) {
    ProbeResult result;
    result.status = status;
    result.error = std::move(error);
    return result;
}
bool accessible(const std::filesystem::path& path) {
    try { return file_exists(path); } catch (const std::exception&) { return false; }
}
// File name of a CUE FILE entry (directories dropped), from UTF-8 text.
std::filesystem::path path_from_utf8_name(const std::string& name) {
    const auto slash = name.find_last_of("/\\");
    const auto base = slash == std::string::npos ? name : name.substr(slash + 1);
    return std::filesystem::path(std::u8string(base.begin(), base.end()));
}
std::string comparable(const std::filesystem::path& path) {
    auto text = path_utf8(display_path(path).lexically_normal());
#ifdef _WIN32
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return text;
}

// ISO 9660 user-data sectors of an image with 2048-byte sectors or raw
// 2352-byte CD sectors (Mode 1: data at 16, Mode 2 Form 1: data at 24).
class SectorReader {
public:
    explicit SectorReader(InputFile& file) : file_(file) {}
    bool detect() {
        std::array<std::uint8_t, kRawSector> sector{};
        if (file_.read_at(16u * kIsoSector, std::span(sector.data(), kIsoSector)) == kIsoSector &&
            is_primary_descriptor(sector.data())) {
            sector_size_ = kIsoSector;
            data_offset_ = 0;
            format_ = "ISO 9660 image (2048-byte sectors)";
            return true;
        }
        if (file_.read_at(16u * kRawSector, sector) == kRawSector && is_sync(sector.data())) {
            const unsigned mode = sector[15];
            const std::size_t offset = mode == 1 ? 16 : mode == 2 ? 24 : 0;
            if (offset != 0 && is_primary_descriptor(sector.data() + offset)) {
                sector_size_ = kRawSector;
                data_offset_ = offset;
                format_ = mode == 1 ? "Raw CD image (2352-byte sectors, Mode 1)"
                                    : "Raw CD image (2352-byte sectors, Mode 2 Form 1)";
                return true;
            }
        }
        return false;
    }
    bool read(std::uint32_t lba, std::span<std::uint8_t, kIsoSector> out) {
        return file_.read_at(static_cast<std::uint64_t>(lba) * sector_size_ + data_offset_, out) == kIsoSector;
    }
    [[nodiscard]] std::size_t sector_size() const { return sector_size_; }
    [[nodiscard]] const std::string& format() const { return format_; }

private:
    static bool is_primary_descriptor(const std::uint8_t* p) { return p[0] == 1 && std::memcmp(p + 1, "CD001", 5) == 0; }
    static bool is_sync(const std::uint8_t* p) {
        if (p[0] != 0 || p[11] != 0) return false;
        return std::all_of(p + 1, p + 11, [](std::uint8_t b) { return b == 0xFFu; });
    }
    InputFile& file_;
    std::size_t sector_size_{kIsoSector};
    std::size_t data_offset_{};
    std::string format_;
};

struct DirectoryFile {
    std::uint32_t lba{};
    std::uint32_t size{};
};
// Looks a file up in the root directory (ISO 9660 directory records).
std::optional<DirectoryFile> find_root_file(SectorReader& reader, const std::uint8_t* descriptor, std::string_view name) {
    const std::uint8_t* root = descriptor + 156;
    const std::uint32_t lba = le32(root + 2);
    const std::uint32_t length = le32(root + 10);
    const std::uint32_t sectors = std::min<std::uint32_t>((length + kIsoSector - 1) / kIsoSector, 64u);
    std::array<std::uint8_t, kIsoSector> sector{};
    for (std::uint32_t s = 0; s < sectors; ++s) {
        if (!reader.read(lba + s, sector)) return std::nullopt;
        for (std::size_t at = 0; at < kIsoSector;) {
            const std::uint8_t record = sector[at];
            if (record == 0) break; // records never span sectors: the rest is padding
            if (record < 34 || at + record > kIsoSector) return std::nullopt;
            const std::uint8_t name_length = sector[at + 32];
            if (33u + name_length <= record) {
                std::string id(reinterpret_cast<const char*>(sector.data() + at + 33), name_length);
                if (const auto semicolon = id.find(';'); semicolon != std::string::npos) id.resize(semicolon);
                const bool directory = (sector[at + 25] & 0x02u) != 0;
                if (!directory && upper(id) == name) return DirectoryFile{le32(sector.data() + at + 2), le32(sector.data() + at + 10)};
            }
            at += record;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> find_cover(const std::filesystem::path& disc, const std::string& serial,
                                                const ProbeContext& context) {
    static constexpr std::array<const char*, 4> kExtensions{".png", ".jpg", ".jpeg", ".webp"};
    if (!context.covers_folder.empty() && !serial.empty())
        for (const char* ext : kExtensions) {
            auto candidate = context.covers_folder / std::filesystem::path(serial + ext);
            if (accessible(candidate)) return candidate;
        }
    for (const char* ext : kExtensions) {
        auto candidate = disc;
        candidate.replace_extension(ext);
        if (accessible(candidate)) return candidate;
    }
    return std::nullopt;
}
} // namespace

const char* content_kind_name(ContentKind kind) noexcept {
    switch (kind) {
    case ContentKind::Bios: return "bios";
    case ContentKind::Disc: return "disc";
    }
    return "unknown";
}
std::optional<ContentKind> content_kind_from_name(std::string_view name) noexcept {
    if (name == "bios") return ContentKind::Bios;
    if (name == "disc") return ContentKind::Disc;
    return std::nullopt;
}
const char* probe_status_name(ProbeStatus status) noexcept {
    switch (status) {
    case ProbeStatus::Pending: return "pending";
    case ProbeStatus::Valid: return "valid";
    case ProbeStatus::Invalid: return "invalid";
    case ProbeStatus::Unsupported: return "unsupported";
    case ProbeStatus::Missing: return "missing";
    }
    return "unknown";
}
std::optional<ProbeStatus> probe_status_from_name(std::string_view name) noexcept {
    for (const auto status : {ProbeStatus::Pending, ProbeStatus::Valid, ProbeStatus::Invalid,
                              ProbeStatus::Unsupported, ProbeStatus::Missing})
        if (name == probe_status_name(status)) return status;
    return std::nullopt;
}

const CueTrack* CueSheet::data_track() const noexcept {
    for (const auto& track : tracks)
        if (track.mode != "AUDIO") return &track;
    return nullptr;
}

CueSheet parse_cue_sheet(const std::filesystem::path& path) {
    if (regular_file_size(path) > 1024u * 1024u) throw std::runtime_error("The CUE sheet is larger than 1 MiB.");
    const auto bytes = read_file(path);
    std::string text(bytes.begin(), bytes.end());
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    CueSheet sheet;
    std::filesystem::path current;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        const auto content = trim(line);
        std::istringstream words(content);
        std::string command;
        words >> command;
        command = upper(command);
        if (command == "FILE") {
            // FILE "name with spaces.bin" BINARY  |  FILE name.bin BINARY
            std::string name;
            const auto rest = trim(std::string_view(content).substr(4));
            if (rest.starts_with('"')) {
                const auto close = rest.find('"', 1);
                if (close == std::string::npos) throw std::runtime_error("Malformed FILE line in the CUE sheet: " + content);
                name = rest.substr(1, close - 1);
            } else {
                name = rest.substr(0, rest.find_first_of(" \t"));
            }
            if (name.empty()) throw std::runtime_error("Empty FILE name in the CUE sheet.");
            // Only the file name is used: sheets reference tracks next to themselves.
            current = path.parent_path() / path_from_utf8_name(name);
            sheet.files.push_back(current);
        } else if (command == "TRACK") {
            unsigned number = 0;
            std::string mode;
            words >> number >> mode;
            if (current.empty() || number == 0 || mode.empty())
                throw std::runtime_error("Malformed TRACK line in the CUE sheet: " + content);
            sheet.tracks.push_back({number, upper(mode), current});
        }
    }
    if (sheet.tracks.empty()) throw std::runtime_error("The CUE sheet lists no FILE and TRACK entries.");
    return sheet;
}

bool is_disc_image_extension(const std::filesystem::path& path) {
    static constexpr std::array<std::string_view, 9> kExtensions{".iso", ".bin", ".img", ".cso", ".zso", ".chd", ".gz", ".mdf", ".cue"};
    const auto ext = lower_extension(path);
    return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}
bool is_nvm_candidate(const std::filesystem::path& path) {
    if (lower_extension(path) == ".nvm") return true;
    try { return regular_file_size(path) == kNvmBytes; } catch (const std::exception&) { return false; }
}
std::optional<ContentKind> guess_content_kind(const std::filesystem::path& path) {
    std::uint64_t size = 0;
    try { size = regular_file_size(path); } catch (const std::exception&) { return std::nullopt; }
    if (size == kRom0Bytes) return ContentKind::Bios;
    if (is_disc_image_extension(path)) return ContentKind::Disc;
    return std::nullopt;
}

std::vector<std::string> console_profiles_for_romver(std::string_view romver) {
    std::vector<std::string> profiles;
    for (const auto& profile : known_console_hardware())
        if (profile.romver == romver) profiles.push_back(std::string(profile.model) + ":" + std::string(profile.mainboard));
    return profiles;
}

ProbeResult probe_bios(const std::filesystem::path& path) {
    if (!accessible(path)) return refused(ProbeStatus::Missing, "The file is not accessible: " + path_utf8(path));
    try {
        const auto size = regular_file_size(path);
        if (size > kParserReadLimit)
            return refused(ProbeStatus::Invalid, "A ROM0 dump is exactly 4 MiB (4194304 bytes); this file has " +
                                                     std::to_string(size) + " bytes.");
        const auto bytes = read_file(path);
        const auto bios = load_bios_rom0(bytes);
        ProbeResult result;
        result.status = ProbeStatus::Valid;
        const auto& v = bios.version;
        std::ostringstream version;
        version << v.major << '.' << std::setw(2) << std::setfill('0') << v.minor;
        result.info["sha256"] = bios.sha256;
        result.info["romver"] = v.romver;
        result.info["version"] = version.str();
        result.info["region"] = v.region;
        result.info["region_code"] = std::string(1, v.region_code);
        result.info["console_kind"] = v.kind;
        result.info["build_date"] = v.date;
        result.info["romdir_files"] = std::to_string(bios.modules.size());
        result.info["size"] = std::to_string(bytes.size());
        std::string joined;
        for (const auto& profile : console_profiles_for_romver(v.romver)) joined += (joined.empty() ? "" : ",") + profile;
        result.info["profiles"] = joined;
        if (joined.empty())
            result.info["warning"] = "No console profile of the core database ships with ROMVER " + v.romver +
                                     ": board registers and NVM reads will be refused during the diagnostic.";
        for (const auto& module : bios.modules) {
            std::ostringstream line;
            line << module.name << '\t' << hex(module.offset, 8) << '\t' << module.size;
            line << '\t' << (module.version ? hex(*module.version, 4) : std::string());
            line << '\t' << module.comment;
            result.details.push_back(line.str());
        }
        return result;
    } catch (const std::exception& error) {
        return refused(ProbeStatus::Invalid, error.what());
    }
}

ProbeResult probe_nvm(const std::filesystem::path& path, std::string_view console_profile) {
    if (!accessible(path)) return refused(ProbeStatus::Missing, "The NVM file is not accessible: " + path_utf8(path));
    if (console_profile.empty())
        return refused(ProbeStatus::Pending, "Select the console profile first: an NVM is validated against its console.");
    const auto* hardware = find_console_hardware(console_profile);
    if (hardware == nullptr)
        return refused(ProbeStatus::Invalid, "Unknown or ambiguous console profile " + std::string(console_profile) + ".");
    try {
        const auto size = regular_file_size(path);
        if (size > kParserReadLimit)
            return refused(ProbeStatus::Invalid, "An NVM image has 1024 bytes; this file has " + std::to_string(size) + " bytes.");
        const auto image = load_nvm_image(read_file(path), *hardware);
        ProbeResult result;
        result.status = ProbeStatus::Valid;
        result.info["sha256"] = image.sha256;
        result.info["model_name"] = image.model_name;
        result.info["layout"] = std::string(nvm_layout_info(image.layout).name);
        result.info["console_profile"] = std::string(console_profile);
        std::string checksums;
        for (unsigned area = 0; area < kNvmConfigAreas; ++area) {
            if (area != 0) checksums += ' ';
            checksums += std::to_string(area) + ':';
            for (const bool ok : image.config_checksum_ok[area]) checksums += ok ? '+' : '-';
        }
        result.info["checksums"] = checksums;
        return result;
    } catch (const std::exception& error) {
        return refused(ProbeStatus::Invalid, error.what());
    }
}

std::string normalize_serial(std::string_view boot_file) {
    std::string letters;
    std::string digits;
    std::size_t at = 0;
    while (at < boot_file.size() && std::isalpha(static_cast<unsigned char>(boot_file[at])) && letters.size() < 4)
        letters += static_cast<char>(std::toupper(static_cast<unsigned char>(boot_file[at++])));
    if (letters.size() != 4) return {};
    for (; at < boot_file.size(); ++at) {
        const char c = boot_file[at];
        if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
        else if (c != '_' && c != '-' && c != '.') break;
    }
    if (digits.size() != 5) return {};
    return letters + "-" + digits;
}

std::string region_from_serial(std::string_view serial) {
    if (serial.size() < 4) return {};
    const auto prefix = serial.substr(0, 4);
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 18> kRegions{{
        {"SLUS", "NTSC-U"}, {"SCUS", "NTSC-U"}, {"PBPX", "NTSC-J"}, {"PAPX", "NTSC-J"},
        {"SLES", "PAL"}, {"SCES", "PAL"}, {"SLED", "PAL"}, {"SCED", "PAL"},
        {"SLPS", "NTSC-J"}, {"SLPM", "NTSC-J"}, {"SCPS", "NTSC-J"}, {"SCPM", "NTSC-J"},
        {"SCAJ", "NTSC-J"}, {"SLAJ", "NTSC-J"}, {"SLKA", "NTSC-K"}, {"SCKA", "NTSC-K"},
        {"SCCS", "NTSC-C"}, {"SLCS", "NTSC-C"},
    }};
    for (const auto& [key, region] : kRegions)
        if (prefix == key) return std::string(region);
    return {};
}

ProbeResult probe_disc(const std::filesystem::path& path, const ProbeContext& context) {
    if (!accessible(path)) return refused(ProbeStatus::Missing, "The file is not accessible: " + path_utf8(path));
    if (lower_extension(path) == ".cue") {
        CueSheet sheet;
        try {
            sheet = parse_cue_sheet(path);
        } catch (const std::exception& error) {
            return refused(ProbeStatus::Invalid, error.what());
        }
        const auto* data = sheet.data_track();
        if (data == nullptr) return refused(ProbeStatus::Invalid, "The CUE sheet has only audio tracks: not a PlayStation disc.");
        if (!accessible(data->file))
            return refused(ProbeStatus::Missing, "The data track referenced by the CUE sheet is missing: " +
                                                     path_utf8(display_path(data->file).filename()));
        auto result = probe_disc(data->file, context);
        result.info["cue_tracks"] = std::to_string(sheet.tracks.size());
        result.info["data_track"] = path_utf8(display_path(data->file));
        if (const auto format = result.info.find("format"); format != result.info.end())
            format->second = "CUE sheet, " + std::to_string(sheet.tracks.size()) + " track(s); data track " + data->mode + " · " + format->second;
        if (!result.info.contains("cover"))
            if (const auto cover = find_cover(path, result.info.contains("serial") ? result.info["serial"] : std::string(), context))
                result.info["cover"] = path_utf8(display_path(*cover));
        if (result.status == ProbeStatus::Missing) result.status = ProbeStatus::Invalid;
        return result;
    }
    try {
        InputFile file(path);
        ProbeResult result;
        result.info["size"] = std::to_string(file.size());
        std::array<std::uint8_t, 16> magic{};
        const auto got = file.read_at(0, magic);
        const auto starts = [&](const char* text) {
            const auto n = std::strlen(text);
            return got >= n && std::memcmp(magic.data(), text, n) == 0;
        };
        if (starts("CISO") || starts("ZISO") || starts("MComprHD") || (got >= 2 && magic[0] == 0x1Fu && magic[1] == 0x8Bu)) {
            result.status = ProbeStatus::Unsupported;
            result.info["format"] = starts("CISO") ? "CSO (compressed ISO)" : starts("ZISO") ? "ZSO (compressed ISO)"
                                  : starts("MComprHD") ? "CHD (compressed hunks)" : "gzip-compressed image";
            result.error = "Compressed disc images are not readable yet. Register the uncompressed ISO instead.";
            return result;
        }
        SectorReader reader(file);
        if (!reader.detect())
            return refused(ProbeStatus::Invalid, "No ISO 9660 primary volume descriptor at sector 16: not a readable disc image.");
        result.info["format"] = reader.format();
        std::array<std::uint8_t, kIsoSector> descriptor{};
        if (!reader.read(16, descriptor)) return refused(ProbeStatus::Invalid, "Cannot read the primary volume descriptor.");
        result.info["volume_id"] = trim(std::string_view(reinterpret_cast<const char*>(descriptor.data() + 40), 32));
        const std::uint32_t sectors = le32(descriptor.data() + 80);
        result.info["sectors"] = std::to_string(sectors);
        const std::string_view date(reinterpret_cast<const char*>(descriptor.data() + 813), 8);
        if (std::all_of(date.begin(), date.end(), [](char c) { return c >= '0' && c <= '9'; }) && date != "00000000")
            result.info["volume_date"] = std::string(date.substr(0, 4)) + "-" + std::string(date.substr(4, 2)) + "-" +
                                         std::string(date.substr(6, 2));
        if (reader.sector_size() == kRawSector) result.info["media"] = "CD";
        else if (sectors > 360000u) result.info["media"] = "DVD";
        const std::uint64_t expected = static_cast<std::uint64_t>(sectors) * reader.sector_size();
        if (file.size() < expected)
            result.info["warning"] = "The image is smaller than its volume (" + std::to_string(file.size()) + " of " +
                                     std::to_string(expected) + " bytes): it may be truncated.";

        const auto cnf = find_root_file(reader, descriptor.data(), "SYSTEM.CNF");
        if (!cnf) {
            result.status = ProbeStatus::Invalid;
            result.error = "No SYSTEM.CNF in the root directory: this is not a PlayStation disc.";
            return result;
        }
        std::string text;
        const std::uint32_t cnf_size = std::min<std::uint32_t>(cnf->size, 4096u);
        std::array<std::uint8_t, kIsoSector> sector{};
        for (std::uint32_t at = 0; at < cnf_size; at += kIsoSector) {
            if (!reader.read(cnf->lba + at / kIsoSector, sector)) return refused(ProbeStatus::Invalid, "Cannot read SYSTEM.CNF.");
            text.append(reinterpret_cast<const char*>(sector.data()), std::min<std::size_t>(kIsoSector, cnf_size - at));
        }
        std::map<std::string, std::string> keys;
        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line);) {
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            keys[upper(trim(std::string_view(line).substr(0, equals)))] = trim(std::string_view(line).substr(equals + 1));
        }
        std::string boot;
        if (keys.contains("BOOT2")) {
            boot = keys["BOOT2"];
            result.info["platform"] = "PlayStation 2";
        } else if (keys.contains("BOOT")) {
            boot = keys["BOOT"];
            result.info["platform"] = "PlayStation";
        } else {
            return refused(ProbeStatus::Invalid, "SYSTEM.CNF has no BOOT2 or BOOT entry.");
        }
        result.info["boot"] = boot;
        auto file_name = boot.substr(boot.find_last_of("\\/:") == std::string::npos ? 0 : boot.find_last_of("\\/:") + 1);
        if (const auto semicolon = file_name.find(';'); semicolon != std::string::npos) file_name.resize(semicolon);
        const auto serial = normalize_serial(file_name);
        if (!serial.empty()) {
            result.info["serial"] = serial;
            if (const auto region = region_from_serial(serial); !region.empty()) result.info["region"] = region;
        }
        if (keys.contains("VER")) result.info["disc_version"] = keys["VER"];
        if (keys.contains("VMODE")) result.info["video_mode"] = keys["VMODE"];
        if (const auto cover = find_cover(path, serial, context)) result.info["cover"] = path_utf8(display_path(*cover));
        result.status = ProbeStatus::Valid;
        return result;
    } catch (const std::exception& error) {
        return refused(ProbeStatus::Invalid, error.what());
    }
}

ProbeResult probe(ContentKind kind, const std::filesystem::path& path, const ProbeContext& context) {
    return kind == ContentKind::Bios ? probe_bios(path) : probe_disc(path, context);
}

std::vector<std::filesystem::path> find_candidates(const std::filesystem::path& folder, ContentKind kind, std::size_t limit) {
    std::vector<std::filesystem::path> found;
    const auto root = native_io_path(folder);
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error);
    if (error) throw std::runtime_error("Cannot open folder '" + path_utf8(folder) + "': " + error.message());
    for (const std::filesystem::recursive_directory_iterator end; it != end && found.size() < limit; it.increment(error)) {
        if (error) { error.clear(); continue; }
        std::error_code entry_error;
        if (!it->is_regular_file(entry_error) || entry_error) continue;
        const auto& path = it->path();
        if (kind == ContentKind::Disc) {
            if (!is_disc_image_extension(path)) continue;
            // 4 MiB .bin files next to discs are ROM0 dumps, not disc images.
            if (it->file_size(entry_error) == kRom0Bytes) continue;
        } else if (it->file_size(entry_error) != kRom0Bytes || entry_error) {
            continue;
        }
        found.push_back(display_path(path));
    }
    if (kind == ContentKind::Disc) {
        // A CUE sheet stands for its tracks: list the sheet, not each track file.
        std::vector<std::string> tracks;
        for (const auto& candidate : found) {
            if (lower_extension(candidate) != ".cue") continue;
            try {
                for (const auto& file : parse_cue_sheet(candidate).files) tracks.push_back(comparable(file));
            } catch (const std::exception&) {
                // An unreadable sheet stays listed; its probe reports why.
            }
        }
        std::sort(tracks.begin(), tracks.end());
        std::erase_if(found, [&](const std::filesystem::path& candidate) {
            return std::binary_search(tracks.begin(), tracks.end(), comparable(candidate));
        });
    }
    std::sort(found.begin(), found.end());
    return found;
}
} // namespace ps2native::frontend
