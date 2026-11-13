// Include the implementation to inject a failing FILE without a production test hook.
#include "Kernel/Stubs/MemoryCard.cpp"
#include <memory>
#include <signal.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void writeTest(bool broken) {
    int ends[2];
#ifdef _WIN32
    require(_pipe(ends, 4096, _O_BINARY) == 0, "Could not create fixture pipe");
    const auto closeFd = [](int fd) { return _close(fd); };
    FILE* file = _fdopen(ends[1], "wb");
#else
    require(pipe(ends) == 0, "Could not create fixture pipe");
    const auto closeFd = [](int fd) { return close(fd); };
    FILE* file = fdopen(ends[1], "wb");
#endif
    require(file != nullptr, "Could not create fixture stream");
    std::array<char, 4096> buffer{};
    require(std::setvbuf(file, buffer.data(), _IOFBF, buffer.size()) == 0, "Could not buffer stream");
    struct Cleanup {
        FILE* file;
        int reader;
        ~Cleanup() {
            ps2_stubs::g_mcFiles.erase(1);
            std::fclose(file);
#ifdef _WIN32
            if (reader >= 0) _close(reader);
#else
            if (reader >= 0) close(reader);
#endif
        }
    } cleanup{file, ends[0]};
    if (broken) { closeFd(ends[0]); cleanup.reader = -1; }
    ps2_stubs::g_mcFiles[1] = {file, 0, {}};
    std::vector<uint8_t> ram(PS2_RAM_SIZE);
    for (unsigned i = 0; i < 16; ++i) ram[0x1000 + i] = static_cast<uint8_t>(i + 1);
    R5900Context context;
    SET_GPR_U32(&context, 4, 1);
    SET_GPR_U32(&context, 5, 0x1000);
    SET_GPR_U32(&context, 6, 16);
    ps2_stubs::sceMcWrite(ram.data(), &context, nullptr);
    const auto result = ps2_stubs::getMemoryCardDebugSnapshot().lastResult;
    if (broken) {
        require(result == -5, "Buffered flush failure was reported as a successful memory-card write");
        require(std::ferror(file) == 0, "Write error was not cleared for the next operation");
    } else {
        require(result == 16, "Successful memory-card write lost its byte count");
        std::array<uint8_t, 16> received{};
#ifdef _WIN32
        const int bytes = _read(ends[0], received.data(), static_cast<unsigned>(received.size()));
#else
        const int bytes = static_cast<int>(read(ends[0], received.data(), received.size()));
#endif
        require(bytes == 16 && std::memcmp(received.data(), ram.data() + 0x1000, 16) == 0,
                "Successful memory-card write changed its payload");
    }
}

int main() try {
#ifndef _WIN32
    const auto previous = signal(SIGPIPE, SIG_IGN);
#endif
    writeTest(false);
    writeTest(true);
#ifndef _WIN32
    signal(SIGPIPE, previous);
#endif
    std::cout << "PASS memory-card buffered write payload and flush failure\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
