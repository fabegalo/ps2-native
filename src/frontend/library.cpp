#include "ps2native/frontend/library.hpp"

#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace ps2native::frontend {
namespace {
constexpr std::string_view kLibraryMagic = "ps2-native library";
constexpr std::string_view kSettingsMagic = "ps2-native settings";
constexpr int kFormatVersion = 1;

template <typename Integer>
Integer parse_integer(std::string_view text, Integer fallback) {
    Integer value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : fallback;
}
std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}
// Checks "<magic> <version>" and returns the remaining lines.
std::vector<std::string_view> body_of(std::string_view text, std::string_view magic) {
    auto lines = split_lines(text);
    auto first = std::find_if(lines.begin(), lines.end(), [](std::string_view l) { return !l.empty(); });
    if (first == lines.end()) return {};
    if (!first->starts_with(magic) || first->size() <= magic.size() + 1 || (*first)[magic.size()] != ' ')
        throw std::runtime_error("Not a " + std::string(magic) + " file");
    const int version = parse_integer(first->substr(magic.size() + 1), -1);
    if (version < 1) throw std::runtime_error("Malformed " + std::string(magic) + " version");
    if (version > kFormatVersion)
        throw std::runtime_error(std::string(magic) + " version " + std::to_string(version) +
                                 " was written by a newer launcher; refusing to overwrite it");
    return {first + 1, lines.end()};
}
std::pair<std::string_view, std::string> key_value(std::string_view line) {
    const auto equals = line.find('=');
    if (equals == std::string_view::npos) return {line, {}};
    return {line.substr(0, equals), unescape_value(line.substr(equals + 1))};
}
void put(std::ostringstream& out, std::string_view key, std::string_view value) {
    out << key << '=' << escape_value(value) << '\n';
}
std::string path_text(const std::filesystem::path& path) { return path_utf8(path); }
} // namespace

const char* compatibility_name(Compatibility value) noexcept {
    switch (value) {
    case Compatibility::Pending: return "pending";
    case Compatibility::Missing: return "missing";
    case Compatibility::InvalidImage: return "invalid-image";
    case Compatibility::UnsupportedFormat: return "unsupported-format";
    case Compatibility::NotPlayStation2: return "not-ps2";
    case Compatibility::RuntimeUnavailable: return "runtime-unavailable";
    case Compatibility::Boots: return "boots";
    case Compatibility::Menus: return "menus";
    case Compatibility::InGame: return "in-game";
    case Compatibility::Playable: return "playable";
    }
    return "unknown";
}

FileStamp stamp_file(const std::filesystem::path& path) {
    FileStamp stamp;
    try {
        const auto native = native_io_path(path);
        std::error_code error;
        const auto status = std::filesystem::status(native, error);
        if (error || !std::filesystem::is_regular_file(status)) return stamp;
        stamp.size = std::filesystem::file_size(native, error);
        if (error) return stamp;
        stamp.modified = static_cast<std::int64_t>(std::filesystem::last_write_time(native, error).time_since_epoch().count());
        if (error) return stamp;
        stamp.exists = true;
    } catch (const std::exception&) {
    }
    return stamp;
}

std::string LibraryEntry::value(const std::string& key) const {
    const auto it = info.find(key);
    return it == info.end() ? std::string() : it->second;
}

Compatibility compatibility_of(const LibraryEntry& entry) noexcept {
    switch (entry.status) {
    case ProbeStatus::Pending: return Compatibility::Pending;
    case ProbeStatus::Missing: return Compatibility::Missing;
    case ProbeStatus::Invalid: return Compatibility::InvalidImage;
    case ProbeStatus::Unsupported: return Compatibility::UnsupportedFormat;
    case ProbeStatus::Valid: break;
    }
    if (entry.kind == ContentKind::Disc && entry.value("platform") != "PlayStation 2") return Compatibility::NotPlayStation2;
    return Compatibility::RuntimeUnavailable;
}

std::filesystem::path normalize_library_path(const std::filesystem::path& path) {
    auto absolute = display_path(native_io_path(path));
    if (!absolute.is_absolute()) absolute = std::filesystem::absolute(absolute);
    return absolute.lexically_normal();
}
std::string library_id_for(const std::filesystem::path& path) {
    auto text = path_utf8(normalize_library_path(path));
#ifdef _WIN32
    // Win32 file names are case-insensitive: one file, one entry.
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return sha256_hex({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}).substr(0, 16);
}

void apply_probe(LibraryEntry& entry, const ProbeResult& result, const FileStamp& stamp) {
    entry.status = result.status;
    entry.error = result.error;
    entry.info = result.info;
    entry.details = result.details;
    entry.stamp = stamp;
    if (entry.title.empty()) entry.title = path_utf8(entry.path.stem());
}
void apply_nvm_probe(LibraryEntry& entry, const ProbeResult& result) {
    entry.nvm_status = result.status;
    entry.nvm_error = result.error;
    entry.nvm_info = result.info;
}
bool needs_probe(const LibraryEntry& entry, const FileStamp& current) noexcept {
    return entry.status == ProbeStatus::Pending || entry.status == ProbeStatus::Missing || !current.exists ||
           current.exists != entry.stamp.exists || current.size != entry.stamp.size ||
           current.modified != entry.stamp.modified;
}

LibraryEntry* Library::find(std::string_view id) noexcept {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const LibraryEntry& e) { return e.id == id; });
    return it == entries_.end() ? nullptr : &*it;
}
const LibraryEntry* Library::find(std::string_view id) const noexcept {
    return const_cast<Library*>(this)->find(id);
}
std::pair<LibraryEntry*, bool> Library::add(ContentKind kind, const std::filesystem::path& path, std::int64_t now_unix) {
    const auto id = library_id_for(path);
    if (auto* existing = find(id)) return {existing, false};
    LibraryEntry entry;
    entry.id = id;
    entry.kind = kind;
    entry.path = normalize_library_path(path);
    entry.title = path_utf8(entry.path.stem());
    entry.added = now_unix;
    entries_.push_back(std::move(entry));
    return {&entries_.back(), true};
}
bool Library::remove(std::string_view id) {
    const auto before = entries_.size();
    std::erase_if(entries_, [&](const LibraryEntry& e) { return e.id == id; });
    return entries_.size() != before;
}
void Library::replace(LibraryEntry entry) {
    if (auto* existing = find(entry.id)) *existing = std::move(entry);
    else entries_.push_back(std::move(entry));
}

std::string Library::serialize() const {
    std::ostringstream out;
    out << kLibraryMagic << ' ' << kFormatVersion << '\n';
    out << "# Local references to user files; nothing listed here is copied or distributed.\n";
    for (const auto& e : entries_) {
        out << "[entry]\n";
        put(out, "id", e.id);
        put(out, "kind", content_kind_name(e.kind));
        put(out, "path", path_text(e.path));
        put(out, "title", e.title);
        put(out, "exists", e.stamp.exists ? "1" : "0");
        put(out, "size", std::to_string(e.stamp.size));
        put(out, "modified", std::to_string(e.stamp.modified));
        put(out, "added", std::to_string(e.added));
        put(out, "favorite", e.favorite ? "1" : "0");
        put(out, "status", probe_status_name(e.status));
        if (!e.error.empty()) put(out, "error", e.error);
        for (const auto& [key, value] : e.info) put(out, "info." + key, value);
        for (const auto& line : e.details) put(out, "detail", line);
        if (!e.console_profile.empty()) put(out, "console_profile", e.console_profile);
        if (!e.nvm_path.empty()) {
            put(out, "nvm_path", path_text(e.nvm_path));
            put(out, "nvm_status", probe_status_name(e.nvm_status));
            if (!e.nvm_error.empty()) put(out, "nvm_error", e.nvm_error);
            for (const auto& [key, value] : e.nvm_info) put(out, "nvm_info." + key, value);
        }
        out << "[end]\n";
    }
    return out.str();
}

Library Library::parse(std::string_view text) {
    Library library;
    std::optional<LibraryEntry> current;
    for (const auto line : body_of(text, kLibraryMagic)) {
        if (line.empty() || line.front() == '#') continue;
        if (line == "[entry]") {
            current.emplace();
            continue;
        }
        if (line == "[end]") {
            if (current && !current->id.empty() && !current->path.empty() && !library.find(current->id))
                library.entries_.push_back(std::move(*current));
            current.reset();
            continue;
        }
        if (!current) continue;
        auto& e = *current;
        const auto [key, value] = key_value(line);
        if (key == "id") e.id = value;
        else if (key == "kind") e.kind = content_kind_from_name(value).value_or(ContentKind::Disc);
        else if (key == "path") e.path = path_from_utf8(value);
        else if (key == "title") e.title = value;
        else if (key == "exists") e.stamp.exists = value == "1";
        else if (key == "size") e.stamp.size = parse_integer<std::uint64_t>(value, 0);
        else if (key == "modified") e.stamp.modified = parse_integer<std::int64_t>(value, 0);
        else if (key == "added") e.added = parse_integer<std::int64_t>(value, 0);
        else if (key == "favorite") e.favorite = value == "1";
        else if (key == "status") e.status = probe_status_from_name(value).value_or(ProbeStatus::Pending);
        else if (key == "error") e.error = value;
        else if (key.starts_with("info.")) e.info[std::string(key.substr(5))] = value;
        else if (key == "detail") e.details.push_back(value);
        else if (key == "console_profile") e.console_profile = value;
        else if (key == "nvm_path") e.nvm_path = path_from_utf8(value);
        else if (key == "nvm_status") e.nvm_status = probe_status_from_name(value).value_or(ProbeStatus::Pending);
        else if (key == "nvm_error") e.nvm_error = value;
        else if (key.starts_with("nvm_info.")) e.nvm_info[std::string(key.substr(9))] = value;
        // Unknown keys (newer minor versions) are ignored.
    }
    return library;
}

Library Library::load(const std::filesystem::path& file) {
    if (!file_exists(file)) return {};
    const auto bytes = read_file(file);
    return parse({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}
void Library::save(const std::filesystem::path& file) const {
    const auto text = serialize();
    write_file_atomically(file, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

std::string LauncherSettings::serialize() const {
    std::ostringstream out;
    out << kSettingsMagic << ' ' << kFormatVersion << '\n';
    for (const auto& folder : game_folders) put(out, "game_folder", path_text(folder));
    put(out, "covers_folder", path_text(covers_folder));
    put(out, "boot_tool", path_text(boot_tool));
    put(out, "active_bios", active_bios);
    put(out, "scan_folders_on_start", scan_folders_on_start ? "1" : "0");
    put(out, "diagnostic_budget", std::to_string(diagnostic_budget));
    put(out, "diagnostic_trace", diagnostic_trace);
    put(out, "renderer", renderer);
    put(out, "reduce_motion", reduce_motion ? "1" : "0");
    put(out, "library_view", library_view);
    put(out, "language", language);
    return out.str();
}
LauncherSettings LauncherSettings::parse(std::string_view text) {
    LauncherSettings settings;
    for (const auto line : body_of(text, kSettingsMagic)) {
        if (line.empty() || line.front() == '#') continue;
        const auto [key, value] = key_value(line);
        if (key == "game_folder" && !value.empty()) settings.game_folders.push_back(path_from_utf8(value));
        else if (key == "covers_folder") settings.covers_folder = path_from_utf8(value);
        else if (key == "boot_tool") settings.boot_tool = path_from_utf8(value);
        else if (key == "active_bios") settings.active_bios = value;
        else if (key == "scan_folders_on_start") settings.scan_folders_on_start = value == "1";
        else if (key == "diagnostic_budget") settings.diagnostic_budget = parse_integer<std::uint64_t>(value, settings.diagnostic_budget);
        else if (key == "diagnostic_trace" && (value == "events" || value == "full")) settings.diagnostic_trace = value;
        else if (key == "renderer" && (value == "gs-faithful" || value == "native")) settings.renderer = value;
        else if (key == "language" && (value == "auto" || value == "en" || value == "pt-BR")) settings.language = value;
        else if (key == "reduce_motion") settings.reduce_motion = value == "1";
        else if (key == "library_view" && (value == "grid" || value == "list")) settings.library_view = value;
    }
    return settings;
}
LauncherSettings LauncherSettings::load(const std::filesystem::path& file) {
    if (!file_exists(file)) return {};
    const auto bytes = read_file(file);
    return parse({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}
void LauncherSettings::save(const std::filesystem::path& file) const {
    const auto text = serialize();
    write_file_atomically(file, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

std::string escape_value(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}
std::string unescape_value(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) { out += text[i]; continue; }
        const char next = text[++i];
        out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
    }
    return out;
}
std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
} // namespace ps2native::frontend
