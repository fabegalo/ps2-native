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
void register_generated_vu();
int main(int argc,char** argv) {
    if (argc!=1 && (argc!=5 || std::string(argv[1])!="--iop" || std::string(argv[3])!="--iop-output")) {
        std::fprintf(stderr,"usage: ps2n-engine-run [--iop ORIGINAL.irx --iop-output OBSERVED.txt]\n");return 2;
    }
    try {
        ps2native::engine::Console console;
        if (ps2native::sha256_file(ps2::os::path(PS2N_ENGINE_ELF)) != kAotElfHash)
            throw std::runtime_error("AOT ELF identity mismatch");
        console.load(ps2::os::path(PS2N_ENGINE_ELF));
        for (const auto& [pc, word] : kAotWords) {
            if (ps2::ld32(pc) != word) throw std::runtime_error("AOT code image mismatch");
            console.code_words.emplace(pc,word);
        }
        register_generated_code();register_generated_vu();
        if (argc==5) {
            const auto input=ps2::os::path(argv[2]),output=ps2::os::path(argv[4]);
            if (std::filesystem::exists(ps2native::native_io_path(output)) &&
                (ps2native::files_equivalent(input,output) || ps2native::files_equivalent(ps2::os::path(PS2N_ENGINE_ELF),output)))
                throw std::runtime_error("IOP observation must not overwrite original content");
            console.load_iop(input);
        }
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
        const auto ee_finish=console.ticks;
        if (argc==5) {
            console.run_iop();
            const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(console.iop_output.data()),console.iop_output.size());
            ps2native::write_file_atomically(ps2::os::path(argv[4]),bytes);
            std::fprintf(stderr,"[joint] ee_finish=%llu iop_first=%llu iop_last=%llu iop_instructions=%llu\n",
                static_cast<unsigned long long>(ee_finish),static_cast<unsigned long long>(console.iop_first_tick),
                static_cast<unsigned long long>(console.iop_last_tick),static_cast<unsigned long long>(console.iop_instructions));
            if (console.iop_result) return console.iop_result;
        }
        for(unsigned n=0;n<2;++n)if(console.vu_aot_pairs[n] || console.vu_interpreted_pairs[n])
            std::fprintf(stderr,"[engine-vu] unit=%u aot_pairs=%llu interpreted_pairs=%llu busy=%d\n",n,
                static_cast<unsigned long long>(console.vu_aot_pairs[n]),static_cast<unsigned long long>(console.vu_interpreted_pairs[n]),console.vu[n].busy);
        std::fwrite(console.output.data(), 1, console.output.size(), stdout);
        std::fprintf(stderr, "[engine] original ELF exited, AOT only, instruction_cost=%llu\n", static_cast<unsigned long long>(console.ticks));
        return console.exit_code;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[engine] %s\n", error.what());
        return 1;
    }
}
