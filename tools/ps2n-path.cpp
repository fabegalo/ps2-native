// Configure-time host path helper; uses the same I/O boundary as the runtime.
#include "ps2native/io.hpp"
#include <iostream>
#include <stdexcept>

template<class Char> int path_main(int argc, Char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: ps2n-path alias|equivalent <path> <target>");
        const auto command = ps2native::path_utf8(std::filesystem::path(argv[1]));
        if (command == "alias") ps2native::ensure_directory_alias(argv[2], argv[3]);
        else if (command == "equivalent") {
            if (!ps2native::files_equivalent(argv[2], argv[3])) throw std::runtime_error("Paths refer to different files");
        } else throw std::runtime_error("Unknown path operation");
        return 0;
    } catch (const std::exception& error) { std::cerr << "ps2n-path: " << error.what() << '\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return path_main(argc, argv); }
#else
int main(int argc, char** argv) { return path_main(argc, argv); }
#endif
