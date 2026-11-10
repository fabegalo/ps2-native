// Disc access and the process supervisor used by ps2n. Synthetic content only.
#include "iso_image.hpp"
#include "ps2native/disc.hpp"
#include "ps2native/io.hpp"
#include "ps2native/process.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace ps2native;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void disc_formats(const fs::path& dir) {
    for (const unsigned mode : {0u, 1u, 2u}) {
        iso_test::IsoOptions options;
        options.raw_mode = mode;
        const auto image = dir / ("disc" + std::to_string(mode) + ".img");
        write_file_atomically(image, iso_test::build_iso(options));
        DiscImage disc(image);
        require(disc.raw_sectors() == (mode != 0), "raw sector detection");
        require(disc.volume_id() == "PS2N_TEST", "volume id");
        require(disc.boot_path() == "\\SLUS_000.01;1", "BOOT2 path");
        const auto readme = disc.read("README.TXT");
        require(std::string(readme.begin(), readme.end()) == "test", "file contents");
        require(disc.list().size() == 2, "root listing");
    }
}

void processes(const fs::path& self, const fs::path& dir) {
    const auto log = dir / "child.log";
    require(run_process(self, {"--child-exit", "7"}, dir, log) == 7, "child exit code");
    std::string seen;
    const auto started = std::chrono::steady_clock::now();
    run_process(self, {"--child-marker"}, dir, log, [&](std::string_view text) {
        seen.append(text);
        return seen.find("marker") == seen.npos;
    });
    require(seen.find("marker") != seen.npos, "observer saw the child output");
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(20), "observer stopped the child");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--child-exit") return std::stoi(argv[2]);
        if (argc == 2 && std::string(argv[1]) == "--child-marker") {
            std::cout << "marker" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(60));
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--write-iso") {  // launcher UI test fixture
            ps2native::create_directories(fs::absolute(argv[2]).parent_path());
            write_file_atomically(argv[2],iso_test::build_iso({}));
            return 0;
        }
        if (argc != 2) return 2;
        const fs::path dir = argv[1];
        ps2native::create_directories(dir);
        disc_formats(dir);
        processes(fs::absolute(argv[0]), dir);
        std::cout << "content tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "content tests failed: " << error.what() << "\n";
        return 1;
    }
}
