#include "ps2native/process.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ps2native {
namespace {
// Feeds the observer with text appended to the log since the previous call.
class LogFollower {
public:
    explicit LogFollower(const std::filesystem::path& log) : log_(log) {}
    bool poll(const ProcessObserver& observer) {
        if (!observer) return true;
        std::ifstream in(log_, std::ios::binary);
        if (!in) return true;
        in.seekg(static_cast<std::streamoff>(offset_));
        std::string chunk(64 * 1024, '\0');
        for (;;) {
            in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto count = static_cast<std::size_t>(in.gcount());
            if (count == 0) return true;
            offset_ += count;
            if (!observer(std::string_view(chunk.data(), count))) return false;
        }
    }
private:
    std::filesystem::path log_;
    std::uint64_t offset_{};
};
} // namespace

#ifdef _WIN32
namespace {
std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) throw std::runtime_error("argument is not valid UTF-8");
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}
// CommandLineToArgvW quoting rules.
void append_quoted(std::wstring& line, const std::wstring& argument) {
    line += L'"';
    std::size_t backslashes = 0;
    for (const auto c : argument) {
        if (c == L'\\') { ++backslashes; continue; }
        line.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        line += c;
    }
    line.append(backslashes * 2, L'\\');
    line += L'"';
}
} // namespace

int run_process(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                const std::filesystem::path& working_directory, const std::filesystem::path& log,
                const ProcessObserver& observer) {
    std::wstring line;
    append_quoted(line, executable.wstring());
    for (const auto& argument : arguments) {
        line += L' ';
        append_quoted(line, widen(argument));
    }
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE output = CreateFileW(log.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create process log");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(executable.wstring().c_str(), line.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                        working_directory.wstring().c_str(), &startup, &process);
    CloseHandle(output);
    if (!started) throw std::runtime_error("cannot start " + executable.filename().string() +
                                           " (Windows error " + std::to_string(GetLastError()) + ")");
    CloseHandle(process.hThread);
    LogFollower follower(log);
    bool stopped = false;
    while (WaitForSingleObject(process.hProcess, 200) == WAIT_TIMEOUT) {
        if (!stopped && !follower.poll(observer)) {
            TerminateProcess(process.hProcess, 1);
            stopped = true;
        }
    }
    if (!stopped) follower.poll(observer);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
#else
int run_process(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                const std::filesystem::path& working_directory, const std::filesystem::path& log,
                const ProcessObserver& observer) {
    const int output = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (output < 0) throw std::runtime_error("cannot create process log");
    std::vector<std::string> owned{executable.string()};
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& argument : owned) argv.push_back(argument.data());
    argv.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) { close(output); throw std::runtime_error("cannot fork"); }
    if (child == 0) {
        if (chdir(working_directory.c_str()) != 0) _exit(127);
        dup2(output, STDOUT_FILENO);
        dup2(output, STDERR_FILENO);
        execv(argv[0], argv.data());
        _exit(127);
    }
    close(output);
    LogFollower follower(log);
    bool stopped = false;
    int status = 0;
    for (;;) {
        const pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) break;
        if (!stopped && !follower.poll(observer)) {
            kill(child, SIGKILL);
            stopped = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!stopped) follower.poll(observer);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
#endif
} // namespace ps2native
