#pragma once
#include "ps2/generated.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace ps2native::engine {
// Owns the console; the translator ABI only borrows its RAM while executing.
class Console {
public:
    Console();
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;
    void load(const std::filesystem::path& elf);
    void run();
    enum class ThreadState { Dormant, Ready, Running, Waiting };
    struct Thread {
        ps2::Cpu context{};
        ThreadState state{ThreadState::Dormant};
        std::uint32_t entry{}, stack{}, size{}, gp{}, priority{}, wait{};
        std::uint64_t order{};
    };
    struct Semaphore { int count{}, maximum{}; std::vector<std::uint32_t> waiters; };
    std::map<std::uint32_t, Thread> threads;
    std::map<std::uint32_t, Semaphore> semaphores;
    std::uint32_t current{1};
    std::uint64_t order{};
    void schedule();
    std::uint64_t now() const;
    void account();
    ps2::Cpu ee{};
    std::vector<std::uint8_t> ram, scratchpad, iop_ram;
    std::map<std::uint32_t, const ps2::UnitInfo*> aot;
    std::string output;
    std::map<std::uint32_t,std::uint32_t> code_words;
    std::uint64_t ticks{};
    std::uint32_t stack_base{}, heap_end{};
    struct Timer { std::uint32_t mode{}, count{}, compare{}, hold{}; std::uint64_t base{}, due{~0ull}; };
    Timer timers[4];
    struct InterruptHandler { std::uint32_t cause{}, function{}, gp{}; };
    std::map<std::uint32_t, InterruptHandler> handlers;
    std::uint32_t intc_mask{}, intc_status{}, dmac_status{};
    std::map<std::uint32_t,std::uint32_t> dma_registers;
    std::map<std::uint32_t,std::uint32_t> sif_registers;
    bool exited{};
    int exit_code{};
};
Console& active_console();
}
