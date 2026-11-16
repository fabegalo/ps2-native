#pragma once
#include "ps2/generated.hpp"
#include "iop_cpu.h"
#include "iop_memory.h"
#include "ps2native/engine/vu.hpp"
#include "gs/gs.hpp"
#include "vif.hpp"
#include "gs/gif.hpp"
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
    struct IopThread {
        ps2x::iop::detail::IopCpuState context{};
        std::uint32_t status{16},attr{},option{},entry{},stack{},size{},gp{},initial_priority{},priority{};
        std::uint32_t wait{},wait_id{},wakeups{},wait_bits{},wait_mode{},wait_pointer{};
        std::uint64_t order{},deadline{~0ull};
        bool delete_on_exit{};
    };
    std::map<std::uint32_t,IopThread> iop_threads;
    std::uint32_t iop_current{},iop_next_thread{1},iop_dispatch_depth{};
    std::uint64_t iop_order{};
    bool iop_reschedule{},iop_module_returned{};
    bool iop_work_pending() const;
    void iop_schedule();
    void iop_ready(std::uint32_t id,std::int32_t result=0);
    void iop_block(std::uint32_t kind,std::uint32_t object=0);
    bool iop_thread_import(const std::string& library,std::uint32_t ordinal);
    struct IopObject {
        std::uint32_t attr{},option{},initial{},value{},maximum{};
        std::vector<std::uint32_t> waiters;
        std::deque<std::uint32_t> messages;
    };
    std::map<std::uint32_t,IopObject> iop_semas,iop_events,iop_mailboxes;
    std::uint32_t iop_next_object{1};
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
    std::array<ps2::vif::Unit,2> vif_state{};
    std::array<ps2::gif::Path,4> gif_paths{},gif_scans{};
    ps2::gif::Held gif_held{};
    std::uint32_t gif_status{};
    struct GraphicDma {bool tag_end{},paused{};};
    std::array<GraphicDma,10> graphic_dma{};
    std::array<std::uint64_t,32> gs_private{};
    std::array<std::uint64_t,2> gs_display{},gs_buffers{};
    bool graphics_initialized{};
    void initialize_graphics();
    void graphics_vblank();
    std::uint64_t next_event_tick() const;
    void graphics_dma(unsigned channel);
    void graphics_dma_resume(unsigned channel);
    ps2::gs::State gs_state{};
    std::vector<std::uint8_t> gs_vram=std::vector<std::uint8_t>(4u<<20);
    ps2::GsHostOptions gs_options{};
    std::uint32_t gs_crt_mode{},host_video_options{};
    std::uint64_t graphics_frames{};
    std::uint32_t gs_finish_pending{},gs_finish_queued{};
    bool gs_odd_field{};
    std::map<int,std::pair<std::uint64_t,std::function<void()>>> graphics_events;
    ps2::Cpu ee{};
    std::vector<std::uint8_t> ram, scratchpad, iop_ram;
    std::array<std::uint8_t,4096> vu0_memory{};
    std::uint32_t vu_vi24{};
    ps2::Vu0Regs vu1_regs{};
    std::array<std::uint8_t,4096> vu0_code{};
    std::array<std::uint8_t,16384> vu1_code{},vu1_memory{};
    std::array<VuExecution,2> vu{};
    std::array<std::uint64_t,2> vu_aot_pairs{},vu_interpreted_pairs{};
    std::vector<const VuProgram*> vu_programs;
    std::array<const VuProgram*,2> vu_selected{};
    void start_vu(unsigned unit,std::uint32_t address);
    void step_vu(unsigned unit);
    void wait_vu(unsigned unit);
    void invalidate_vu(unsigned unit);

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
    struct InterruptHandler { std::uint32_t cause{}, function{}, gp{}, argument{}; };
    std::map<std::uint32_t, InterruptHandler> handlers;
    std::vector<std::uint32_t> interrupt_order;
    bool in_interrupt{};
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
        bool armed{}, interrupt{}, rpc_ready{};
        std::deque<std::vector<std::uint32_t>> replies;
    } sif;
    void initialize_sif();
    std::uint32_t sif_getreg(std::uint32_t reg);
    void sif_setreg(std::uint32_t reg, std::uint32_t value);
    std::uint32_t sif_dma(std::uint32_t descriptors, std::uint32_t count);
    void sif_command(std::span<const std::uint8_t> packet);
    void sif_pump();
    bool interrupt_ready() const;
    void service_interrupts();
    bool reschedule_requested{};
    bool exited{};
    int exit_code{};
};
Console& active_console();
}
