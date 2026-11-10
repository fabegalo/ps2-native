// Host entry point of a game recompiled by PS2Recomp: the runtime, kernel,
// IOP, GS and audio are the pinned PS2Recomp ps2xRuntime, used unchanged.
//
//   ps2n-game-<id> <boot-elf> <cd-image>
// The working directory is the extracted disc (cdrom0:); <cd-image> holds the
// 2048-byte user sectors for raw LBN reads.
#include "ps2_runtime.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << (argc ? argv[0] : "ps2n-game") << " <boot-elf> <cd-image>\n";
        return 2;
    }
    try {
        const std::filesystem::path elf = std::filesystem::absolute(argv[1]);
        PS2Runtime runtime;
        if (!runtime.initialize(("ps2-native | " + elf.filename().string()).c_str())) {
            std::cerr << "PS2Recomp runtime initialization failed\n";
            return 1;
        }
        if (!runtime.loadELF(elf.string())) {
            std::cerr << "PS2Recomp could not load " << elf.string() << "\n";
            return 1;
        }
        auto paths = PS2Runtime::getIoPaths();
        paths.cdImage = std::filesystem::absolute(argv[2]);
        PS2Runtime::setIoPaths(paths);
        runtime.run();
    } catch (const std::exception& error) {
        std::cerr << "ps2n-game: " << error.what() << "\n";
        std::cerr.flush();
        std::_Exit(1);
    }
    // Same exit path as the PS2Recomp runner: guest threads are not joined.
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(0);
}
