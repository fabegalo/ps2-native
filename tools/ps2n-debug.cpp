#include "diagnostic_cli.hpp"
#include <deque>
#include <fstream>

template<class C> int debug_main(int argc, C** argv) {
    using namespace ps2native;
    try {
        if (argc < 3) {
            std::cout << "Usage: ps2n-debug report <diagnostic-directory>\n"
                         "       ps2n-debug compare <events.ndjson> <events.ndjson>\n"
                         "       ps2n-debug log <legacy-log> [ring-size]\n"
                         "Capture: ps2n-run / ps2n-boot ... --diagnostic-dir=DIR [--diagnostic-ring=N]\n"
                         "         [--diagnostic-stream-bytes=N] (explicit full event stream)\n";
            return 2;
        }
        const auto command = path_utf8(std::filesystem::path(argv[1]));
        if (command == "report" && argc == 3) {
            const auto bytes = read_file(std::filesystem::path(argv[2]) / "report.txt");
            std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return 0;
        }
        if (command == "compare" && argc == 4) {
            std::string explanation;
            const auto result = compare_diagnostics(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]), explanation);
            std::cout << explanation << '\n'; return result;
        }
        if (command == "log" && (argc == 3 || argc == 4)) {
            const auto capacity = argc == 4 ? cli::decimal(path_utf8(std::filesystem::path(argv[3])), 1000000) : 32;
            if (!capacity) throw std::runtime_error("log ring must be nonzero");
            std::ifstream input(native_io_path(std::filesystem::path(argv[2])), std::ios::binary);
            if (!input) throw std::runtime_error("cannot read legacy log");
            std::deque<std::string> ring; std::string line, blocker; std::uint64_t lines = 0, stop_line = 0;
            while (std::getline(input, line)) {
                ++lines;
                // Only explicit stop records are facts. Do not infer deadlocks
                // from repeated PCs, warning words, or inactive peripherals.
                if (blocker.empty() && (line.starts_with("STOP ") || line.starts_with("# block point ") ||
                    line.starts_with("block point pc=") || line.starts_with("IOP_STOP=") ||
                    (line.starts_with("DIAGNOSTIC ONLY developer ELF injection: ") && line.find(" pc=")!=std::string::npos && line.find(" retired=")!=std::string::npos))) {
                    blocker = line.substr(0,8192); stop_line = lines;
                }
                if (ring.size() == capacity) ring.pop_front();
                ring.push_back(line.substr(0, 8192));
            }
            if (input.bad()) throw std::runtime_error("legacy log read failed");
            std::cout << "{\"schema\":\"ps2n-log-summary-v1\",\"lines\":" << lines << ",\"first_explicit_stop_line\":"
                      << stop_line << ",\"stop\":" << (blocker.empty() ? "null" : json_string(blocker)) << ",\"tail\":[";
            bool comma = false;
            for (const auto& entry : ring) { if (comma) std::cout << ','; comma = true; std::cout << json_string(entry); }
            std::cout << "]}\n"; return 0;
        }
        throw std::runtime_error("unknown command or argument count");
    } catch (const std::exception& e) { std::cerr << "ps2n-debug: " << e.what() << '\n'; return 2; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return debug_main(argc, argv); }
#else
int main(int argc, char** argv) { return debug_main(argc, argv); }
#endif
