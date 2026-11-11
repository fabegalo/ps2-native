// ps2n: prepares a PS2 disc with the pinned PS2Recomp tools and runs the
// resulting native executable, which links the PS2Recomp runtime unchanged.
//
//   ps2n prepare <disc.bin|.cue|.iso>   extract, analyze, recompile, build
//   ps2n run <game-id|game-dir>          run; learn missing entry points
//   ps2n play <disc>                     prepare when needed, then run
#include "ps2native/disc.hpp"
#include "ps2native/io.hpp"
#include "ps2native/process.hpp"
#include "ps2n_build_environment.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
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
    }
    if (game.id.empty() || game.elf.empty() || game.cd_image.empty())
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
void recompile(const Game& game) {
    const auto generated = game.dir / "generated";
    ps2native::create_directories(generated);
    run_tool(PS2N_RECOMP_EXE, {"config.toml"}, game.dir, game.dir / "recomp.log");
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

void build(const Game& game) {
    std::cout << "Building " << game.id << "...\n";
    apply_build_environment();
    run_tool(PS2N_CMAKE_COMMAND, {fs::path(PS2N_BINARY_DIR).string()}, PS2N_BINARY_DIR, game.dir / "configure.log");
    run_tool(PS2N_CMAKE_COMMAND, {"--build", fs::path(PS2N_BINARY_DIR).string(), "--target", "ps2n-game-" + game.id},
             PS2N_BINARY_DIR, game.dir / "build.log");
}

fs::path game_executable(const Game& game) {
    return fs::path(PS2N_BINARY_DIR) / from_utf8("ps2n-game-" + game.id + PS2N_EXE_SUFFIX);
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
    const auto elf = game.dir / "cd" / from_utf8(game.elf);
    run_tool(PS2N_ANALYZER_EXE, {path_utf8(elf), "config.toml"}, game.dir, game.dir / "analyzer.log");
    std::istringstream original(read_text(game.dir / "config.toml"));
    std::string config;
    for (std::string line; std::getline(original, line);) {
        if (line.starts_with("input = ")) {
            const auto generic = fs::absolute(elf).generic_u8string();  // forward slashes: no TOML escapes
            line = "input = \"" + std::string(generic.begin(), generic.end()) + "\"";
        }
        else if (line.starts_with("output = ")) line = "output = \"generated/\"";
        config += line + "\n";
    }
    write_text(game.dir / "config.toml", config);
    write_text(game.dir / "game.txt", "id=" + game.id + "\nelf=" + game.elf + "\ncd_image=" + game.cd_image + "\n");

    std::cout << "Recompiling with PS2Recomp...\n";
    recompile(game);
    build(game);
    std::cout << "Prepared " << path_utf8(game_executable(game)) << "\n";
    return game;
}

// PS2Recomp reports a guest branch to code it did not emit as an entry.
std::set<std::string> missing_targets(std::string_view log) {
    constexpr std::string_view marker = "[guest-branch:missing-target]", key = " target=0x";
    std::set<std::string> found;
    for (auto at = log.find(marker); at != log.npos; at = log.find(marker, at + 1)) {
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
        const auto general = config.find("[general]\n");
        if (general == config.npos) throw std::runtime_error("config.toml has no [general] table");
        config.insert(general + 10, "entry_points = [" + added + "]\n");
    }
    write_text(game.dir / "config.toml", config);
    return true;
}

int run(Game game) {
    for (int round = 0; round < kMaxLearningRounds; ++round) {
        const auto log = game.dir / "run.log";
        std::string learned;
        const auto code = run_process(game_executable(game), {game.elf, game.cd_image}, game.dir / "cd", log,
            [&](std::string_view output) {
                // Keep a short overlap so a report split across reads is still seen.
                learned.append(output);
                const bool keep_running = missing_targets(learned).empty();
                if (learned.size() > 4096) learned.erase(0, learned.size() - 4096);
                return keep_running;  // stop the game when an entry is missing
            });
        const auto targets = missing_targets(read_text(log));
        if (targets.empty()) return code;
        std::cout << "PS2Recomp reported " << targets.size() << " missing entry point(s); recompiling\n";
        if (!add_entry_points(game, targets))
            throw std::runtime_error("entry points already declared but still missing; see " + path_utf8(log));
        recompile(game);
        build(game);
    }
    throw std::runtime_error("entry-point learning did not converge");
}

int usage() {
    std::cerr << "Usage: ps2n prepare <disc.bin|.cue|.iso>\n"
                 "       ps2n run <game-id|game-dir>\n"
                 "       ps2n play <disc.bin|.cue|.iso>\n"
                 "Games are kept in " << path_utf8(games_root()) << "\n";
    return 2;
}

template <class C> int main_impl(int argc, C** argv) {
    try {
        if (argc != 3) return usage();
        const auto command = path_utf8(fs::path(argv[1]));
        const fs::path argument(argv[2]);
        if (command == "prepare") return (void)prepare(argument), 0;
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
