// ps2n: prepares a PS2 disc with the pinned PS2Recomp tools and runs the
// resulting native executable, which links the PS2Recomp runtime unchanged.
//
//   ps2n prepare <disc.bin|.cue|.iso>   extract, analyze, recompile, build
//   ps2n run <game-id|game-dir>          run; learn missing entry points
//   ps2n play <disc>                     prepare when needed, then run
#include "ps2native/disc.hpp"
#include "ps2native/io.hpp"
#include "ps2native/process.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/bios.hpp"
#include "ps2n_build_environment.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace ps2native;

namespace {
constexpr int kMaxLearningRounds = 16;

fs::path from_utf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }
fs::path games_root() { return from_utf8(PS2N_GAMES_DIR); }

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path_utf8(path));
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void write_text(const fs::path& path, const std::string& text) {
    write_file_atomically(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

// A CUE sheet names the raw image of its first data track.
fs::path disc_image(const fs::path& input) {
    auto extension = path_utf8(input.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
    if (extension != ".cue") return input;
    const std::regex file_line(R"re(^\s*FILE\s+"([^"]+)")re", std::regex::icase);
    std::istringstream cue(read_text(input));
    for (std::string line; std::getline(cue, line);) {
        std::smatch match;
        if (std::regex_search(line, match, file_line)) return input.parent_path() / from_utf8(match[1].str());
    }
    throw std::runtime_error("CUE sheet names no image file");
}

std::string game_id(std::string boot) {
    boot = boot.substr(boot.find_last_of("\\/") + 1);
    boot = boot.substr(0, boot.find(';'));
    std::string id;
    for (const unsigned char c : boot)
        if (std::isalnum(c)) id += static_cast<char>(std::tolower(c));
    if (id.empty()) throw std::runtime_error("boot executable has no usable name");
    return id;
}

// The PS2Recomp runtime reads cdrom0: files from a host directory and raw
// LBN reads from a 2048-byte-sector image.
void extract(DiscImage& disc, const std::string& directory, const fs::path& target) {
    ps2native::create_directories(target);
    std::array<std::uint8_t, 2048> sector{};
    for (const auto& entry : disc.list(directory)) {
        const auto name = entry.name.substr(0, entry.name.find(';'));
        const auto inner = directory.empty() ? name : directory + "/" + name;
        if (entry.directory) {
            extract(disc, inner, target / from_utf8(name));
            continue;
        }
        std::ofstream out(target / from_utf8(name), std::ios::binary | std::ios::trunc);
        for (std::uint64_t at = 0; at < entry.size; at += 2048u) {
            disc.read_sector(entry.lba + static_cast<std::uint32_t>(at / 2048u), sector);
            out.write(reinterpret_cast<const char*>(sector.data()),
                      static_cast<std::streamsize>(std::min<std::uint64_t>(2048u, entry.size - at)));
        }
        if (!out) throw std::runtime_error("cannot write " + path_utf8(target / from_utf8(name)));
    }
}

void write_user_image(DiscImage& disc, const fs::path& target) {
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    std::array<std::uint8_t, 2048> sector{};
    for (std::uint32_t lba = 0; lba < disc.user_sector_count(); ++lba) {
        disc.read_sector(lba, sector);
        out.write(reinterpret_cast<const char*>(sector.data()), sector.size());
    }
    if (!out) throw std::runtime_error("cannot write " + path_utf8(target));
}

struct Game {
    fs::path dir;
    std::string id, elf, cd_image;
    std::string native_output = "generated";
    std::string rom0, rom0_sha256, console_profile, nvm, nvm_sha256;
    bool browser() const { return !rom0.empty(); }
};

Game load_game(const std::string& selector) {
    Game game;
    game.dir = fs::exists(from_utf8(selector)) ? from_utf8(selector) : games_root() / from_utf8(selector);
    const auto text = read_text(game.dir / "game.txt");
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        const auto equal = line.find('=');
        if (equal == line.npos) continue;
        const auto key = line.substr(0, equal), value = line.substr(equal + 1);
        if (key == "id") game.id = value;
        else if (key == "elf") game.elf = value;
        else if (key == "cd_image") game.cd_image = value;
        else if (key == "rom0") game.rom0 = value;
        else if (key == "rom0_sha256") game.rom0_sha256 = value;
        else if (key == "console_profile") game.console_profile = value;
        else if (key == "nvm") game.nvm = value;
        else if (key == "nvm_sha256") game.nvm_sha256 = value;
    }
    if (game.id.empty() || game.elf.empty() || (!game.browser() && game.cd_image.empty()) ||
        (game.browser() && (game.rom0_sha256.empty() || game.console_profile.empty() || game.nvm.empty() || game.nvm_sha256.empty())))
        throw std::runtime_error("incomplete game.txt in " + path_utf8(game.dir));
    return game;
}

void run_tool(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd, const fs::path& log) {
    const auto code = run_process(exe, args, cwd, log);
    if (code != 0)
        throw std::runtime_error(path_utf8(exe.filename()) + " failed with exit code " + std::to_string(code) +
                                 "; see " + path_utf8(log));
}

// Generated headers sit beside the sources; the per-game CMake target adds
// that directory to its include path. PS2Recomp rewrites only changed files and
// removes stale ones, so the directory is kept for incremental rebuilds.
bool restore_heuristic_stores(const Game& game) {
    const auto path = game.dir / "config.toml";
    const auto original = read_text(path);
    std::istringstream lines(original);
    std::string config;
    int removed = 0;
    for (std::string line; std::getline(lines, line);) {
        if (line.find("# Potential self-modifying code") != line.npos &&
            line.find("value = \"0x0\"") != line.npos) {
            ++removed;
            continue;
        }
        config += line + "\n";
    }
    if (!removed) return false;
    write_text(path, config);
    std::cout << "Restored " << removed << " guest stores replaced by analyzer heuristics\n";
    return true;
}

std::string generation_inputs(const Game& game) {
    const auto selected = sha256_file(game.dir / "cd" / from_utf8(game.elf));
    const auto metadata = game.dir / "generation.inputs";
    std::string result = "ps2n-generation-v1\n" + sha256_file(PS2N_RECOMP_EXE) + "\n" + selected +
        "\n" + sha256_file(game.dir / "config.toml") + "\n" + sha256_file(metadata) + "\n";
    std::istringstream description(read_text(metadata));
    std::string line;
    if (!std::getline(description, line) || line != "ps2x-inputs-v1")
        throw std::runtime_error("unsupported SDK input description");
    unsigned elf = 0, output = 0;
    while (std::getline(description, line)) {
        const auto space = line.find(' ');
        if (space == line.npos || (line.size() - space - 1) % 2)
            throw std::runtime_error("malformed SDK input description");
        const auto nibble = [](char c) -> unsigned {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            throw std::runtime_error("invalid SDK path encoding");
        };
        std::string decoded;
        for (auto at = space + 1; at < line.size(); at += 2)
            decoded += static_cast<char>((nibble(line[at]) << 4) | nibble(line[at + 1]));
        auto path = from_utf8(decoded);
        if (path.is_relative()) path = game.dir / path;
        const auto role = line.substr(0, space);
        if (role == "elf") {
            if (++elf != 1 || sha256_file(path) != selected)
                throw std::runtime_error("SDK input differs from the selected ELF");
        } else if (role == "output") {
            std::error_code error;
            if (++output != 1 || !fs::equivalent(path, game.dir / game.native_output, error) || error)
                throw std::runtime_error("SDK output differs from the native program directory");
        } else if (role == "extra") result += sha256_file(path) + "\n";
        else throw std::runtime_error("unsupported SDK input dependency role");
    }
    if (elf != 1 || output != 1) throw std::runtime_error("incomplete SDK input description");
    return result;
}

std::string generation_outputs(const Game& game) {
    std::vector<fs::path> sources;
    const auto generated = game.dir / game.native_output;
    if (!fs::exists(generated)) return "missing";
    for (const auto& file : fs::directory_iterator(generated))
        if (file.is_regular_file() && (file.path().extension() == ".cpp" || file.path().extension() == ".h"))
            sources.push_back(file.path());
    std::sort(sources.begin(), sources.end());
    if (std::none_of(sources.begin(), sources.end(), [](const auto& path) { return path.extension() == ".cpp"; }))
        return "missing";
    Sha256 digest;
    for (const auto& source : sources) {
        const auto name = path_utf8(source.filename());
        const auto entry = std::to_string(name.size()) + ":" + name + ":" + sha256_file(source) + "\n";
        digest.update(std::span(reinterpret_cast<const std::uint8_t*>(entry.data()), entry.size()));
    }
    return digest.hex_digest();
}

bool generation_current(const Game& game) {
    const auto receipt = game.dir / "generation.sha256";
    if (!fs::exists(receipt)) return false;
    const auto recorded = read_text(receipt), inputs = generation_inputs(game);
    return recorded.starts_with(inputs) && recorded == inputs + generation_outputs(game) + "\n";
}

void recompile(const Game& game) {
    restore_heuristic_stores(game);
    const auto generated = game.dir / game.native_output;
    ps2native::create_directories(generated);
    const auto before = generation_inputs(game);
    run_tool(PS2N_RECOMP_EXE, {"config.toml"}, game.dir, game.dir / "recomp.log");
    const auto outputs = generation_outputs(game);
    if (generation_inputs(game) != before || outputs == "missing")
        throw std::runtime_error("preparation inputs changed or native output is missing; receipt not published");
    write_text(game.dir / "generation.sha256", before + outputs + "\n");
}

// The build tree was configured inside a compiler environment; rebuilds started
// from the launcher or a plain shell must see the same one.
void apply_build_environment() {
    for (const auto& [name, value] : ps2n_build::kEnvironment) {
        if (value.empty()) continue;
#ifdef _WIN32
        _putenv_s(std::string(name).c_str(), std::string(value).c_str());
#else
        setenv(std::string(name).c_str(), std::string(value).c_str(), 1);
#endif
    }
}

bool configured_game(std::string_view cache, std::string_view id) {
    std::istringstream lines{std::string(cache)};
    const auto selected = "PS2N_BUILD_GAME:STRING=" + std::string(id);
    constexpr std::string_view retained = "PS2N_CONFIGURED_GAMES:INTERNAL=";
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == selected) return true;
        if (line.starts_with(retained) &&
            (";" + line.substr(retained.size()) + ";").find(";" + std::string(id) + ";") != std::string::npos)
            return true;
    }
    return false;
}

void build(const Game& game, bool configure) {
    std::cout << "Building " << game.id << "...\n";
    apply_build_environment();
    // Add the selected game without dropping dependency records for previous targets.
    const auto cache = fs::path(PS2N_BINARY_DIR) / "CMakeCache.txt";
    if (configure || !fs::exists(cache) || !configured_game(read_text(cache), game.id))
        run_tool(PS2N_CMAKE_COMMAND, {"-DPS2N_BUILD_GAME=" + game.id, fs::path(PS2N_BINARY_DIR).string()},
                 PS2N_BINARY_DIR, game.dir / "configure.log");
    run_tool(PS2N_CMAKE_COMMAND, {"--build", fs::path(PS2N_BINARY_DIR).string(), "--target", "ps2n-game-" + game.id},
             PS2N_BINARY_DIR, game.dir / "build.log");
}

struct ExecModule {
    Game game;
    std::string config;
    uint32_t base = 0, size = 0;
};

std::optional<ExecModule> exec_module(const Game& game, const BrowserFirmware& firmware) {
    const auto metadata = game.dir / "exec/current.txt";
    if (!fs::exists(metadata)) return std::nullopt;
    if (!firmware.unpacked) throw std::runtime_error("executable receipt has no matching firmware payload");
    std::istringstream input(read_text(metadata));
    std::string line;
    if (!std::getline(input, line) || line != "ps2n-exec-image-v1")
        throw std::runtime_error("unsupported executable receipt");
    std::map<std::string, std::string> fields;
    while (std::getline(input, line)) {
        const auto equal = line.find('=');
        if (equal == line.npos || !fields.emplace(line.substr(0, equal), line.substr(equal + 1)).second)
            throw std::runtime_error("malformed executable receipt");
    }
    if (fields.size() != 5 || !fields.contains("sha256") || !fields.contains("parent_sha256") ||
        !fields.contains("load_address") || !fields.contains("size") || !fields.contains("entry"))
        throw std::runtime_error("incomplete executable receipt");
    const auto& image = *firmware.unpacked;
    const auto identity = sha256_hex(image.bytes);
    if (fields.at("sha256") != identity || fields.at("parent_sha256") != firmware.bios.sha256 ||
        game.rom0_sha256 != firmware.bios.sha256)
        throw std::runtime_error("executable receipt belongs to different firmware content");
    const auto number = [&](const char* name) {
        const auto& value = fields.at(name);
        size_t used = 0;
        const auto result = std::stoull(value, &used, 10);
        if (used != value.size() || result > UINT32_MAX || std::to_string(result) != value)
            throw std::runtime_error("invalid executable receipt number");
        return static_cast<uint32_t>(result);
    };
    const auto base = number("load_address"), size = number("size"), entry = number("entry");
    if (base != image.load_address || size != image.bytes.size() || size < 4 || (entry & 3u) ||
        entry < base || uint64_t(entry) + 4 > uint64_t(base) + size)
        throw std::runtime_error("executable receipt range differs from its firmware payload");
    const auto binary = fs::absolute(game.dir / "exec" / (identity + ".bin"));
    if (sha256_file(binary) != identity) throw std::runtime_error("prepared executable image changed");
    ExecModule module;
    module.game.id = identity;
    module.game.dir = fs::path(PS2N_BINARY_DIR) / "ee-aot" / identity;
    module.game.elf = path_utf8(binary);
    module.game.native_output = "g";
    module.base = base; module.size = size;
    const auto generic = binary.generic_u8string();
    std::string native_input;
    for (const char byte : std::string(generic.begin(), generic.end())) {
        if (byte == '"' || byte == '\\') native_input += '\\';
        if (static_cast<unsigned char>(byte) < 0x20) throw std::runtime_error("control character in native input path");
        native_input += byte;
    }
    module.config = "[general]\ninput = \"" + native_input + "\"\noutput = \"g\"\nmodule_id = \"" +
        identity + "\"\noutput_worker_threads = 8\n[raw_image]\nload_address = " + std::to_string(base) +
        "\nentry = " + std::to_string(entry) + "\n";
    return module;
}

void ensure_prepared(const Game& game, bool configure = false) {
    if (game.browser()) {
        const auto firmware = load_browser_firmware(read_file(from_utf8(game.rom0)), game.console_profile,
            read_file(from_utf8(game.nvm)), {game.rom0_sha256}, {game.nvm_sha256});
        if (sha256_file(game.dir / "cd" / from_utf8(game.elf)) != sha256_hex(firmware.program.bytes))
            throw std::runtime_error("Prepared OSDSYS differs from the selected ROM0");
    }
    apply_build_environment();
    run_tool(PS2N_CMAKE_COMMAND, {"--build", fs::path(PS2N_BINARY_DIR).string(), "--target", "ps2recomp_tools"},
             PS2N_BINARY_DIR, game.dir / "build.log");
    if (game.browser()) {
        const auto firmware = load_browser_firmware(read_file(from_utf8(game.rom0)), game.console_profile,
            read_file(from_utf8(game.nvm)), {game.rom0_sha256}, {game.nvm_sha256});
        if (auto module = exec_module(game, firmware)) {
            ps2native::create_directories(module->game.dir / module->game.native_output);
            if (!fs::exists(module->game.dir / "config.toml")) write_text(module->game.dir / "config.toml", module->config);
            run_tool(PS2N_RECOMP_EXE, {"config.toml", "--describe-inputs", "generation.inputs"},
                module->game.dir, module->game.dir / "recomp.log");
            if (!generation_current(module->game)) recompile(module->game);
        }
    }
    restore_heuristic_stores(game);
    ps2native::create_directories(game.dir / "generated");
    run_tool(PS2N_RECOMP_EXE, {"config.toml", "--describe-inputs", "generation.inputs"},
             game.dir, game.dir / "recomp.log");
    if (!generation_current(game)) recompile(game);
    build(game, configure);
}

fs::path game_executable(const Game& game) {
    return fs::path(PS2N_BINARY_DIR) / from_utf8("ps2n-game-" + game.id + PS2N_EXE_SUFFIX);
}

void analyze(const Game& game) {
    const auto elf = game.dir / "cd" / from_utf8(game.elf);
    run_tool(PS2N_ANALYZER_EXE, {path_utf8(elf), "config.toml"}, game.dir, game.dir / "analyzer.log");
    std::istringstream original(read_text(game.dir / "config.toml"));
    std::string config;
    for (std::string line; std::getline(original, line);) {
        if (line.starts_with("input = ")) {
            const auto generic = fs::absolute(elf).generic_u8string();
            line = "input = \"" + std::string(generic.begin(), generic.end()) + "\"";
        }
        else if (line.starts_with("output = ")) line = "output = \"generated/\"";
        config += line + "\n";
    }
    write_text(game.dir / "config.toml", config);
}

Game prepare_browser(const fs::path& rom0, const std::string& profile, const fs::path& nvm) {
    const auto firmware = load_browser_firmware(read_file(rom0), profile, read_file(nvm));
    Game game;
    game.id = "bios-" + firmware.bios.sha256;
    game.dir = games_root() / game.id;
    game.elf = "OSDSYS";
    game.rom0 = path_utf8(fs::absolute(rom0)); game.rom0_sha256 = firmware.bios.sha256;
    game.console_profile = profile;
    game.nvm = path_utf8(fs::absolute(nvm)); game.nvm_sha256 = firmware.nvm.sha256;
    ps2native::create_directories(game.dir / "cd");
    const auto elf = game.dir / "cd" / game.elf;
    if (!fs::exists(elf) || sha256_file(elf) != sha256_hex(firmware.program.bytes))
        write_file_atomically(elf, firmware.program.bytes);
    // Keep learned entries and unchanged native objects for this exact ROM0.
    if (!fs::exists(game.dir / "config.toml")) analyze(game);
    write_text(game.dir / "game.txt", "id=" + game.id + "\nelf=" + game.elf + "\nrom0=" + game.rom0 +
        "\nrom0_sha256=" + game.rom0_sha256 + "\nconsole_profile=" + profile +
        "\nnvm=" + game.nvm + "\nnvm_sha256=" + game.nvm_sha256 + "\n");
    ensure_prepared(game);
    return game;
}

Game prepare(const fs::path& input) {
    const auto image = disc_image(input);
    DiscImage disc(image);
    const auto boot = disc.boot_path();
    Game game;
    game.id = game_id(boot);
    game.dir = games_root() / game.id;
    game.elf = boot.substr(0, boot.find(';'));
    std::replace(game.elf.begin(), game.elf.end(), '\\', '/');
    if (!game.elf.empty() && game.elf.front() == '/') game.elf.erase(0, 1);
    std::cout << "Preparing " << disc.volume_id() << " (" << game.id << ") in " << path_utf8(game.dir) << "\n";
    ps2native::create_directories(game.dir);

    std::cout << "Extracting disc files...\n";
    if (fs::exists(game.dir / "cd")) remove_directory_tree(game.dir / "cd");
    extract(disc, {}, game.dir / "cd");
    if (!disc.raw_sectors()) {
        game.cd_image = path_utf8(fs::absolute(image));  // already user sectors: no copy
    } else {
        write_user_image(disc, game.dir / "disc.iso");
        game.cd_image = path_utf8(game.dir / "disc.iso");
    }

    std::cout << "Analyzing " << game.elf << "...\n";
    analyze(game);
    write_text(game.dir / "game.txt", "id=" + game.id + "\nelf=" + game.elf + "\ncd_image=" + game.cd_image + "\n");

    std::cout << "Recompiling with PS2Recomp...\n";
    ensure_prepared(game, true);
    std::cout << "Prepared " << path_utf8(game_executable(game)) << "\n";
    return game;
}

// PS2Recomp reports a guest branch to code it did not emit as an entry.
std::set<std::string> missing_targets(std::string_view log) {
    constexpr std::string_view marker = "[guest-branch:missing-target]", key = " target=0x";
    std::set<std::string> found;
    for (auto at = log.find(marker); at != log.npos; at = log.find(marker, at + 1)) {
        const auto line_end = log.find('\n', at);
        const auto report = log.substr(at, line_end == log.npos ? log.npos : line_end - at);
        if (report.find(" codeRegion=no") != report.npos)
            throw std::runtime_error("guest jumped outside registered code; refusing to recompile data as an entry point");
        const auto target = log.find(key, at);
        if (target == log.npos) break;
        auto end = target + key.size();
        while (end < log.size() && std::isxdigit(static_cast<unsigned char>(log[end]))) ++end;
        if (end > target + key.size()) found.insert(std::string(log.substr(target + 8, end - target - 8)));
    }
    return found;
}

bool add_entry_points(const Game& game, const std::set<std::string>& targets) {
    auto config = read_text(game.dir / "config.toml");
    std::string added;
    for (const auto& target : targets) {
        const auto item = "\"entry@" + target + "\"";
        if (config.find(item) != config.npos) continue;
        added += item + ", ";
    }
    if (added.empty()) return false;
    if (const auto existing = config.find("\nentry_points = ["); existing != config.npos) config.insert(existing + 17, added);
    else {
        added.resize(added.size() - 2);
        auto general = config.find("[general]\n");
        std::string newline = "\n";
        if (general == config.npos) {
            general = config.find("[general]\r\n");
            newline = "\r\n";
        }
        if (general == config.npos) throw std::runtime_error("config.toml has no [general] table");
        config.insert(general + 9 + newline.size(), "entry_points = [" + added + "]" + newline);
    }
    write_text(game.dir / "config.toml", config);
    return true;
}

int run(Game game) {
    ensure_prepared(game);
    std::optional<ExecModule> module;
    std::optional<BrowserFirmware> firmware;
    if (game.browser()) {
        firmware = load_browser_firmware(read_file(from_utf8(game.rom0)), game.console_profile,
            read_file(from_utf8(game.nvm)), {game.rom0_sha256}, {game.nvm_sha256});
        module = exec_module(game, *firmware);
    }
    for (int round = 0; round < kMaxLearningRounds; ++round) {
        const auto log = game.dir / "run.log";
        std::string learned;
        auto arguments = game.browser() ? std::vector<std::string>{game.elf, "--bios", game.rom0,
            game.rom0_sha256, game.console_profile, game.nvm, game.nvm_sha256} :
            std::vector<std::string>{game.elf, game.cd_image};
        if (game.browser() && !module) arguments.emplace_back("--prepare-exec");
        const auto code = run_process(game_executable(game), arguments, game.dir / "cd", log,
            [&](std::string_view output) {
                // Keep a short overlap so a report split across reads is still seen.
                learned.append(output);
                const auto report = learned.find("[guest-branch:missing-target]");
                const bool keep_running = report == learned.npos || learned.find('\n', report) == learned.npos;
                if (learned.size() > 4096) learned.erase(0, learned.size() - 4096);
                return keep_running;  // stop the game when an entry is missing
            });
        if (firmware && !module && code == 0 && fs::exists(game.dir / "exec/current.txt")) {
            module = exec_module(game, *firmware);
            std::cout << "Original EE payload verified; preparing its native module" << std::endl;
            ensure_prepared(game);
            continue;
        }
        const auto targets = missing_targets(read_text(log));
        if (targets.empty()) return code;
        std::cout << "PS2Recomp reported " << targets.size() << " missing entry point(s); recompiling\n";
        std::set<std::string> module_targets, initial_targets;
        for (const auto& target : targets) {
            const auto address = std::stoull(target, nullptr, 0);
            if (module && address >= module->base && address < uint64_t(module->base) + module->size)
                module_targets.insert(target);
            else initial_targets.insert(target);
        }
        const auto learn = [&](const Game& owner, const std::set<std::string>& entries) {
            if (entries.empty()) return;
            if (!add_entry_points(owner, entries))
                throw std::runtime_error("entry points already declared but still missing; see " + path_utf8(log));
            recompile(owner);
        };
        learn(game, initial_targets);
        if (module) learn(module->game, module_targets);
        build(game, false);
    }
    throw std::runtime_error("entry-point learning did not converge");
}

int usage() {
    std::cerr << "Usage: ps2n prepare <disc.bin|.cue|.iso>\n"
                 "       ps2n prepare-bios <ROM0> <console-profile> <NVM>\n"
                 "       ps2n bios <ROM0> <console-profile> <NVM>\n"
                 "       ps2n build <game-id|game-dir>    reuse content; update native artifacts\n"
                 "       ps2n run <game-id|game-dir>\n"
                 "       ps2n play <disc.bin|.cue|.iso>\n"
                 "Games are kept in " << path_utf8(games_root()) << "\n";
    return 2;
}

template <class C> int main_impl(int argc, C** argv) {
    try {
        if (argc != 3 && argc != 5) return usage();
        const auto command = path_utf8(fs::path(argv[1]));
        const fs::path argument(argv[2]);
        if (argc == 5) {
            if (command != "bios" && command != "prepare-bios") return usage();
            const auto game = prepare_browser(argument, path_utf8(fs::path(argv[3])), fs::path(argv[4]));
            return command == "bios" ? run(game) : 0;
        }
        if (command == "prepare") return (void)prepare(argument), 0;
        if (command == "build") return (void)ensure_prepared(load_game(path_utf8(argument))), 0;
        if (command == "run") return run(load_game(path_utf8(argument)));
        if (command == "play") {
            const auto image = disc_image(argument);
            DiscImage disc(image);
            const auto id = game_id(disc.boot_path());
            const auto existing = games_root() / id;
            const bool ready = fs::exists(existing / "game.txt") && fs::exists(game_executable(load_game(path_utf8(existing))));
            return run(ready ? load_game(path_utf8(existing)) : prepare(argument));
        }
        return usage();
    } catch (const std::exception& error) {
        std::cerr << "ps2n: " << error.what() << "\n";
        return 1;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return main_impl(argc, argv); }
#else
int main(int argc, char** argv) { return main_impl(argc, argv); }
#endif
