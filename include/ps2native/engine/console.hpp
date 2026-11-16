#pragma once
#include "ps2/generated.hpp"
#include "iop_cpu.h"
#include "iop_memory.h"
#include <map>
#include <deque>
#include <span>
#include <stdexcept>
#include <memory>
#include <string>
#include <string_view>
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
    void advance_to(std::uint64_t target);
    void load_iop(const std::filesystem::path& irx);
    void step_iop();
    void run_iop();
    std::uint32_t iop_read(std::uint32_t address,unsigned bytes) const;
    void iop_write(std::uint32_t address,std::uint32_t value,unsigned bytes);
    std::string iop_string(std::uint32_t address,std::size_t limit=4096) const;
    std::uint32_t iop_arg(unsigned index) const;
    std::string iop_format(std::uint32_t address,unsigned first);
    void iop_import(const std::string& library,std::uint32_t ordinal);
    void tty_write(bool from_iop,std::string_view text);
    ps2x::iop::detail::IopCpuState iop{};
    ps2x::iop::detail::IopMemory iop_bus;
    ps2x::iop::detail::IopCpuCore iop_core;
    std::uint64_t iop_next_tick{}, iop_instructions{};
    bool iop_active{}, advancing{}, iop_callback_active{}, iop_suspended_active{};
    ps2x::iop::detail::IopCpuState iop_suspended{};
    std::uint64_t iop_first_tick{}, iop_last_tick{};
    void iop_send_command(std::uint32_t cid,std::uint32_t packet,std::uint32_t size,
                          std::uint32_t source,std::uint32_t destination,std::uint32_t extra);
    std::uint32_t iop_heap_top{0x10000}, iop_strtok_next{}, iop_import_slot{};
    std::map<std::uint32_t,std::pair<std::string,std::uint32_t>> iop_imports;
    std::string iop_output;
    int iop_result{};
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
    std::array<std::uint8_t,4096> vu0_memory{};
    std::uint32_t vu_vi24{};
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
        struct IopHandler { std::uint32_t entry{},argument{},gp{}; };
        std::map<std::uint32_t,IopHandler> iop_handlers;
        std::deque<std::pair<IopHandler,std::vector<std::uint8_t>>> iop_commands;
        std::array<std::uint32_t,32> iop_sregs{};
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
