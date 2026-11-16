#pragma once
#include "ps2/generated.hpp"
#include <map>
#include <deque>
#include <span>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace ps2native::engine {
class MissingAotEntry : public std::runtime_error {
public:
    explicit MissingAotEntry(std::uint32_t address) : std::runtime_error("Missing AOT entry"), pc(address) {}
    std::uint32_t pc;
};
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
    std::uint32_t translate(std::uint32_t address, std::size_t size, bool write) const;
    ps2::Cpu ee{};
    std::vector<std::uint8_t> ram, scratchpad, iop_ram;
    std::map<std::uint32_t, const ps2::UnitInfo*> aot;
    std::vector<const ps2::ModuleInfo*> aot_modules;
    std::string output;
    std::map<std::uint32_t,std::uint32_t> code_words;
    std::uint64_t ticks{};
    std::uint32_t stack_base{}, heap_end{};
    // shortcut: headless ELF settings start at zero; firmware sessions must supply the matched profile/NVM settings.
    std::uint32_t osd_config{};
    struct Timer { std::uint32_t mode{}, count{}, compare{}, hold{}; std::uint64_t base{}, due{~0ull}; };
    Timer timers[4];
    struct TlbEntry { std::uint32_t page_mask{}, entry_hi{}, entry_lo0{}, entry_lo1{}; bool defined{}; };
    TlbEntry tlb[48];
    struct InterruptHandler { std::uint32_t cause{}, function{}, gp{}; };
    std::map<std::uint32_t, InterruptHandler> handlers;
    std::uint32_t intc_mask{}, intc_status{}, dmac_status{};
    std::map<std::uint32_t,std::uint32_t> dma_registers;
    std::map<std::uint32_t,std::uint32_t> sif_registers;
    std::map<std::uint32_t,std::uint32_t> syscall_handlers;
    // HLE-owned IOP service region, not a hardware-mandated guest address.
    struct Sif {
        std::uint32_t command_buffer{}, main_address{}, ee_receive{}, msflag{}, smflag{};
        std::uint32_t transfer_id{}, completed_id{};
        std::map<std::uint32_t,std::uint32_t> tty_descriptors{{0,1},{1,2},{2,2}};
        bool armed{}, interrupt{}, in_interrupt{}, rpc_ready{};
        std::deque<std::vector<std::uint32_t>> replies;
    } sif;
    void initialize_sif();
    std::uint32_t sif_getreg(std::uint32_t reg);
    void sif_setreg(std::uint32_t reg, std::uint32_t value);
    std::uint32_t sif_dma(std::uint32_t descriptors, std::uint32_t count);
    void sif_command(std::span<const std::uint8_t> packet);
    void sif_pump();
    bool sif_irq_ready() const;
    void service_sif_irq();
    bool reschedule_requested{};
    bool exited{};
    int exit_code{};
};
Console& active_console();
}
