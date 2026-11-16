#include "ps2native/engine/console.hpp"
#include <cstdio>
#include "ps2/os.hpp"
#include "aot_image.hpp"
#include "ps2native/sha256.hpp"
#include <exception>
#include <cstdlib>
#include <format>
#include "ps2native/io.hpp"
void register_generated_code();
int main() {
    try {
        ps2native::engine::Console console;
        if (ps2native::sha256_file(ps2::os::path(PS2N_ENGINE_ELF)) != kAotElfHash)
            throw std::runtime_error("AOT ELF identity mismatch");
        console.load(ps2::os::path(PS2N_ENGINE_ELF));
        for (const auto& [pc, word] : kAotWords) {
            if (ps2::ld32(pc) != word) throw std::runtime_error("AOT code image mismatch");
            console.code_words.emplace(pc,word);
        }
        register_generated_code();
        try { console.run(); }
        catch (const ps2native::engine::MissingAotEntry& missing) {
            const auto base=missing.pc & ~0xffffu;
            const auto physical=base & 0x1fffffff;
            if (const auto* root=std::getenv("PS2N_ENGINE_AOT_MISSING_DIR"); root && physical<console.ram.size() && 0x10000<=console.ram.size()-physical) {
                const auto directory=ps2::os::path(root);
                ps2native::create_directories(directory);
                ps2native::write_file_atomically(directory/"image.bin",std::span(console.ram).subspan(physical,0x10000));
                const auto info=std::format("{{\"base\":{},\"entry\":{}}}\n",base,missing.pc);
                (void)ps2native::write_text_if_changed(directory/"missing.json",info);
                std::fprintf(stderr,"[engine-aot-missing] %08X\n",missing.pc);
                std::fwrite(console.output.data(),1,console.output.size(),stdout);
                return 86;
            }
            throw;
        }
        catch (...) { std::fwrite(console.output.data(),1,console.output.size(),stdout); throw; }
        std::fwrite(console.output.data(), 1, console.output.size(), stdout);
        std::fprintf(stderr, "[engine] original ELF exited, AOT only, instruction_cost=%llu\n", static_cast<unsigned long long>(console.ticks));
        return console.exit_code;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[engine] %s\n", error.what());
        return 1;
    }
}
