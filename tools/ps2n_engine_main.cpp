#include "ps2native/engine/console.hpp"
#include <cstdio>
#include "ps2/os.hpp"
#include "aot_image.hpp"
#include "ps2native/sha256.hpp"
#include <exception>
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
        console.run();
        std::fwrite(console.output.data(), 1, console.output.size(), stdout);
        std::fprintf(stderr, "[engine] original ELF exited, AOT only, instruction_cost=%llu\n", static_cast<unsigned long long>(console.ticks));
        return console.exit_code;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[engine] %s\n", error.what());
        return 1;
    }
}
