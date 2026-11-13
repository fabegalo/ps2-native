#include <filesystem>
static std::filesystem::path testCompiler;
#define PS2N_RECOMP_EXE testCompiler
#define PS2N_ANALYZER_EXE "unused-analyzer"
#define PS2N_CMAKE_COMMAND "unused-cmake"
#define PS2N_BINARY_DIR "."
#define PS2N_GAMES_DIR "."
#define PS2N_EXE_SUFFIX ""
#define main unused_ps2n_main
#define wmain unused_ps2n_wmain
#include "../tools/ps2n.cpp"
#undef main
#undef wmain

int main(int argc, char** argv) {
    fs::path owned;
    try {
        if (argc != 2) throw std::runtime_error("owned scratch directory required");
        const auto scratch = fs::absolute(argv[1]);
        ps2native::create_directories(scratch);
        owned = scratch / ("receipt-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!fs::create_directory(owned)) throw std::runtime_error("fixture already exists");
        Game game;
        game.dir = owned; game.elf = "content.elf";
        ps2native::create_directories(owned / "cd");
        ps2native::create_directories(owned / "generated");
        testCompiler = owned / "compiler.bin";
        write_text(testCompiler, "compiler-v1");
        write_text(owned / "cd/content.elf", "guest-v1");
        write_text(owned / "config.toml", "config-v1");
        write_text(owned / "generated/module.cpp", "native-v1");
        write_text(owned / "generated/module.h", "declarations-v1");
        auto field = [](const std::string& role, const std::string& path) {
            static constexpr char digits[] = "0123456789abcdef";
            auto value = role + " ";
            for (const unsigned char byte : path) {
                value += digits[byte >> 4]; value += digits[byte & 15];
            }
            return value + "\n";
        };
        const auto metadata = std::string("ps2x-inputs-v1\n") +
            field("elf", "cd/content.elf") + field("output", "generated");
        write_text(owned / "generation.inputs", metadata);
        auto require = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
        require(configured_game("PS2N_BUILD_GAME:STRING=game\r\n", "game"), "CRLF cache forced reconfiguration");
        require(configured_game("PS2N_BUILD_GAME:STRING=game\n", "game"), "LF cache forced reconfiguration");
        require(configured_game("PS2N_CONFIGURED_GAMES:INTERNAL=other;game;last\r\n", "game"), "Retained game forced reconfiguration");
        require(!configured_game("PS2N_CONFIGURED_GAMES:INTERNAL=game-extra;other\r\n", "game"), "Game prefix was mistaken for the requested target");
        auto record = [&] { write_text(owned / "generation.sha256", generation_inputs(game) + generation_outputs(game) + "\n"); };
        require(!generation_current(game), "Unverified generation was reused");
        record();
        require(generation_current(game), "Unchanged generation was invalidated");
        auto change = [&](const fs::path& file, const std::string& replacement) {
            const auto before = read_text(file);
            const auto time = fs::last_write_time(file);
            write_text(file, replacement);
            fs::last_write_time(file, time);
            require(!generation_current(game), "Changed content with preserved mtime was reused");
            write_text(file, before);
            fs::last_write_time(file, time);
            require(generation_current(game), "Restored generation did not match its receipt");
        };
        change(testCompiler, "compiler-v2");
        change(owned / "cd/content.elf", "guest-v2");
        change(owned / "config.toml", "config-v2");
        change(owned / "generated/module.cpp", "native-v2");
        change(owned / "generated/module.h", "declarations-v2");
        write_text(owned / "generated/report.txt", "not a compilation input");
        require(generation_current(game), "A diagnostic report invalidated native objects");
        write_text(owned / "generation.inputs", metadata + field("extra", "generated/report.txt"));
        require(!generation_current(game), "Changed SDK dependency list was reused");
        record();
        change(owned / "generated/report.txt", "changed compilation dependency");
        ps2native::remove_file(owned / "generated/module.h");
        require(!generation_current(game), "Missing generated header was ignored");
        BrowserFirmware firmware;
        firmware.bios.sha256 = std::string(64, 'a');
        firmware.unpacked = UnpackedEeImage{0x200000, {8, 0, 0xe0, 3, 0, 0, 0, 0}};
        Game browser = game;
        browser.rom0_sha256 = firmware.bios.sha256;
        require(!exec_module(browser, firmware), "Missing executable receipt invented a module");
        ps2native::create_directories(owned / "exec");
        const auto identity = sha256_hex(firmware.unpacked->bytes);
        const auto binary = owned / "exec" / (identity + ".bin");
        write_file_atomically(binary, firmware.unpacked->bytes);
        const auto receipt = "ps2n-exec-image-v1\nsha256=" + identity + "\nparent_sha256=" + firmware.bios.sha256 +
            "\nload_address=2097152\nsize=8\nentry=2097152\n";
        write_text(owned / "exec/current.txt", receipt);
        const auto prepared = exec_module(browser, firmware);
        require(prepared && prepared->game.native_output == "g" &&
                from_utf8(prepared->game.elf) == fs::absolute(binary), "EE module copied or changed its source view");
        auto reject_receipt = [&](std::string text) {
            write_text(owned / "exec/current.txt", text);
            bool rejected = false;
            try { (void)exec_module(browser, firmware); } catch (const std::exception&) { rejected = true; }
            require(rejected, "Invalid executable receipt was accepted");
        };
        reject_receipt(receipt + "entry=2097152\n");
        reject_receipt(receipt + "unknown=value\n");
        auto edited = receipt;
        edited.replace(edited.find("size=8"), 6, "size=7"); reject_receipt(edited);
        edited = receipt;
        edited.replace(edited.find("entry=2097152"), 13, "entry=2097154"); reject_receipt(edited);
        edited = receipt;
        edited.replace(edited.find("parent_sha256=") + 14, 1, "b"); reject_receipt(edited);
        write_text(owned / "exec/current.txt", receipt);
        write_text(binary, "changed!");
        bool rejected = false;
        try { (void)exec_module(browser, firmware); } catch (const std::exception&) { rejected = true; }
        require(rejected, "Changed EE image was reused");
        write_file_atomically(binary, firmware.unpacked->bytes);
        auto module = prepared->game;
        module.dir = owned;
        ps2native::create_directories(owned / "g");
        write_text(owned / "g/module.cpp", "native-module-v1");
        write_text(owned / "generation.inputs", "ps2x-inputs-v1\n" +
            field("elf", module.elf) + field("output", "g"));
        require(!generation_inputs(module).empty() && generation_outputs(module) != "missing",
                "EE module did not reuse the verified preparation receipt path");
        for (const std::string newline : {"\n", "\r\n"}) {
            const auto config = "[general]" + newline + "input = \"image.bin\"" + newline +
                "[raw_image]" + newline + "load_address = 2097152" + newline;
            write_text(owned / "config.toml", config);
            require(add_entry_points(module, {"0x270958"}), "EE module learning rejected a valid table newline");
            auto changed = read_text(owned / "config.toml");
            require(changed.find("entry_points = [\"entry@0x270958\"]" + newline) < changed.find("[raw_image]"),
                    "Entry was added to the raw image table instead of general");
            require(add_entry_points(module, {"0x270970"}), "Existing EE entry list could not grow");
            const auto timestamp = fs::last_write_time(owned / "config.toml");
            changed = read_text(owned / "config.toml");
            require(!add_entry_points(module, {"0x270970"}) && read_text(owned / "config.toml") == changed &&
                    fs::last_write_time(owned / "config.toml") == timestamp, "Unchanged entry learning invalidated preparation");
        }
        require(fs::canonical(owned).parent_path() == fs::canonical(scratch), "Fixture escaped scratch");
        ps2native::remove_directory_tree(owned);
        std::cout << "PASS preparation identity, SDK/config/content changes, output tampering and cache reuse\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
