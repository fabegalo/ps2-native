// PS2N_EE_HWWATCH=addr: a hardware write breakpoint on one guest RAM word.
// The first writes report the recompiled guest functions on the host stack,
// which finds the guest code behind a memory corruption.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <thread>

namespace {
std::vector<std::pair<uintptr_t, uint32_t>> g_functions; // host entry -> guest address, sorted
uintptr_t g_watch = 0;
uint32_t g_guest = 0;
std::atomic<int> g_hits{0};

uint32_t guest_function(uintptr_t host) {
    auto it = std::upper_bound(g_functions.begin(), g_functions.end(), std::make_pair(host, UINT32_MAX));
    return it == g_functions.begin() ? 0u : std::prev(it)->second;
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(info->ContextRecord->Dr6 & 1u))
        return EXCEPTION_CONTINUE_SEARCH;
    info->ContextRecord->Dr6 = 0;
    const int hit = ++g_hits;
    uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(g_watch), sizeof value);
    void* frames[48];
    const USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);
    std::fprintf(stderr, "[EE-hwwatch] %x=%x hit=%d guest:", g_guest, value, hit);
    uint32_t last = 0;
    for (USHORT i = 0; i < count; ++i) {
        const uint32_t guest = guest_function(reinterpret_cast<uintptr_t>(frames[i]));
        if (guest && guest != last) std::fprintf(stderr, " %x", guest);
        last = guest;
    }
    // PS2N_EE_HWDUMP=addr,*addr: words to print at each hit; *addr follows a pointer (8 words).
    if (const char* dump = std::getenv("PS2N_EE_HWDUMP")) {
        const auto* ram = reinterpret_cast<const uint8_t*>(g_watch - g_guest);
        const auto word = [&](uint32_t address) {
            uint32_t v = 0;
            std::memcpy(&v, ram + (address & 0x1FFFFFCu), sizeof v);
            return v;
        };
        for (const char* p = dump; *p;) {
            const bool follow = *p == '*';
            char* end = nullptr;
            const uint32_t address = static_cast<uint32_t>(std::strtoul(p + (follow ? 1 : 0), &end, 16));
            if (end == p) break;
            if (follow) {
                const uint32_t target = word(address);
                std::fprintf(stderr, " *%x=%x:", address, target);
                for (uint32_t i = 0; i < 8u; ++i) std::fprintf(stderr, " %x", word(target + 4u * i));
            } else {
                std::fprintf(stderr, " %x=%x", address, word(address));
            }
            p = *end == ',' ? end + 1 : end;
        }
    }
    std::fprintf(stderr, "\n");
    if (hit >= 200) info->ContextRecord->Dr7 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void arm_all_threads() {
    const DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry{sizeof entry};
    for (BOOL ok = Thread32First(snap, &entry); ok; ok = Thread32Next(snap, &entry)) {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) continue;
        HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &context) && context.Dr0 != g_watch) {
                context.Dr0 = g_watch;
                context.Dr7 = 1u | (1u << 16) | (3u << 18); // L0, break on write, 4 bytes
                SetThreadContext(thread, &context);
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
    }
    CloseHandle(snap);
}
} // namespace

void ps2n_hw_watch(uint8_t* rdram, uint32_t guest, std::vector<std::pair<uintptr_t, uint32_t>> functions) {
    std::sort(functions.begin(), functions.end());
    g_functions = std::move(functions);
    g_guest = guest & 0x1FFFFFCu;
    g_watch = reinterpret_cast<uintptr_t>(rdram + g_guest);
    AddVectoredExceptionHandler(1, on_exception);
    std::thread([] {
        while (g_hits < 200) {
            arm_all_threads();
            Sleep(20);
        }
    }).detach();
}
#else
void ps2n_hw_watch(uint8_t*, uint32_t, std::vector<std::pair<uintptr_t, uint32_t>>) {}
#endif
