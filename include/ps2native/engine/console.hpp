#pragma once
#include "ps2/generated.hpp"
#include "iop_cpu.h"
#include "iop_memory.h"
#include "ps2native/engine/vu.hpp"
#include "gs/gs.hpp"
#include "vif.hpp"
#include "gs/gif.hpp"
#include <map>
#include <optional>
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
class IopCdvd;
class Console {
public:
    Console();
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;
    void load(const std::filesystem::path& elf);
    void run();
    void reset_iop();
    std::deque<std::pair<std::string,std::vector<std::uint8_t>>> iop_boot_modules;
    std::uint32_t iop_boot_waiting{};
    bool iop_boot_active{};
    void iop_boot_step();
    std::filesystem::path iop_rom0;
    std::array<std::uint32_t,24> iop_sio2_send{};
    std::map<std::uint32_t,std::uint32_t> iop_bus_registers;
    struct IopModuleWait {std::uint32_t thread{},result{};std::string library;std::uint32_t ordinal{},version{};};
    std::map<std::uint32_t,std::vector<IopModuleWait>> iop_module_waiters;
    std::array<std::uint32_t,4> iop_modload_callbacks{};
    struct IopTimer {std::uint32_t users{},mode{},count{},compare{},prescale{1};std::uint64_t base{};};
    std::array<IopTimer,6> iop_timers{};
    std::uint32_t iop_timer_counter(unsigned index) const;
    struct IopVblankHandler {std::uint32_t edge{},priority{},function{},argument{},gp{};std::uint64_t order{};};
    std::vector<IopVblankHandler> iop_vblank_handlers;
    std::deque<IopVblankHandler> iop_vblank_jobs;
    std::optional<IopVblankHandler> iop_vblank_active;
    std::uint32_t iop_vblank_pending{};
    bool iop_in_vblank{};
    void iop_vblank_edge(unsigned edge);
    void advance_to(std::uint64_t target);
    void load_iop(const std::filesystem::path& irx);
    std::uint32_t load_iop(std::span<const std::uint8_t> file,std::string origin);
    struct IopModule {std::uint32_t base{},size{},entry{},gp{},thread{};std::string origin;std::int32_t result{};bool returned{},resident{};};
    struct IopLibrary {std::uint32_t address{},version{},owner{};std::vector<std::uint32_t> exports;};
    std::map<std::uint32_t,IopModule> iop_modules;
    std::map<std::string,IopLibrary> iop_libraries;
    std::map<std::uint32_t,std::uint32_t> iop_allocations;
    std::uint32_t iop_allocate(std::uint32_t bytes);
    void iop_release(std::uint32_t address);
    bool iop_loader_import(const std::string& library,std::uint32_t ordinal,std::uint32_t version=0);
    void iop_module_return();
    void boot_cdvd(const std::filesystem::path& rom0,bool extended=true);
    std::unique_ptr<IopCdvd> cdvd;
    bool iop_irq_active{};
    struct RpcServer {std::uint32_t address{},sid{},function{},buffer{},callback{},callback_buffer{},queue{},gp{};std::uint64_t completions{};};
    struct RpcRequest {std::vector<std::uint32_t> packet;std::vector<std::uint8_t> payload;};
    struct RpcQueue {std::uint32_t thread{};std::deque<RpcRequest> requests;};
    struct RpcCall {RpcRequest request;ps2x::iop::detail::IopCpuState saved;std::uint32_t queue{};};
    std::optional<std::uint32_t> rpc_missing_server;
    std::map<std::uint32_t,RpcServer> rpc_servers;
    std::map<std::uint32_t,RpcQueue> rpc_queues;
    std::map<std::uint32_t,RpcCall> rpc_calls;
    struct IopNativeCall {ps2x::iop::detail::IopCpuState saved;std::function<void(std::uint32_t)> finish;};
    std::map<std::uint32_t,std::vector<IopNativeCall>> iop_native_calls;
    std::map<std::string,std::uint32_t> iop_drivers;
    struct IopIrq {std::uint32_t function{},argument{},gp{};bool enabled{};};
    std::map<std::uint32_t,IopIrq> iop_irqs;
    bool iop_intr_enabled{true};
    void iop_native_call(std::uint32_t function,std::uint32_t argument,std::function<void(std::uint32_t)> finish);
    bool iop_services_import(const std::string& library,std::uint32_t ordinal);
    bool iop_rpc_import(const std::string& library,std::uint32_t ordinal);
    bool rpc_command(std::span<const std::uint8_t> packet);
    void rpc_next(std::uint32_t queue);
    void rpc_finish();

    void step_iop();
    void run_iop();
    std::uint32_t iop_read(std::uint32_t address,unsigned bytes) const;
    void iop_write(std::uint32_t address,std::uint32_t value,unsigned bytes);
    std::string iop_string(std::uint32_t address,std::size_t limit=4096) const;
    std::uint32_t iop_arg(unsigned index) const;
    std::string iop_format(std::uint32_t address,unsigned first);
    void iop_import(const std::string& library,std::uint32_t ordinal,std::uint32_t version=0);
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
    std::map<std::uint32_t,std::uint16_t> iop_import_versions;
    struct IopThread {
        ps2x::iop::detail::IopCpuState context{};
        std::uint32_t status{16},attr{},option{},entry{},stack{},size{},gp{},initial_priority{},priority{};
        std::uint32_t wait{},wait_id{},wakeups{},wait_bits{},wait_mode{},wait_pointer{},module{};
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
        std::uint32_t entry{}, stack{}, size{}, gp{}, priority{}, wait{}, initial_priority{}, attr{}, option{};
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
    std::uint64_t ticks{},execution_limit{100000000};
    std::uint32_t stack_base{}, heap_end{};
    // shortcut: headless ELF settings start at zero; firmware sessions must supply the matched profile/NVM settings.
    std::uint32_t osd_config{};
    struct Timer { std::uint32_t mode{}, count{}, compare{}, hold{}; std::uint64_t base{}, due{~0ull}; };
    Timer timers[4];
    std::uint32_t ee_timer_count(unsigned index) const;
    void refresh_ee_timer(unsigned index);
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
