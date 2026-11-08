#include "ps2native/disc.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "ps2recomp/config_manager.h"
#include "ps2recomp/elf_analyzer.h"
#include "ps2recomp/ps2_recompiler.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace ps2native::dev {
int prepare_ps2recomp(const std::filesystem::path& source, const std::filesystem::path& destination, bool disc) {
    namespace fs = std::filesystem;
    using nlohmann::json;
    const auto input = fs::weakly_canonical(fs::absolute(source));
    const auto output = fs::weakly_canonical(fs::absolute(destination));
    const auto input_hash = sha256_file(input);
    const auto image = disc ? DiscImage(input).boot_elf() : parse_ee_elf(read_file(input));
    constexpr const char* required[] = {"ps2_recompiled_functions.cpp", "ps2_recompiled_functions.h", "ps2_recompiled_stubs.h", "register_functions.cpp"};
    const auto receipt_path = output / "sdk-prepare.json";
    if (fs::exists(native_io_path(output))) {
        const auto bytes = read_file(receipt_path);
        const auto receipt = json::parse(bytes.begin(), bytes.end());
        if (receipt.at("schema") != "ps2n-ps2recomp-prepare-v1" || receipt.at("input_sha256") != input_hash ||
            receipt.at("sdk_lock_sha256") != PS2N_PS2RECOMP_LOCK_SHA256 || receipt.at("disc") != disc ||
            receipt.at("input") != path_utf8(input) || receipt.at("files").empty())
            throw std::runtime_error("PS2Recomp output identity differs; preserve it and select a different output directory");
        for (const auto* name : required)
            if (!receipt.at("files").contains(path_utf8(fs::path("generated") / name)))
                throw std::runtime_error("PS2Recomp receipt is incomplete");
        for (const auto& [name, hash] : receipt.at("files").items()) {
            const auto path = fs::u8path(name);
            if (path.empty() || path.is_absolute()) throw std::runtime_error("Invalid SDK receipt path");
            for (const auto& part : path) if (part == "..") throw std::runtime_error("Invalid SDK receipt path");
            if (sha256_file(output / path) != hash.get<std::string>())
                throw std::runtime_error("PS2Recomp output changed: " + name);
        }
        std::cout << "PS2Recomp translation reused; runtime integration remains pending\n";
        return 0;
    }
    fs::create_directories(native_io_path(output.parent_path()));
    if (!fs::create_directory(native_io_path(output))) throw std::runtime_error("PS2Recomp output is already owned");
    try {
        const auto elf = disc ? output / "boot.elf" : input;
        if (disc) (void)write_binary_if_changed(elf, image.bytes);
        ps2recomp::ElfAnalyzer analyzer(path_utf8(native_io_path(elf)));
        if (!analyzer.analyze()) throw std::runtime_error("PS2Recomp analysis failed");

        ps2recomp::RecompilerConfig config;
        config.inputPath = path_utf8(native_io_path(elf));
        config.outputPath = path_utf8(native_io_path(output / "generated"));
        config.singleFileOutput = true;
        config.outputWorkerThreads = 1;
        config.patchSyscalls = config.patchCop0 = config.patchCache = false;
        // Analyzer heuristics are evidence, never permission to skip/stub/patch guest code.
        for (const auto& function : analyzer.getFunctions()) {
            (void)image.instruction_at(function.start);
            std::ostringstream address;
            address << "0x" << std::hex << function.start;
            config.entryPointHints.push_back(address.str());
        }
        const auto config_path = path_utf8(native_io_path(output / "recompiler.toml"));
        ps2recomp::ConfigManager(config_path).saveConfig(config);
        ps2recomp::PS2Recompiler recompiler(config_path);
        if (!recompiler.initialize() || !recompiler.recompile()) {
            recompiler.printReport();
            throw std::runtime_error("PS2Recomp translation failed");
        }
        const auto& counters = recompiler.reportCounters();
        const auto require_complete = [&] {
            if (recompiler.hasErrors() || counters.decodeFailures || counters.functionsStubbed || counters.functionsSkipped ||
                counters.unhandledInstructions || counters.correctnessCriticalFailures || !counters.functionsRecompiled)
            {
                recompiler.printReport();
                throw std::runtime_error("PS2Recomp translation incomplete: decode failure, unsupported instruction, skipped function or stub");
            }
        };
        require_complete();
        recompiler.generateOutput();
        require_complete();
        if (!counters.generatedFunctions) throw std::runtime_error("PS2Recomp produced no functions");
        // Required SDK outputs must be present; generateOutput() has a void API.
        for (const auto* name : required)
            if (!regular_file_size(output / "generated" / name)) throw std::runtime_error("PS2Recomp output is empty");
        if (sha256_file(input) != input_hash) throw std::runtime_error("Input changed during PS2Recomp preparation");
        json receipt{{"schema", "ps2n-ps2recomp-prepare-v1"}, {"sdk_revision", PS2N_PS2RECOMP_REVISION},
            {"sdk_lock_sha256", PS2N_PS2RECOMP_LOCK_SHA256}, {"input_sha256", input_hash}, {"input", path_utf8(input)}, {"disc", disc},
            {"functions", counters.functionsRecompiled}, {"runtime_ready", false}, {"files", json::object()}};
        for (const auto& item : fs::recursive_directory_iterator(native_io_path(output))) {
            if (!item.is_regular_file()) continue;
            receipt["files"][path_utf8(item.path().lexically_relative(native_io_path(output)))] = sha256_file(item.path());
        }
        (void)write_text_if_changed(receipt_path, receipt.dump(2) + '\n');
        std::cout << "PS2Recomp AOT generated: " << counters.functionsRecompiled
                  << " functions; runtime integration remains pending\n";
        return 0;
    } catch (...) {
        // This invocation exclusively created this directory; originals are outside it.
        std::error_code ignored;
        fs::remove_all(native_io_path(output), ignored);
        throw;
    }
}
} // namespace ps2native::dev
