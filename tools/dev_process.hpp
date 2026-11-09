// BSD-3-Clause. Native argument vectors, bounded process trees, no shell.
#pragma once
#include "ps2native/io.hpp"
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>
#include <algorithm>
#include <functional>
#include <exception>
#include <string_view>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
extern char** environ;
#endif

namespace ps2native::dev {
struct ProcessResult { int exit_code{}; bool timed_out{},cancelled{}; double milliseconds{}; };
inline ProcessResult process(const std::filesystem::path& executable, const std::vector<std::filesystem::path>& args,
                             const std::filesystem::path& log, unsigned timeout_seconds,std::uint64_t max_log_bytes=0,
                             const std::function<bool()>& cancelled={},
                             const std::function<void(std::string_view)>& on_output={}) {
    const auto start = std::chrono::steady_clock::now();
    if(!log.empty())ps2native::create_directories(log.parent_path());
    ProcessResult result;
#ifdef _WIN32
    auto quote = [](const std::wstring& value) {
        std::wstring out = L"\""; std::size_t slashes = 0;
        for (auto c : value) {
            if (c == L'\\') { ++slashes; continue; }
            out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            slashes = 0; out += c;
        }
        out.append(slashes * 2, L'\\'); return out + L'"';
    };
    std::wstring command = quote(executable.native());
    for (const auto& arg : args) command += L" " + quote(arg.native());
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const auto log_handle = CreateFileW(log.empty()?L"NUL":native_io_path(log).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                   log.empty()?OPEN_EXISTING:CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_handle == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open process log");
    HANDLE pipe_read{}, output{};
    if (!CreatePipe(&pipe_read,&output,&security,0) || !SetHandleInformation(pipe_read,HANDLE_FLAG_INHERIT,0)) {
        if(pipe_read)CloseHandle(pipe_read);if(output)CloseHandle(output);
        CloseHandle(log_handle);throw std::runtime_error("cannot create process output pipe");
    }
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow=SW_HIDE;
    startup.StartupInfo.hStdOutput = output; startup.StartupInfo.hStdError = output;
    startup.StartupInfo.hStdInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    SIZE_T attribute_bytes{}; InitializeProcThreadAttributeList(nullptr,1,0,&attribute_bytes);
    std::vector<unsigned char> attribute_storage(attribute_bytes);
    startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    HANDLE inherited[]{output,startup.StartupInfo.hStdInput};
    const bool attributes_initialized=InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&attribute_bytes)!=0;
    if (!attributes_initialized ||
        !UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)) {
        if(attributes_initialized)DeleteProcThreadAttributeList(startup.lpAttributeList);
        CloseHandle(output);CloseHandle(pipe_read);CloseHandle(log_handle);CloseHandle(startup.StartupInfo.hStdInput);throw std::runtime_error("cannot isolate process handles");
    }
    const auto job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        if (job) CloseHandle(job); CloseHandle(output); CloseHandle(startup.StartupInfo.hStdInput);
        CloseHandle(pipe_read);CloseHandle(log_handle);
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        throw std::runtime_error("cannot bound process tree");
    }
    PROCESS_INFORMATION child{};
    // Keep the compiler probe and Ninja on the same console code page, hidden from desktop users.
    const auto created=CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NEW_CONSOLE | EXTENDED_STARTUPINFO_PRESENT,
                        nullptr, nullptr, &startup.StartupInfo, &child);
    const auto launch_error=GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!created) {
        CloseHandle(job); CloseHandle(output); CloseHandle(startup.StartupInfo.hStdInput);
        CloseHandle(pipe_read);CloseHandle(log_handle);
        throw std::runtime_error("cannot start process (Win32 " + std::to_string(launch_error) + ")");
    }
    CloseHandle(output); CloseHandle(startup.StartupInfo.hStdInput);
    if (!AssignProcessToJobObject(job, child.hProcess)) {
        TerminateProcess(child.hProcess, 1); WaitForSingleObject(child.hProcess, 5000);
        CloseHandle(child.hThread); CloseHandle(child.hProcess); CloseHandle(job);
        CloseHandle(pipe_read);CloseHandle(log_handle);
        throw std::runtime_error("cannot assign bounded process job");
    }
    if(ResumeThread(child.hThread)==static_cast<DWORD>(-1)) {
        TerminateJobObject(job,1);WaitForSingleObject(child.hProcess,5000);
        CloseHandle(child.hThread);CloseHandle(child.hProcess);CloseHandle(job);CloseHandle(pipe_read);CloseHandle(log_handle);
        throw std::runtime_error("cannot resume child process");
    }
    CloseHandle(child.hThread);
    bool log_failed=false;std::uint64_t log_bytes=0;std::exception_ptr output_error;
    const auto drain=[&] {
        char buffer[16384];DWORD available{};
        unsigned chunks=0;
        while(chunks++<64 && PeekNamedPipe(pipe_read,nullptr,0,nullptr,&available,nullptr) && available) {
            DWORD got{},written{};
            if(!ReadFile(pipe_read,buffer,std::min<DWORD>(sizeof(buffer),available),&got,nullptr))break;
            if(max_log_bytes && (log_bytes>=max_log_bytes || got>max_log_bytes-log_bytes)){log_failed=true;continue;}
            if(!WriteFile(log_handle,buffer,got,&written,nullptr) || written!=got)log_failed=true;
            if(on_output && !output_error)try{on_output(std::string_view(buffer,got));}
                catch(...){output_error=std::current_exception();log_failed=true;}
            log_bytes+=got;
        }
    };
    const auto deadline=start+std::chrono::seconds(timeout_seconds);
    for(;;) {
        drain();const auto waited=WaitForSingleObject(child.hProcess,10);
        if(waited==WAIT_OBJECT_0)break;
        result.cancelled=cancelled && cancelled();
        if(result.cancelled || log_failed || waited==WAIT_FAILED || std::chrono::steady_clock::now()>=deadline) {
            result.timed_out=!result.cancelled;TerminateJobObject(job,124);WaitForSingleObject(child.hProcess,5000);break;
        }
    }
    drain();
    DWORD code = 1; GetExitCodeProcess(child.hProcess, &code);
    result.exit_code = result.cancelled ? 130 : result.timed_out ? 124 : static_cast<int>(code);
    CloseHandle(child.hProcess); CloseHandle(job);
    drain();CloseHandle(pipe_read);CloseHandle(log_handle);
    if(output_error)std::rethrow_exception(output_error);
    if(log_failed)throw std::runtime_error("process log exceeded its limit or could not be written");
#else
    if(on_output || log.empty())throw std::runtime_error("streamed process output requires the Windows build adapter");
    const auto output = open(native_io_path(log).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output < 0) throw std::runtime_error("cannot open process log");
    std::vector<std::string> owned{executable.native()};
    for (const auto& arg : args) owned.push_back(arg.native());
    std::vector<char*> argv; for (auto& value : owned) argv.push_back(value.data()); argv.push_back(nullptr);
    posix_spawn_file_actions_t actions; posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions)) { close(output); throw std::runtime_error("cannot initialize process actions"); }
    if (posix_spawnattr_init(&attributes)) { posix_spawn_file_actions_destroy(&actions); close(output); throw std::runtime_error("cannot initialize process attributes"); }
    int error = posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
    error |= posix_spawn_file_actions_adddup2(&actions, output, STDERR_FILENO);
    error |= posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (output > STDERR_FILENO) error |= posix_spawn_file_actions_addclose(&actions, output);
    error |= posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    error |= posix_spawnattr_setpgroup(&attributes, 0);
    pid_t child{};
    if (!error) error = posix_spawnp(&child, executable.c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes); close(output);
    if (error) throw std::runtime_error("cannot spawn process (errno " + std::to_string(error) + ")");
    const auto deadline = start + std::chrono::seconds(timeout_seconds);
    int status{};
    bool log_limit=false;
    for (;;) {
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0 && errno != EINTR) { kill(-child,SIGKILL); waitpid(child,&status,0); throw std::runtime_error("cannot wait for process"); }
        std::error_code log_error;
        log_limit=max_log_bytes && std::filesystem::file_size(log,log_error)>=max_log_bytes && !log_error;
        result.cancelled=cancelled && cancelled();
        if (result.cancelled || log_limit || std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = !result.cancelled; kill(-child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Do not leave grandchildren running after their direct parent exits.
    kill(-child, SIGKILL);
    result.exit_code = result.cancelled ? 130 : result.timed_out ? 124 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if(max_log_bytes) {
        std::error_code error;
        const auto bytes=std::filesystem::file_size(log,error);
        if(!error && bytes>max_log_bytes) {
            log_limit=true;std::filesystem::resize_file(log,max_log_bytes,error);
            if(error)throw std::runtime_error("process log exceeded its limit and could not be bounded");
        }
    }
    if(log_limit)throw std::runtime_error("process log exceeded its limit");
#endif
    result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
} // namespace ps2native::dev
