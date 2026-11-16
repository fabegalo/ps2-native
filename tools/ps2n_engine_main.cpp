#include "ps2native/engine/console.hpp"
#include "ps2native/engine/graphics_output.hpp"
#include "iop_cdvd.hpp"
#include <cstdio>
#include <algorithm>
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
    try {
        std::filesystem::path iop_input,iop_output,capture,rom0,disc;std::string profile;auto medium=ps2native::engine::IopCdvd::Medium::Auto;
        unsigned present_ms=0;std::uint64_t max_ticks=100000000;bool extended=true,audio_output=true;
        for(int n=1;n<argc;++n) {
            if(n+1>=argc)throw std::runtime_error("Engine option requires a value");
            const auto option=std::string(argv[n++]);
            if(option=="--iop")iop_input=ps2::os::path(argv[n]);
            else if(option=="--iop-output")iop_output=ps2::os::path(argv[n]);
            else if(option=="--disc")disc=ps2::os::path(argv[n]);
            else if(option=="--disc-type"){const std::string type=argv[n];if(type=="cd")medium=ps2native::engine::IopCdvd::Medium::Cd;else if(type=="dvd")medium=ps2native::engine::IopCdvd::Medium::Dvd;else throw std::runtime_error("Invalid physical disc type");}
            else if(option=="--profile")profile=argv[n];
            else if(option=="--max-ticks"){max_ticks=std::stoull(argv[n]);if(!max_ticks || max_ticks>10000000000ull)throw std::runtime_error("Logical run limit must be in 1..10000000000");}
            else if(option=="--cdvd-modules"){const std::string family=argv[n];if(family!="legacy" && family!="extended")throw std::runtime_error("Unknown original CDVD module family");extended=family=="extended";}
            else if(option=="--rom0")rom0=ps2::os::path(argv[n]);
            else if(option=="--audio-output"){const std::string mode=argv[n];if(mode!="on" && mode!="off")throw std::runtime_error("Audio output must be on or off");audio_output=mode=="on";}
            else if(option=="--capture")capture=ps2::os::path(argv[n]);
            else if(option=="--present-ms")present_ms=std::stoul(argv[n]);
            else throw std::runtime_error("Unknown engine option");
        }
        if(present_ms>5000)throw std::runtime_error("Presentation limit is 5000 ms");
        if(iop_input.empty()!=iop_output.empty())throw std::runtime_error("IOP input and observation must be supplied together");
        const auto protect=[&](const std::filesystem::path& output){if(output.empty() || !std::filesystem::exists(ps2native::native_io_path(output)))return;for(const auto& input:{ps2::os::path(PS2N_ENGINE_ELF),iop_input,rom0,disc})if(!input.empty() && ps2native::files_equivalent(output,input))throw std::runtime_error("Observation must not overwrite original content");};
        protect(capture);protect(iop_output);
        ps2native::engine::Console console;
        if (ps2native::sha256_file(ps2::os::path(PS2N_ENGINE_ELF)) != kAotElfHash)
            throw std::runtime_error("AOT ELF identity mismatch");
        console.audio_output=audio_output;
        console.load(ps2::os::path(PS2N_ENGINE_ELF));console.execution_limit=max_ticks;
        for (const auto& [pc, word] : kAotWords) {
            if (ps2::ld32(pc) != word) throw std::runtime_error("AOT code image mismatch");
            console.code_words.emplace(pc,word);
        }
        register_generated_code();register_generated_vu();
        if (!iop_input.empty()) {
            const auto input=iop_input,output=iop_output;
            if (std::filesystem::exists(ps2native::native_io_path(output)) &&
                (ps2native::files_equivalent(input,output) || ps2native::files_equivalent(ps2::os::path(PS2N_ENGINE_ELF),output)))
                throw std::runtime_error("IOP observation must not overwrite original content");
            console.load_iop(input);
        }
        if(!profile.empty()){console.cdvd->hardware=ps2native::find_console_hardware(profile);if(!console.cdvd->hardware)throw std::runtime_error("Unknown CDVD console profile");}
        if(!disc.empty())console.cdvd->mount(disc,medium);
        try {if(!rom0.empty())console.boot_cdvd(rom0,extended); console.run(); }

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
        catch (...) {
            if(console.graphics_initialized && !capture.empty()){try{ps2native::engine::capture_graphics(console,capture);}catch(const std::exception& e){std::fprintf(stderr,"[frame] no failure-boundary capture: %s\n",e.what());}}
            std::fprintf(stderr,"[engine-state] ee_pc=%08X tick=%llu iop_pc=%08X gs=%d vblanks=%llu sif_ms=%08X sif_sm=%08X rpc_absent=%08X\n",console.ee.pc,static_cast<unsigned long long>(console.ticks),console.iop.pc,console.graphics_initialized,static_cast<unsigned long long>(console.graphics_frames),console.sif.msflag,console.sif.smflag,console.rpc_missing_server.value_or(0));
            for(const auto& [id,t]:console.threads)if(t.state!=ps2native::engine::Console::ThreadState::Dormant)
                std::fprintf(stderr,"[engine-ee-thread] id=%u state=%u priority=%u pc=%08X wait=%u\n",id,static_cast<unsigned>(t.state),t.priority,id==console.current?console.ee.pc:t.context.pc,t.wait);
            for(const auto& [id,t]:console.iop_threads)if(t.status!=16)
                std::fprintf(stderr,"[engine-iop-thread] id=%u status=%u priority=%u pc=%08X wait=%u object=%u\n",id,t.status,t.priority,id==console.iop_current?console.iop.pc:t.context.pc,t.wait,t.wait_id);
            std::fprintf(stderr,"[engine-registers] ee_ra=%08X a0=%08X a1=%08X v0=%08X pmode=%016llX rpc_active=%zu\n",console.ee.r[31].w[0],console.ee.r[4].w[0],console.ee.r[5].w[0],console.ee.r[2].w[0],static_cast<unsigned long long>(console.gs_private[0]),console.rpc_calls.size());
            std::fprintf(stderr,"[engine-irq-state] status=%08X intc=%08X mask=%08X dmac=%08X\n",console.ee.cop0[12],console.intc_status,console.intc_mask,console.dmac_status);
            for(const auto& [id,h]:console.handlers)std::fprintf(stderr,"[engine-irq-handler] id=%u cause=%u function=%08X\n",id,h.cause,h.function);
            for(const auto& [id,sema]:console.semaphores)std::fprintf(stderr,"[engine-sema] id=%u count=%d maximum=%d waiters=%zu\n",id,sema.count,sema.maximum,sema.waiters.size());
            if(!console.iop_output.empty()){
                const auto size=std::min<std::size_t>(4096,console.iop_output.size());
                std::fprintf(stderr,"[engine-iop-tty] %s\n",size==console.iop_output.size()?"complete":"last 4096 bytes; truncated observation");
                std::fwrite(console.iop_output.data()+console.iop_output.size()-size,1,size,stderr);
            }
            std::fwrite(console.output.data(),1,console.output.size(),stdout);throw;
        }
        if(!capture.empty()) {
            if(std::filesystem::exists(ps2native::native_io_path(capture)) && (ps2native::files_equivalent(capture,ps2::os::path(PS2N_ENGINE_ELF)) || (!iop_input.empty() && ps2native::files_equivalent(capture,iop_input))))throw std::runtime_error("Capture must not overwrite original content");
            ps2native::engine::capture_graphics(console,capture);
        }
        if(present_ms)ps2native::engine::present_graphics(console,present_ms);
        const auto ee_finish=console.ticks;
        if (!iop_input.empty()) {
            console.run_iop();
            const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(console.iop_output.data()),console.iop_output.size());
            ps2native::write_file_atomically(iop_output,bytes);
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
