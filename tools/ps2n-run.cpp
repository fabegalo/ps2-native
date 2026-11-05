// Direct ELF bare-metal developer runner. No HLE BIOS or title overrides.
#include "ps2native/boot.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/disc.hpp"
#include "diagnostic_cli.hpp"
#include "checkpoint_cli.hpp"
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
                         "         [--iop-image=raw.bin --iop-load=ADDRESS --iop-entry=ADDRESS]\n";
            std::cerr << "         [--diagnostic-dir=DIR --diagnostic-ring=N --diagnostic-stream-bytes=N]\n"
                         "         [--diagnostic-ram=ADDRESS:BYTES] (passive backing RAM)\n"
                         "         [--resume=FILE --checkpoint-out=FILE --checkpoint-every=N]\n"
                         "         [--checkpoint-scenario=SHA256] [--checkpoint-allow-runtime-change]\n";
            return 2;
        }
        ps2native::BootOptions options;
        ps2native::cli::DiagnosticOptions diagnostic;
        ps2native::cli::CheckpointOptions checkpoint;
        std::filesystem::path frame;
        std::filesystem::path iop_image;
        std::optional<std::uint32_t> iop_load, iop_entry;
        std::filesystem::path trace_out;
        bool disc_input=false;
        for (int i = 2; i < argc; ++i) {
            const auto flag = ps2native::path_utf8(std::filesystem::path(argv[i]));
            const auto number = [&](std::size_t offset, std::uint64_t max) {
                const auto value = flag.substr(offset);
                std::size_t used = 0;
                if (value.empty() || value[0] == '-') throw std::runtime_error("invalid numeric option");
                const auto n = std::stoull(value, &used, 0);
                if (used != value.size() || n > max) throw std::runtime_error("numeric option out of range");
                return n;
            };
            if (diagnostic.parse(flag) || checkpoint.parse(flag,argv[i])) {}
            else if (flag.starts_with("--budget=")) options.max_instructions = number(9, ~0ull);
            else if (flag.starts_with("--stop=")) options.stop_at = static_cast<std::uint32_t>(number(7, 0xFFFFFFFFu));
            else if (flag.starts_with("--frame=")) frame = std::filesystem::path(argv[i]).native().substr(8);
            else if (flag.starts_with("--iop-image=")) iop_image = std::filesystem::path(argv[i]).native().substr(12);
            else if (flag.starts_with("--iop-load=")) iop_load = static_cast<std::uint32_t>(number(11,0xFFFFFFFFu));
            else if (flag.starts_with("--iop-entry=")) iop_entry = static_cast<std::uint32_t>(number(12,0xFFFFFFFFu));
            else if (flag == "--disc") disc_input=true;
            else if (flag == "--trace=full") options.trace=ps2native::TraceLevel::Full;
            else if (flag == "--trace=events") options.trace=ps2native::TraceLevel::Events;
            else if (flag.starts_with("--trace-out=")) trace_out=std::filesystem::path(argv[i]).native().substr(12);
            else if (flag.starts_with("--tail=")) options.tail_instructions=static_cast<std::size_t>(number(7,1000000));
            else throw std::runtime_error("unknown option: " + flag);
        }
        const auto elf = disc_input ? ps2native::DiscImage(std::filesystem::path(argv[1])).boot_elf()
                                   : ps2native::parse_ee_elf(ps2native::read_file(std::filesystem::path(argv[1])));
        ps2native::Memory memory;
        ps2native::EEState state;
        const ps2native::AotModule* module = nullptr;
#ifdef PS2N_RUN_WITH_AOT
        module = &ps2native::aot_module();
#endif
        ps2native::prepare_direct_elf(elf, module, state, memory);
        if (!iop_image.empty() || iop_load || iop_entry) {
            if (iop_image.empty() || !iop_load || !iop_entry)
                throw std::runtime_error("IOP image requires explicit --iop-image, --iop-load and --iop-entry");
            const auto image=ps2native::read_file(iop_image);
            memory.load_iop_program(image,*iop_load,*iop_entry);
        }
        const std::array protected_inputs{std::filesystem::path(argv[1]), iop_image};
        diagnostic.attach(options, protected_inputs);
        const auto start = std::chrono::steady_clock::now();
        const auto report = checkpoint.run(state, memory, module, options, protected_inputs);
        diagnostic.finish(report, state, memory);
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
        std::cout << "Direct ELF bare-metal (no BIOS services), sha256 " << ps2native::sha256_hex(elf.bytes)
                  << "\n" << ps2native::boot_outcome_name(report.outcome) << " pc=0x" << std::hex << report.pc
                  << std::dec << " retired=" << report.retired << " aot=" << report.aot_instructions
                  << " interpreted=" << report.interpreted_instructions << " switches=" << report.engine_switches
                  << " runtime_ms=" << elapsed << '\n' << report.reason << '\n';
        checkpoint.print_execution_metrics(report);
        std::cout << "status=0x" << std::hex << state.cp0(ps2native::Cop0::Status)
                  << " cause=0x" << state.cp0(ps2native::Cop0::Cause) << " epc=0x" << state.cp0(ps2native::Cop0::Epc)
                  << " v1=0x" << state.reg32(3) << std::dec
                  << " gs_pixel_writes=" << memory.gs().vram().pixel_writes() << '\n';
        std::string trace="# ELF sha256 "+ps2native::sha256_hex(elf.bytes)+"\n";
        for (const auto& line:report.trace) trace+=line+'\n';
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
