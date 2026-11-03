// Shell-free, bounded child processes for CLI regression tests. No shell quoting
// or current-directory mutation; arguments retain the host's native encoding.
#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>
extern char** environ;
#endif
namespace io_test {
inline void run(const std::filesystem::path& tool, const std::vector<std::filesystem::path>& args) {
#ifdef _WIN32
    auto quote = [](const std::wstring& value) {
        std::wstring out = L"\"";
        std::size_t slashes = 0;
        for (auto c : value) {
            if (c == L'\\') { ++slashes; continue; }
            if (c == L'"') out.append(slashes * 2 + 1, L'\\');
            else out.append(slashes, L'\\');
            slashes = 0; out += c;
        }
        out.append(slashes * 2, L'\\'); return out + L'"';
    };
    std::wstring command = quote(tool.native());
    for (const auto& arg : args) command += L" " + quote(arg.native());
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(tool.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &child))
        throw std::runtime_error("Cannot start test tool (Win32 " + std::to_string(GetLastError()) + ")");
    CloseHandle(child.hThread);
    const auto wait = WaitForSingleObject(child.hProcess, 15000);
    if (wait != WAIT_OBJECT_0) { TerminateProcess(child.hProcess, 1); WaitForSingleObject(child.hProcess, 5000); CloseHandle(child.hProcess); throw std::runtime_error("Test tool timed out"); }
    DWORD code = 1; const bool ok = GetExitCodeProcess(child.hProcess, &code) != 0;
    CloseHandle(child.hProcess);
    if (!ok || code != 0) throw std::runtime_error("Test tool failed (exit " + std::to_string(code) + ")");
#else
    std::vector<std::string> owned{tool.native()};
    for (const auto& arg : args) owned.push_back(arg.native());
    std::vector<char*> argv;
    for (auto& value : owned) argv.push_back(value.data());
    argv.push_back(nullptr);
    pid_t child{};
    if (const auto error = posix_spawn(&child, tool.c_str(), nullptr, nullptr, argv.data(), environ); error)
        throw std::runtime_error("Cannot start test tool (errno " + std::to_string(error) + ")");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    int status{};
    for (;;) {
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child) break;
        if (result < 0) throw std::runtime_error("Cannot wait for test tool");
        if (std::chrono::steady_clock::now() >= deadline) { kill(child, SIGKILL); waitpid(child, &status, 0); throw std::runtime_error("Test tool timed out"); }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) throw std::runtime_error("Test tool failed");
#endif
}
} // namespace io_test
