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
inline void run(const std::filesystem::path& tool, const std::vector<std::filesystem::path>& args,
                unsigned timeout_seconds = 15) {
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
    const auto job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        if (job) CloseHandle(job);
        throw std::runtime_error("Cannot bound test process tree");
    }
    if (!CreateProcessW(tool.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startup, &child)) {
        CloseHandle(job);
        throw std::runtime_error("Cannot start test tool (Win32 " + std::to_string(GetLastError()) + ")");
    }
    if (!AssignProcessToJobObject(job, child.hProcess)) {
        TerminateProcess(child.hProcess, 1); CloseHandle(child.hThread); CloseHandle(child.hProcess); CloseHandle(job);
        throw std::runtime_error("Cannot assign test process to bounded job");
    }
    ResumeThread(child.hThread);
    CloseHandle(child.hThread);
    const auto wait = WaitForSingleObject(child.hProcess, timeout_seconds * 1000u);
    if (wait != WAIT_OBJECT_0) { CloseHandle(job); WaitForSingleObject(child.hProcess, 5000); CloseHandle(child.hProcess); throw std::runtime_error("Test tool timed out"); }
    DWORD code = 1; const bool ok = GetExitCodeProcess(child.hProcess, &code) != 0;
    CloseHandle(child.hProcess);
    CloseHandle(job);
    if (!ok || code != 0) throw std::runtime_error("Test tool failed (exit " + std::to_string(code) + ")");
#else
    std::vector<std::string> owned{tool.native()};
    for (const auto& arg : args) owned.push_back(arg.native());
    std::vector<char*> argv;
    for (auto& value : owned) argv.push_back(value.data());
    argv.push_back(nullptr);
    pid_t child{};
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0) throw std::runtime_error("Cannot initialize test process attributes");
    if (posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) != 0 || posix_spawnattr_setpgroup(&attributes, 0) != 0) {
        posix_spawnattr_destroy(&attributes); throw std::runtime_error("Cannot bound test process group");
    }
    const auto error = posix_spawn(&child, tool.c_str(), nullptr, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    if (error)
        throw std::runtime_error("Cannot start test tool (errno " + std::to_string(error) + ")");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    int status{};
    for (;;) {
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child) break;
        if (result < 0) throw std::runtime_error("Cannot wait for test tool");
        if (std::chrono::steady_clock::now() >= deadline) { kill(-child, SIGKILL); waitpid(child, &status, 0); throw std::runtime_error("Test tool timed out"); }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) throw std::runtime_error("Test tool failed");
#endif
}
} // namespace io_test
