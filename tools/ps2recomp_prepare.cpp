#include "ps2native/disc.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "ps2recomp_codegen.hpp"
#include "ps2recomp/config_manager.h"
#include "ps2recomp/elf_analyzer.h"
#include "ps2recomp/ps2_recompiler.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <set>
#include <stdexcept>

namespace ps2native::dev {
int prepare_ps2recomp(const std::filesystem::path& source, const std::filesystem::path& destination, bool disc,std::uint32_t alias) {
    namespace fs = std::filesystem;
    using nlohmann::json;
    const auto input = fs::weakly_canonical(fs::absolute(source));
    const auto output = fs::weakly_canonical(fs::absolute(destination));
    const auto input_hash = sha256_file(input);
    const auto image = disc ? DiscImage(input).boot_elf() : parse_ee_elf(read_file(input));
    if(alias && disc)throw std::runtime_error("execution alias requires a stripped ELF");
    if(alias)(void)ee_execution_alias(image,alias);
    constexpr const char* required[] = {"ps2_recompiled_functions.cpp", "ps2_recompiled_functions.h", "ps2_recompiled_stubs.h", "register_functions.cpp"};
    const auto receipt_path = output / "sdk-prepare.json";
    if (fs::exists(native_io_path(output))) {
        const auto bytes = read_file(receipt_path);
        const auto receipt = json::parse(bytes.begin(), bytes.end());
        if (receipt.at("schema") != "ps2n-ps2recomp-prepare-v1" || receipt.at("input_sha256") != input_hash ||
            receipt.at("sdk_lock_sha256") != PS2N_PS2RECOMP_LOCK_SHA256 || receipt.at("disc") != disc ||
            receipt.at("native_backend") != "ps2runtime-native-v1" || receipt.value("execution_alias",0u)!=alias ||
            receipt.at("native_backend_sha256") != PS2N_PS2RECOMP_BACKEND_SHA256 || receipt.at("files").empty())
            throw std::runtime_error("PS2Recomp output identity differs; preserve it and select a different output directory");
        for (const auto* name : required)
            if (!receipt.at("files").contains(std::string("generated/")+name))
                throw std::runtime_error("PS2Recomp receipt is incomplete");
        for (const auto& [name, hash] : receipt.at("files").items()) {
            const auto path = fs::u8path(name);
            if (path.empty() || path.is_absolute()) throw std::runtime_error("Invalid SDK receipt path");
            for (const auto& part : path) if (part == "..") throw std::runtime_error("Invalid SDK receipt path");
            if (sha256_file(output / path) != hash.get<std::string>())
                throw std::runtime_error("PS2Recomp output changed: " + name);
        }
        std::cout << "PS2Recomp native translation reused; host compilation and execution must be validated\n";
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
        std::set<std::uint32_t> entries{image.entry};
        entries.insert(image.function_entries.begin(),image.function_entries.end());
        for (const auto& function : analyzer.getFunctions()) entries.insert(function.start);
        for (const auto entry : entries) {
            (void)image.instruction_at(entry);
            std::ostringstream address;
            address << "0x" << std::hex << entry;
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
        const auto require_complete = [&](bool native_emitted=false) {
            const bool errors=native_emitted ? counters.nonInstructionErrors!=0 : recompiler.hasErrors();
            if (errors || counters.decodeFailures || counters.functionsStubbed || counters.functionsSkipped ||
                (!native_emitted && counters.unhandledInstructions) || counters.correctnessCriticalGuestFallbacks ||
                counters.correctnessCriticalFailures || !counters.functionsRecompiled)
            {
                recompiler.printReport();
                throw std::runtime_error("PS2Recomp translation incomplete: decode failure, unsupported instruction, skipped function or stub");
            }
        };
        require_complete();
        reset_sdk_native_codegen(alias);
        recompiler.generateOutput();
        // Standalone emission errors do not authorize native skips: every word
        // is independently emitted or omitted with an exact strict-stop PC.
        require_complete(true);
        write_sdk_native_sources(output/"generated/ps2_recompiled_functions.cpp");
        // Configured entry points can add synthesized native function roots.
        if (counters.generatedFunctions<counters.functionsRecompiled)throw std::runtime_error("PS2Recomp omitted generated functions");
        // Required SDK outputs must be present; generateOutput() has a void API.
        for (const auto* name : required)
            if (!regular_file_size(output / "generated" / name)) throw std::runtime_error("PS2Recomp output is empty");
        if (sha256_file(input) != input_hash) throw std::runtime_error("Input changed during PS2Recomp preparation");
        json receipt{{"schema", "ps2n-ps2recomp-prepare-v1"}, {"sdk_revision", PS2N_PS2RECOMP_REVISION},
            {"sdk_lock_sha256", PS2N_PS2RECOMP_LOCK_SHA256}, {"input_sha256", input_hash}, {"input", path_utf8(input)}, {"disc", disc},
            {"functions", counters.functionsRecompiled}, {"native_backend", "ps2runtime-native-v1"},
            {"native_backend_sha256", PS2N_PS2RECOMP_BACKEND_SHA256}, {"elf_sha256", sha256_hex(image.bytes)},
            {"runtime_ready", false}, {"execution_alias",alias}, {"files", json::object()}};
        receipt["native_translated_instructions"]=sdk_native_instruction_count();
        receipt["sdk_unhandled_instructions"]=counters.unhandledInstructions;
        receipt["native_unsupported"]=json::array();
        for(const auto& [pc,word]:sdk_native_missing_translations())
            receipt["native_unsupported"].push_back({{"pc",pc},{"word",word}});
        for (const auto& item : fs::recursive_directory_iterator(native_io_path(output))) {
            if (!item.is_regular_file()) continue;
            const auto relative=item.path().lexically_relative(native_io_path(output)).generic_u8string();
            receipt["files"][std::string(relative.begin(),relative.end())] = sha256_file(item.path());
        }
        (void)write_text_if_changed(receipt_path, receipt.dump(2) + '\n');
        std::cout << "PS2Recomp AOT generated: " << counters.functionsRecompiled
                  << " analyzed functions for the integrated PS2Runtime; host compilation and execution must be validated\n"
                  << "Native instructions="<<sdk_native_instruction_count()<<" unsupported="<<sdk_native_missing_translations().size()
                  <<"; execution stops before every missing translation\n";
        return 0;
    } catch (...) {
        // This invocation exclusively created this directory; originals are outside it.
        std::error_code ignored;
        fs::remove_all(native_io_path(output), ignored);
        throw;
    }
}
} // namespace ps2native::dev
