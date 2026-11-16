// ps2n: prepares a PS2 disc with the engine in engine/ (exoteric33/ps2-native-recomp)
// and runs the resulting native program with the console state shared by every game.
//
//   ps2n prepare <disc.bin|.cue|.iso>   translate, explore, compile vector programs, build
//   ps2n build <game-id>                 compile what play recorded (new VU programs, code)
//   ps2n run <game-id>                   play in a window
//   ps2n play <disc>                     prepare or update when needed, then run
#include "ps2native/bios.hpp"
#include "ps2native/disc.hpp"
#include "ps2native/io.hpp"
#include "ps2native/process.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ps2native;

namespace {
fs::path from_utf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }
fs::path engine_root() { return from_utf8(PS2N_ENGINE_DIR); }
fs::path work_root() { return from_utf8(PS2N_ENGINE_WORK); }
fs::path game_dir(const std::string& id) { return work_root() / "games" / from_utf8(id); }
fs::path runner(const std::string& id) {
    return work_root() / "build" / from_utf8("native-" + id) / from_utf8(std::string("ps2run") + PS2N_EXE_SUFFIX);
}
fs::path console_root() { return console_state_root(work_root().parent_path() / "console"); }
// bash and the engine's programs take forward slashes on every OS
std::string arg(const fs::path& path) {
    const auto text = fs::absolute(path).lexically_normal().generic_u8string();
    return {text.begin(), text.end()};
}

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

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    _wputenv_s(from_utf8(name).wstring().c_str(), from_utf8(value).wstring().c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
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

std::string disc_id(const fs::path& input) {
    DiscImage disc(disc_image(input));
    return game_id(disc.boot_path());
}

fs::path console_firmware_path() { return console_root() / "firmware.txt"; }

// The console's ROM0/profile/NVM choice. The engine does not boot it yet (OSDSYS is a later milestone).
void select_console_firmware(const fs::path& rom0, const std::string& profile, const fs::path& nvm) {
    const auto firmware = load_browser_firmware(read_file(rom0), profile, read_file(nvm));
    ps2native::create_directories(console_firmware_path().parent_path());
    write_text(console_firmware_path(), "rom0=" + path_utf8(fs::absolute(rom0)) + "\nrom0_sha256=" + firmware.bios.sha256 +
        "\nconsole_profile=" + profile + "\nnvm=" + path_utf8(fs::absolute(nvm)) + "\nnvm_sha256=" + firmware.nvm.sha256 + "\n");
}

// The engine's build tools: its portable set on Windows (downloaded once and hash-checked by
// engine/scripts/portable.ps1), the system's bash, clang, cmake, ninja and python elsewhere.
fs::path bash() {
#ifdef _WIN32
    const auto tools = engine_root() / "tools" / "portable";
    const auto shell = tools / "mingit" / "usr" / "bin" / "bash.exe";
    const auto system = from_utf8(env("SystemRoot").empty() ? "C:\\Windows" : env("SystemRoot")) / "System32";
    if (!fs::exists(shell)) {
        std::cout << "Downloading the build tools (about 300 MB, once)..." << std::endl;
        ps2native::create_directories(work_root());
        const auto log = work_root() / "tools.log";
        const auto code = run_process(system / "WindowsPowerShell" / "v1.0" / "powershell.exe",
            {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", path_utf8(engine_root() / "scripts" / "portable.ps1")},
            engine_root(), log, [](std::string_view text) { std::cout << text << std::flush; return true; });
        if (code != 0 || !fs::exists(shell)) throw std::runtime_error("downloading the build tools failed; see " + path_utf8(log));
    }
    // Only the portable tools and System32 on the PATH, as engine/tools/portable/env.ps1 sets it.
    std::string path;
    for (const char* dir : {"llvm-mingw/bin", "cmake/bin", "ninja", "python", "mingit/cmd", "mingit/usr/bin", "busybox"})
        path += path_utf8(tools / dir) + ";";
    set_env("PATH", path + path_utf8(system));
    return shell;
#else
    return "/bin/bash";
#endif
}

// port.sh does the whole preparation; its output is the progress the launcher shows.
void port(const std::string& id, const fs::path& input, bool explore) {
    const auto shell = bash();
    set_env("PS2_WORK", arg(work_root()));
    std::vector<std::string> arguments{"scripts/port.sh", id, arg(input), "--native-only"};
    if (!explore) arguments.emplace_back("--no-explore");
    ps2native::create_directories(game_dir(id));
    const auto log = work_root() / from_utf8(id + "-prepare.log");
    const auto code = run_process(shell, arguments, engine_root(), log,
        [](std::string_view text) { std::cout << text << std::flush; return true; });
    if (code != 0 || !fs::exists(runner(id)))
        throw std::runtime_error("preparing " + id + " failed (exit code " + std::to_string(code) + "); see " + path_utf8(log));
    write_text(game_dir(id) / "ps2n.txt", "disc=" + path_utf8(fs::absolute(input)) + "\n");
}

std::string recorded_disc(const std::string& id) {
    std::istringstream lines(read_text(game_dir(id) / "ps2n.txt"));
    for (std::string line; std::getline(lines, line);)
        if (line.starts_with("disc=")) return line.substr(5);
    throw std::runtime_error("game " + id + " has no recorded disc; prepare it again");
}

// Play records the vector programs the build lacks into games/<id>/vu0 and vu1; a recording newer
// than the program means the next start compiles them first.
bool recordings_newer(const std::string& id) {
    std::error_code error;
    const auto built = fs::last_write_time(runner(id), error);
    if (error) return true;
    for (const char* unit : {"vu0", "vu1"}) {
        const auto dir = game_dir(id) / unit;
        if (!fs::is_directory(dir, error)) continue;
        for (const auto& file : fs::directory_iterator(dir, error))
            if (file.is_regular_file() && file.last_write_time() > built) return true;
    }
    return false;
}

int run(const std::string& id) {
    const auto dir = game_dir(id), console = console_root();
    const auto iso = fs::exists(dir / "disc.iso") ? dir / "disc.iso" : from_utf8(recorded_disc(id));
    ps2native::create_directories(console / "mc0");
    ps2native::create_directories(console / "quick" / from_utf8(id));
    const auto log = dir / "run.log";
    std::cout << "Starting " << id << " (log: " << path_utf8(log) << ")" << std::endl;
    // One console state: slot 1 holds the console's card, the same file for every game.
    return run_process(runner(id), {"--disc", arg(iso), "--window", "--game-dir", arg(dir),
        "--memcard", arg(console / "mc0" / ".ps2n-card.raw"), "--quick-dir", arg(console / "quick" / from_utf8(id))},
        dir, log);
}

int play(const fs::path& input) {
    const auto id = disc_id(input);
    if (!fs::exists(runner(id))) port(id, input, true);
    else if (recordings_newer(id)) port(id, from_utf8(recorded_disc(id)), false);
    return run(id);
}

int usage() {
    std::cerr << "Usage: ps2n prepare <disc.bin|.cue|.iso>\n"
                 "       ps2n build <game-id>      compile what play recorded\n"
                 "       ps2n run <game-id>\n"
                 "       ps2n play <disc.bin|.cue|.iso>\n"
                 "       ps2n use-bios <ROM0> <console-profile> <NVM>\n"
                 "prepare/play also accept --bios <ROM0> <console-profile> <NVM>\n"
                 "Games are kept in " << path_utf8(work_root() / "games") << "\n";
    return 2;
}

template <class C> int main_impl(int argc, C** argv) {
    try {
        if (argc == 7 && path_utf8(fs::path(argv[3])) == "--bios") {
            select_console_firmware(fs::path(argv[4]), path_utf8(fs::path(argv[5])), fs::path(argv[6]));
            argc = 3;
        }
        if (argc != 3 && argc != 5) return usage();
        const auto command = path_utf8(fs::path(argv[1]));
        const fs::path argument(argv[2]);
        if (argc == 5) {
            if (command == "use-bios") {
                select_console_firmware(argument, path_utf8(fs::path(argv[3])), fs::path(argv[4]));
                return 0;
            }
            if (command == "bios" || command == "prepare-bios")
                throw std::runtime_error("starting the original BIOS (OSDSYS) is not implemented on the engine yet");
            return usage();
        }
        if (command == "prepare") { port(disc_id(argument), argument, true); return 0; }
        if (command == "build") {
            const auto id = path_utf8(argument);
            port(id, from_utf8(recorded_disc(id)), false);
            return 0;
        }
        if (command == "run") return run(path_utf8(argument));
        if (command == "play") return play(argument);
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
