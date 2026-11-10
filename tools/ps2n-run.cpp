// Native ELF/disc runner with optional selective HLE; no title overrides.
#include "ps2native/boot.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/disc.hpp"
#include "ps2native/hle.hpp"
#include "ps2native/iop_aot.hpp"
#include "ps2native/frontend/native_module.hpp"
#include "ps2native/frontend/content.hpp"
#include "ps2_runtime.h"
#include "../src/sdk/console_interface.hpp"
#include "dvd_geometry_cli.hpp"
#include "diagnostic_cli.hpp"
#include "checkpoint_cli.hpp"
#include "vu_aot_cli.hpp"
#include "gs_gpu_cli.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

template<class C> int run_main(int argc, C** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: ps2n-run <owned-ps2.elf> [--budget=N] [--stop=ADDRESS] [--frame=out.bmp]\n"
                         "         [--disc] [--trace=events|full] [--trace-out=FILE] [--tail=N]\n"
                         "         [--iop-image=raw.bin --iop-load=ADDRESS --iop-entry=ADDRESS]\n"
                         "         [--hle] (PS2Recomp EE kernel services; incomplete, no original boot)\n";
            std::cerr << "         [--native-module=PREPARATION_RECEIPT] (verified offline native code module)\n";
            std::cerr << "         [--rom0=FILE --rom0-sha256=HASH] (identified HLE system dependency)\n";
            std::cerr << "         [--rtc-utc=UNIX_SECONDS] (HLE: defaults to the host clock at session start)\n";
            std::cerr << "         [--console=MODEL:MAINBOARD] [--nvm=FILE --nvm-sha256=HASH] (matching HLE console inputs)\n";
            std::cerr << "         [--assume-ee-early-digit-carry] (HLE: unverified external-reference scalar multiplier)\n";
            std::cerr << "         [--assume-spu2-pio-word-cycles=N] (HLE: unverified positive IOP cycles per 16-bit word)\n";
            std::cerr << "         [--assume-spu2-adma-word-cycles=N] (HLE: unverified shared DMA bus timing)\n";
            std::cerr << "         [--assume-spu2-quiescent-reset] (HLE: unverified initially inactive voices and inputs)\n";
            std::cerr << "         [--assume-spu2-effect-end-reset=CORE0:CORE1] (HLE: unverified decimal four-bit EEA)\n";
            std::cerr << "         [--assume-spu2-reserved-latches] (HLE: unmeasured unassigned-register effects)\n";
            std::cerr << "         [--assume-spu2-7ca-latch] (HLE: unknown SPU2 output effects retained diagnostically)\n";
            std::cerr << "         [--diagnostic-cache-uncached-hits] (HLE: unverified physical cache alias translation)\n";
            std::cerr << "         [--diagnostic-dvd-layer0=SECTORS --diagnostic-dvd-track=otp|ptp] (explicit diagnostic geometry)\n";
            std::cerr << "         [--diagnostic-dir=DIR --diagnostic-ring=N --diagnostic-stream-bytes=N]\n"
                         "         [--diagnostic-ram=ADDRESS:BYTES] (passive backing RAM)\n"
                         "         [--resume=FILE --checkpoint-out=FILE --checkpoint-every=N]\n"
                         "         [--checkpoint-scenario=SHA256] [--checkpoint-allow-runtime-change]\n";
            std::cerr << "         [--profile] (requires -DPS2N_ENABLE_PROFILING=ON; host component timings)\n";
            std::cerr << "         [--vu1-capture=DIR] (offline VU1 AOT diagnostics)\n";
            std::cerr << "         [--diagnostic-iop-ram=ADDRESS:BYTES] (passive physical RAM, up to four 512-byte windows)\n";
            std::cerr << "         [--iop-capture=FILE] (loaded IRX snapshot, at most 1 MiB)\n";
            std::cerr << "         [--gs-backend=cpu|vulkan|directx] (experimental integer compute raster)\n";
            return 2;
        }
        ps2native::BootOptions options;
        ps2native::cli::DiagnosticOptions diagnostic;
        ps2native::cli::CheckpointOptions checkpoint;
        ps2native::cli::VuAotOptions vu1;
        ps2native::cli::GsGpuOptions graphics;
        ps2native::cli::DiagnosticDvdOptions dvd;
        std::filesystem::path frame;
        std::filesystem::path iop_image;
        std::filesystem::path iop_capture;
        std::vector<std::pair<std::uint32_t,std::uint32_t>> iop_ram_windows;
        std::optional<std::uint32_t> iop_load, iop_entry;
        std::optional<std::uint64_t> rtc_utc;
        std::filesystem::path trace_out;
        std::filesystem::path native_receipt;
        std::filesystem::path rom0;std::string rom0_hash;
        std::filesystem::path nvm;std::string nvm_hash,console_profile;
        bool disc_input=false;
        std::optional<std::array<std::uint16_t,2>> spu2_effect_end;
        bool hle=false,ee_early_digit_carry=false,diagnostic_cache_uncached_hits=false,spu2_7ca_latch=false,spu2_reserved_latches=false,spu2_quiescent_reset=false;
        std::uint32_t spu2_pio_word_cycles=0,spu2_adma_word_cycles=0;
        for (int i = 2; i < argc; ++i) {
            const auto flag = ps2native::path_utf8(std::filesystem::path(argv[i]));
            if(dvd.parse(flag))continue;
            const auto number = [&](std::size_t offset, std::uint64_t max) {
                const auto value = flag.substr(offset);
                std::size_t used = 0;
                if (value.empty() || value[0] == '-') throw std::runtime_error("invalid numeric option");
                const auto n = std::stoull(value, &used, 0);
                if (used != value.size() || n > max) throw std::runtime_error("numeric option out of range");
                return n;
            };
            if (diagnostic.parse(flag) || checkpoint.parse(flag,argv[i]) || vu1.parse(flag,argv[i]) || graphics.parse(flag)) {}
            else if (flag.starts_with("--budget=")) options.max_instructions = number(9, ~0ull);
            else if (flag.starts_with("--stop=")) options.stop_at = static_cast<std::uint32_t>(number(7, 0xFFFFFFFFu));
            else if (flag.starts_with("--frame=")) frame = std::filesystem::path(argv[i]).native().substr(8);
            else if (flag.starts_with("--iop-image=")) iop_image = std::filesystem::path(argv[i]).native().substr(12);
            else if (flag.starts_with("--iop-capture=")) iop_capture = std::filesystem::path(argv[i]).native().substr(14);
            else if(flag.starts_with("--diagnostic-iop-ram=")) {
                const auto text=flag.substr(21);const auto colon=text.find(':');
                if(colon==std::string::npos || iop_ram_windows.size()>=4u)throw std::runtime_error("IOP RAM window requires ADDRESS:BYTES; at most four windows");
                std::size_t used=0;const auto address=std::stoull(text.substr(0,colon),&used,0);
                const auto bytes=ps2native::cli::decimal(text.substr(colon+1u),512u);
                if(used!=colon || (address&3u) || !bytes || (bytes&3u) || address>=ps2native::kIopRamBytes || bytes>ps2native::kIopRamBytes-address)
                    throw std::runtime_error("IOP RAM window requires aligned physical RAM and 4..512 bytes");
                iop_ram_windows.emplace_back(static_cast<std::uint32_t>(address),static_cast<std::uint32_t>(bytes));
            }
            else if (flag.starts_with("--iop-load=")) iop_load = static_cast<std::uint32_t>(number(11,0xFFFFFFFFu));
            else if (flag.starts_with("--iop-entry=")) iop_entry = static_cast<std::uint32_t>(number(12,0xFFFFFFFFu));
            else if (flag == "--disc") disc_input=true;
            else if (flag == "--hle") hle=true;
            else if (flag == "--assume-ee-early-digit-carry") {
                if(ee_early_digit_carry)throw std::runtime_error("duplicate EE scalar carry diagnostic");
                ee_early_digit_carry=true;
            }
            else if (flag == "--diagnostic-cache-uncached-hits") diagnostic_cache_uncached_hits=true;
            else if (ps2native::cli::parse_spu2_effect_end(flag,spu2_effect_end)) {}
            else if (flag == "--assume-spu2-reserved-latches") spu2_reserved_latches=true;
            else if (flag == "--assume-spu2-7ca-latch") spu2_7ca_latch=true;
            else if (flag == "--assume-spu2-quiescent-reset") spu2_quiescent_reset=true;
            else if (flag.starts_with("--assume-spu2-adma-word-cycles=")) {
                if(spu2_adma_word_cycles)throw std::runtime_error("duplicate SPU2 DMA timing");
                spu2_adma_word_cycles=static_cast<std::uint32_t>(ps2native::cli::decimal(flag.substr(31),UINT32_MAX));
                if(!spu2_adma_word_cycles)throw std::runtime_error("SPU2 DMA word cycles must be positive");
            }
            else if (flag.starts_with("--assume-spu2-pio-word-cycles=")) {
                if(spu2_pio_word_cycles)throw std::runtime_error("duplicate SPU2 PIO timing");
                spu2_pio_word_cycles=static_cast<std::uint32_t>(ps2native::cli::decimal(flag.substr(30),UINT32_MAX));
                if(!spu2_pio_word_cycles)throw std::runtime_error("SPU2 PIO word cycles must be positive");
            }
            else if (flag.starts_with("--rtc-utc=")) rtc_utc=number(10,UINT64_MAX);
            else if (flag.starts_with("--console=")) console_profile=flag.substr(10);
            else if (flag.starts_with("--nvm=")) nvm=std::filesystem::path(argv[i]).native().substr(6);
            else if (flag.starts_with("--nvm-sha256=")) nvm_hash=flag.substr(13);
            else if (flag.starts_with("--native-module=")) native_receipt=std::filesystem::path(argv[i]).native().substr(16);
            else if (flag.starts_with("--rom0=")) rom0=std::filesystem::path(argv[i]).native().substr(7);
            else if (flag.starts_with("--rom0-sha256=")) rom0_hash=flag.substr(14);
            else if (flag == "--trace=full") options.trace=ps2native::TraceLevel::Full;
            else if (flag == "--trace=events") options.trace=ps2native::TraceLevel::Events;
            else if (flag.starts_with("--trace-out=")) trace_out=std::filesystem::path(argv[i]).native().substr(12);
            else if (flag.starts_with("--tail=")) options.tail_instructions=static_cast<std::size_t>(number(7,1000000));
            else throw std::runtime_error("unknown option: " + flag);
        }
        if((!rom0.empty() || !rom0_hash.empty()) && !hle)throw std::runtime_error("HLE ROM0 dependency requires --hle");
        if(ee_early_digit_carry && !hle)throw std::runtime_error("Diagnostic EE scalar carry requires --hle");
        if(spu2_pio_word_cycles && !hle)throw std::runtime_error("Diagnostic SPU2 PIO timing requires --hle");
        if(spu2_adma_word_cycles && !hle)throw std::runtime_error("Diagnostic SPU2 DMA timing requires --hle");
        if(spu2_quiescent_reset && !hle)throw std::runtime_error("Diagnostic SPU2 quiescent reset requires --hle");
        if(spu2_effect_end && !hle)throw std::runtime_error("Diagnostic SPU2 initial EEA requires --hle");
        if(spu2_reserved_latches && !hle)throw std::runtime_error("Diagnostic SPU2 reserved latches requires --hle");
        if(spu2_7ca_latch && !hle)throw std::runtime_error("Diagnostic SPU2 0x7CA latch requires --hle");
        if(diagnostic_cache_uncached_hits && !hle)throw std::runtime_error("Diagnostic cache alias translation requires --hle");
        if(rtc_utc && !hle)throw std::runtime_error("RTC input in this runner requires --hle");
        if((!console_profile.empty() || !nvm.empty() || !nvm_hash.empty()) && !hle)throw std::runtime_error("Console inputs in this runner require --hle");
        if(rom0.empty() && !rom0_hash.empty())throw std::runtime_error("ROM0 hash requires its original image path");
        const auto geometry=dvd.geometry(disc_input);
        if(geometry)std::cout<<"diagnostic_dvd_layer0="<<geometry->layer0_sectors<<" track="<<(geometry->opposite_track_path?"otp":"ptp")<<'\n';
        const auto disc=disc_input ? std::make_shared<ps2native::DiscImage>(std::filesystem::path(argv[1]),geometry) : nullptr;
        const auto elf = disc ? disc->boot_elf()
                                   : ps2native::parse_ee_elf(ps2native::read_file(std::filesystem::path(argv[1])));
        std::unique_ptr<ps2native::frontend::LoadedNativeModule> loaded;
        PS2Runtime runtime;
        auto& memory=runtime.memory();
        auto& state=runtime.cpu();
        const ps2native::AotModule* module = nullptr;
#ifdef PS2N_RUN_WITH_AOT
        module = &ps2native::aot_module();
#endif
        if(!native_receipt.empty()) {
            if(module)throw std::runtime_error("Choose a loaded native module or a statically linked module");
            loaded=std::make_unique<ps2native::frontend::LoadedNativeModule>(native_receipt,
                ps2native::sha256_file(std::filesystem::path(argv[1])),ps2native::sha256_hex(elf.bytes));
            module=&loaded->module();
        }
        if(hle) {
            if(!checkpoint.resume.empty() || !checkpoint.output.empty())throw std::runtime_error("HLE kernel checkpoints are not implemented");
            runtime.prepare_hle_elf(elf,module);
            if(ee_early_digit_carry) {
                memory.diagnostic_ee_early_digit_carry();
                std::cout<<"diagnostic_ee_early_digit_carry=external-reference-coarse-multiplier-unverified\n";
            }
            if(diagnostic_cache_uncached_hits) {
                memory.diagnostic_data_cache(true);
                std::cout<<"diagnostic_cache_uncached_hits=physical-translation-unverified\n";
            }
            if(spu2_pio_word_cycles) {
                const_cast<ps2native::Iop&>(memory.iop()).diagnostic_spu2_pio_word_cycles(spu2_pio_word_cycles);
                std::cout<<"diagnostic_spu2_pio_word_cycles="<<spu2_pio_word_cycles<<" (unverified timing)\n";
            }
            if(spu2_adma_word_cycles) {
                const_cast<ps2native::Iop&>(memory.iop()).diagnostic_spu2_adma_word_cycles(spu2_adma_word_cycles);
                std::cout<<"diagnostic_spu2_adma_word_cycles="<<spu2_adma_word_cycles<<" (unverified shared-bus timing)\n";
            }
            if(spu2_quiescent_reset) {
                ps2native::configure_hle_spu2_quiescent_reset(memory);
                std::cout<<"diagnostic_spu2_quiescent_reset=unverified-inactive-voices-and-inputs\n";
            }
            if(spu2_effect_end) {
                const_cast<ps2native::Iop&>(memory.iop()).diagnostic_spu2_effect_end_reset(*spu2_effect_end);
                std::cout<<"diagnostic_spu2_effect_end_reset="<<(*spu2_effect_end)[0]<<':'<<(*spu2_effect_end)[1]<<" (unverified reset)\n";
            }
            if(spu2_reserved_latches) {
                const_cast<ps2native::Iop&>(memory.iop()).diagnostic_spu2_reserved_latches();
                std::cout<<"diagnostic_spu2_reserved_latches=unverified-output-effects\n";
            }
            if(spu2_7ca_latch) {
                const_cast<ps2native::Iop&>(memory.iop()).diagnostic_spu2_7ca_latch();
                std::cout<<"diagnostic_spu2_7ca_latch=unverified-output-effects\n";
            }
            if(!rtc_utc)rtc_utc=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            memory.configure_rtc(*rtc_utc);
            std::cout<<"rtc_epoch_utc="<<*rtc_utc<<'\n';
            if(!rom0.empty())memory.hle_kernel()->configure_bios(std::make_shared<const ps2native::BiosImage>(
                ps2native::load_bios_rom0(ps2native::read_file(rom0),ps2native::BiosLoadOptions{
                    rom0_hash.empty()?std::nullopt:std::optional<std::string>{rom0_hash}})));
            const auto console=ps2native::load_hle_console(console_profile,nvm,nvm_hash);
            ps2native::configure_hle_console(memory,console.hardware,console.nvm);
        } else runtime.prepare_direct_elf(elf,module);
        if(disc) {
            const auto probe=ps2native::frontend::probe_disc(std::filesystem::path(argv[1]));
            if(probe.status!=ps2native::frontend::ProbeStatus::Valid)throw std::runtime_error("Disc refused: "+probe.error);
            memory.mount_disc(disc,probe.info.at("media")=="DVD" ? ps2native::Iop::DiscMedium::Ps2Dvd : ps2native::Iop::DiscMedium::Ps2Cd,true);
        }
        if (!iop_image.empty() || iop_load || iop_entry) {
            if (iop_image.empty() || !iop_load || !iop_entry)
                throw std::runtime_error("IOP image requires explicit --iop-image, --iop-load and --iop-entry");
            const auto image=ps2native::read_file(iop_image);
            memory.load_iop_program(image,*iop_load,*iop_entry);
        }
        const std::array protected_inputs{std::filesystem::path(argv[1]), iop_image};
        diagnostic.attach(options, protected_inputs);
        const auto start = std::chrono::steady_clock::now();
        auto report = checkpoint.run(state, memory, module, options, protected_inputs,[&](auto& restored) {
            const std::array capture_inputs{std::filesystem::path(argv[1]),iop_image,checkpoint.resume,checkpoint.output};
            vu1.attach(restored,capture_inputs);
            graphics.attach(restored);
        });
        while(loaded && report.outcome==ps2native::BootOutcome::BlockPoint &&
              loaded->activate_overlays(memory,report.pc,options)) {
            options.max_instructions-=report.retired;
            const auto previous=report;
            if(checkpoint.profile_enabled)options.profile=&checkpoint.profile;
            const auto resumed=std::chrono::steady_clock::now();
            report=runtime.run(module,options);
            checkpoint.execution_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-resumed).count();
            report.retired+=previous.retired;report.aot_instructions+=previous.aot_instructions;
            report.interpreted_instructions+=previous.interpreted_instructions;
            report.engine_switches+=previous.engine_switches;report.frames_presented+=previous.frames_presented;
            options.max_instructions+=previous.retired;
        }
        diagnostic.finish(report, state, memory);
        graphics.print(memory);
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
        std::cout << (hle ? "HLE-assisted ELF (PS2Recomp kernel, incomplete), sha256 " : "Direct ELF bare-metal (no BIOS services), sha256 ") << ps2native::sha256_hex(elf.bytes)
                  << "\n" << ps2native::boot_outcome_name(report.outcome) << " pc=0x" << std::hex << report.pc
                  << std::dec << " retired=" << report.retired << " aot=" << report.aot_instructions
                  << " interpreted=" << report.interpreted_instructions << " switches=" << report.engine_switches
                  << " runtime_ms=" << elapsed << '\n' << report.reason << '\n';
        checkpoint.print_execution_metrics(report);
        std::cout<<"IOP_EXECUTION native="<<memory.iop().native_instructions()<<" interpreted="<<memory.iop().interpreted_instructions()<<'\n';
        if(memory.iop().stopped()) {
            std::cout<<"IOP_IRQ";
            for(const auto& [name,address]:{std::pair{"istat",0x1F801070u},{"imask",0x1F801074u},{"ictrl",0x1F801078u},{"dicr",0x1F8010F4u},{"dicr2",0x1F801574u},
                {"dma3_madr",0x1F8010B0u},{"dma3_bcr",0x1F8010B4u},{"dma3_chcr",0x1F8010B8u},{"dpcr",0x1F8010F0u},{"dma_enable",0x1F801578u}}) {
                const auto value=memory.iop().diagnostic_latch(address);std::cout<<' '<<name<<'=';
                if(value)std::cout<<"0x"<<std::hex<<*value<<std::dec;else std::cout<<"unknown";
            }
            std::cout<<'\n';
        }
        for(const auto& [address,bytes]:iop_ram_windows) {
            for(unsigned offset=0;offset<bytes;offset+=4u) {
                const auto word=memory.iop().diagnostic_ram_observed(address+offset);
                std::cout<<"IOP_RAM 0x"<<std::hex<<address+offset<<" value=0x"<<word.value<<" unknown=0x"<<word.unknown<<std::dec<<'\n';
            }
        }
        if(const auto* kernel=memory.hle_kernel())std::cout<<"HLE syscalls="<<kernel->syscall_count()<<" current_thread="<<kernel->current_thread_id()<<'\n';
        vu1.print(memory);
        if(!iop_capture.empty()) {
            const auto& iop=memory.iop();const auto region=iop.native_module_range(iop.pc());
            if(!region)throw std::runtime_error("No SDK-loaded IOP module at the current PC to capture");
            const auto [base,size]=*region;
            if((base&3u) || !size || size>1024u*1024u)throw std::runtime_error("IOP capture exceeds the aligned 1-MiB module limit");
            for(const auto& input:{std::filesystem::path(argv[1]),rom0,iop_image,native_receipt})
                if(!input.empty() && ps2native::file_exists(iop_capture) && ps2native::files_equivalent(iop_capture,input))
                    throw std::runtime_error("IOP capture must not overwrite an input");
            std::vector<ps2native::ObservedWord> words;
            for(unsigned offset=0;offset<(size+3u)/4u*4u;offset+=4u)words.push_back(iop.diagnostic_ram_observed(base+offset));
            const auto capture=ps2native::encode_iop_aot_image(base,words);
            ps2native::write_file_atomically(iop_capture,capture);
            std::cout<<"iop_capture="<<ps2native::path_utf8(iop_capture)<<" base="<<base<<" pc="<<iop.pc()
                <<" bytes="<<size<<" sha256="<<ps2native::sha256_hex(capture)<<'\n';
        }
        std::cout << "status=0x" << std::hex << state.cp0(ps2native::Cop0::Status)
                  << " cause=0x" << state.cp0(ps2native::Cop0::Cause) << " epc=0x" << state.cp0(ps2native::Cop0::Epc)
                  << " v1=0x" << state.reg32(3) << std::dec
                  << " gs_pixel_writes=" << memory.gs().vram().pixel_writes() << '\n';
        std::string trace="# ELF sha256 "+ps2native::sha256_hex(elf.bytes)+"\n";
        for (const auto& line:report.trace) trace+=line+'\n';
        if(report.outcome==ps2native::BootOutcome::BlockPoint) {
            std::vector<std::string_view> iopEvents;
            for(auto it=report.iop_events.rbegin();it!=report.iop_events.rend() && iopEvents.size()<24u;++it)
                if(it->find("CDVD")!=std::string::npos || it->find("registered IRX")!=std::string::npos ||
                   it->find(" HLE active for IRX ")!=std::string::npos)iopEvents.push_back(*it);
            for(auto it=iopEvents.rbegin();it!=iopEvents.rend();++it)trace+=std::string(*it)+'\n';
        }
        trace+="# instruction tail\n";
        for (const auto& line:report.tail) trace+=line+'\n';
        if (!trace_out.empty()) {
            if (ps2native::file_exists(trace_out) && ps2native::files_equivalent(trace_out,std::filesystem::path(argv[1])))
                throw std::runtime_error("trace output must not overwrite the input");
            (void)ps2native::write_text_if_changed(trace_out,trace);
        } else std::cout << trace;
        if (!frame.empty()) {
            if (ps2native::file_exists(frame) && ps2native::files_equivalent(frame,std::filesystem::path(argv[1])))
                throw std::runtime_error("frame output must not overwrite the input");
            const auto image = memory.gs().scanout(state.retired_instructions);
            ps2native::save_framebuffer_bmp(frame, image);
            std::cout << "frame_rgba_sha256=" << ps2native::sha256_hex(image.rgba) << '\n';
        }
        return report.outcome == ps2native::BootOutcome::StopAddress ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << "ps2n-run: " << error.what() << '\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return run_main(argc, argv); }
#else
int main(int argc, char** argv) { return run_main(argc, argv); }
#endif
