#pragma once
#include "ps2native/ee_registers.hpp"
#include "ps2native/elf.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace ps2native {
// Physical EE bus regions. Main RAM, the BIOS ROM and the scratchpad are
// backed by storage; EE register regions (0x10000000..0x1000FFFF) go to
// EeRegisters, which models some registers and refuses the rest; every other
// device region fails with a named diagnostic.
enum class BusRegion : std::uint8_t {
    MainRam, BiosRom, Scratchpad,
    EeTimers, Ipu, Gif, Vif0, Vif1, EeFifos, Dmac, Intc, Sio, Sif, Mch, DmacExtended, EeRegisters,
    VuMemory, GsRegisters, IopMemory, Rom1, Rom2, IopRegisters, Block1A, Unmapped
};
[[nodiscard]] const char* bus_region_name(BusRegion region) noexcept;
[[nodiscard]] BusRegion classify_physical(std::uint32_t physical_address) noexcept;

enum class BusFault : std::uint8_t {
    Unaligned,          // address error exception (not vectored yet)
    Privilege,          // address segment is inaccessible in the current CPU mode
    OutsideProgramRam,  // ELF program mapping: address outside EE main RAM
    TlbRefill,          // no TLB entry matches
    TlbInvalid,         // matching entry with V = 0
    TlbModified,        // store through an entry with D = 0
    TlbMultipleMatch,   // machine check
    KusegWithErl,       // kuseg while Status.ERL = 1 (unmapped kuseg not modeled)
    Device,             // MMIO or bus region that is not implemented
    RomWrite,           // store to the BIOS ROM
    Unmapped            // no device decodes this physical address
};
[[nodiscard]] const char* bus_fault_name(BusFault fault) noexcept;

// Every refused access is reported with what, where and why; none is ignored.
class BusError : public std::runtime_error {
public:
    BusError(BusFault fault, bool write, std::uint8_t bytes, std::uint32_t virtual_address,
             std::optional<std::uint32_t> physical_address, BusRegion region, const std::string& message);
    BusFault fault;
    bool write;
    std::uint8_t bytes;
    std::uint32_t virtual_address;
    std::optional<std::uint32_t> physical_address;
    BusRegion region;
};

struct BusAccess {
    bool write{};
    std::uint8_t bytes{};
    std::uint32_t virtual_address{};
    std::uint32_t physical_address{};
    BusRegion region{BusRegion::Unmapped};
    std::uint64_t value{};
    std::uint64_t unknown_bits{};
};
class BusObserver {
public:
    virtual ~BusObserver() = default;
    // Successful data accesses only (instruction fetches are not reported).
    virtual void on_bus_access(const BusAccess& access) = 0;
};

// One R5900 joint TLB entry exactly as written by TLBWI.
struct TlbEntry {
    std::uint32_t page_mask{};
    std::uint32_t entry_hi{};
    std::uint32_t entry_lo0{};
    std::uint32_t entry_lo1{};
};

class Memory {
public:
    enum class Privilege : std::uint8_t { Kernel, Supervisor, User, Reserved };
    static constexpr std::size_t kMainRamBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t kBiosRomBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t kScratchpadBytes = 16u * 1024u;
    static constexpr std::uint32_t kBiosRomPhysical = 0x1FC00000u;
    static constexpr unsigned kTlbEntries = 48u;
    enum class Mapping : std::uint8_t {
        // A loaded EE program: kuseg 0..32 MiB and KSEG0/KSEG1 reach main RAM
        // directly, as the PS2 kernel maps them for user ELFs.
        ElfProgram,
        // Explicit bare-metal development environment: low RAM and scratchpad
        // mapped directly, KSEG0/1 use the same physical device bus as ROM boot.
        DirectElf,
        // Power-on: KSEG0/KSEG1 decode the physical bus (RAM, ROM, devices);
        // kuseg/KSEG2/KSEG3 only through TLB entries written by the guest.
        HardwareReset
    };
    Memory();
    void load(const ElfImage& elf);
    void load_direct_elf(const ElfImage& elf);
    // Explicit optional R3000A bare-metal payload for Direct ELF development.
    void load_iop_program(std::span<const std::uint8_t> bytes, std::uint32_t address, std::uint32_t entry);
    // Zeroes RAM and scratchpad, maps the 4 MiB ROM0 at 0x1FC00000, clears the TLB
    // and resets the device registers of console `hardware` (may be nullptr),
    // whose MECHACON holds `nvm` (may be nullptr; must outlive the memory).
    void reset_for_boot(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware = nullptr,
                        const NvmImage* nvm = nullptr, const DiagnosticAssumptions* assumptions = nullptr,
                        std::span<const std::uint8_t> rom1 = {});
    [[nodiscard]] Mapping mapping() const noexcept { return mapping_; }
    // Naturally aligned little-endian accesses; misalignment is an explicit error
    // (the R5900 address-error exception is not modeled yet).
    [[nodiscard]] std::uint8_t read8(std::uint32_t guest_address) const;
    [[nodiscard]] std::uint16_t read16(std::uint32_t guest_address) const;
    [[nodiscard]] std::uint32_t read32(std::uint32_t guest_address) const;
    // CPU loads transport unknown data until a consumer needs those bits.
    // Non-observed reads remain strict.
    [[nodiscard]] ObservedWord load8_observed(std::uint32_t guest_address) const;
    [[nodiscard]] ObservedWord load16_observed(std::uint32_t guest_address) const;
    [[nodiscard]] ObservedWord load32_observed(std::uint32_t guest_address) const;
    [[nodiscard]] std::uint64_t read64(std::uint32_t guest_address) const;
    [[nodiscard]] ObservedDoubleword load64_observed(std::uint32_t guest_address) const;
    // Host diagnostics only: inspect backing RAM without TLB/cache access,
    // advancing devices, bus observers or retrying a latched DMA refusal.
    [[nodiscard]] ObservedDoubleword diagnostic_ram(std::uint32_t physical_offset, std::size_t bytes = 8u) const;
    void write8(std::uint32_t guest_address, std::uint8_t value);
    void write16(std::uint32_t guest_address, std::uint16_t value);
    void write32(std::uint32_t guest_address, std::uint32_t value);
    void store8_observed(std::uint32_t guest_address, ObservedWord word);
    void store16_observed(std::uint32_t guest_address, ObservedWord word);
    void store32_observed(std::uint32_t guest_address, ObservedWord word);
    // Contiguous enabled lanes of SWL/SWR/SDL/SDR, without reading neighbors.
    void store_bytes_observed(std::uint32_t guest_address, std::size_t bytes, ObservedDoubleword word);
    void write64(std::uint32_t guest_address, std::uint64_t value);
    void store64_observed(std::uint32_t guest_address, ObservedDoubleword word);
    // SQ: one 128-bit store at a 16-byte aligned address. Memory takes it as
    // two 64-bit halves (traced as such); a device register (the GIF FIFO)
    // receives the whole quadword at once.
    void write128(std::uint32_t guest_address, std::uint64_t low, std::uint64_t high);
    void store128_observed(std::uint32_t guest_address, ObservedQuadword word);
    // Instruction fetch: same translation and checks, not reported to the observer.
    [[nodiscard]] std::uint32_t fetch32(std::uint32_t guest_address) const;
    // Translation only (CACHE hit operations): physical address or BusError.
    [[nodiscard]] std::uint32_t translate(std::uint32_t guest_address, bool write) const;

    void tlb_write(unsigned index, const TlbEntry& entry);
    [[nodiscard]] const TlbEntry& tlb_entry(unsigned index) const;
    // Index of the entry matching EntryHi's VPN2/ASID (TLBP), if any.
    [[nodiscard]] std::optional<unsigned> tlb_probe(std::uint32_t entry_hi) const;
    // Current ASID (EntryHi) and Status.ERL, which change how addresses translate.
    void set_translation_context(std::uint8_t asid, bool error_level,
                                 Privilege privilege = Privilege::Kernel) noexcept;
    // Synchronize ASID, ERL and effective privilege after COP0/exception changes.
    void set_cpu_context(std::uint32_t status, std::uint32_t entry_hi) noexcept;
    void set_cache_config(std::uint32_t config) noexcept { cache_config_ = config; }
    // Explicit functional D-cache experiment; no additional bus latency yet.
    // Enable before execution. Reset/load removes it; the default stays coherent.
    void diagnostic_data_cache(bool translate_uncached_hits = false);
    [[nodiscard]] bool data_cache_enabled() const noexcept { return data_cache_model_; }
    [[nodiscard]] ObservedWord data_cache_operation(unsigned op, std::uint32_t address, ObservedWord tag);
    void set_observer(BusObserver* observer) noexcept { observer_ = observer; }
    // EE cycle counter used by the timed EE registers (boot mapping only).
    void set_clock(const std::uint64_t* ee_cycles,const std::uint64_t* stalls=nullptr) noexcept { registers_.set_clock(ee_cycles,stalls); cache_clock_=ee_cycles; }
    [[nodiscard]] const GsRegisters& gs() const noexcept { return registers_.gs(); }
    void diagnostic_gs_scanmask_reset(unsigned mask) { registers_.diagnostic_gs_scanmask_reset(mask); }
    [[nodiscard]] const Gif& gif() const noexcept { return registers_.gif(); }
    [[nodiscard]] const MchRegisters& mch() const noexcept { return registers_.mch(); }
    void diagnostic_dev9_power_reset(std::uint16_t value) { registers_.diagnostic_dev9_power_reset(value); }
    void diagnostic_dev9_irq_reset_clear() { registers_.diagnostic_dev9_irq_reset_clear(); }
    void diagnostic_dev9_irq_disconnected() { registers_.diagnostic_dev9_irq_disconnected(); }
    void diagnostic_spu2_dma_wrap_to_zero() { registers_.diagnostic_spu2_dma_wrap_to_zero(); }
    void attach_speed_eeprom_words(std::span<const std::uint16_t> words,std::string_view hash) { registers_.attach_speed_eeprom_words(words,hash); }
    void diagnostic_speed_eeprom_presence(bool present) { registers_.diagnostic_speed_eeprom_presence(present); }
    void diagnostic_dev9_control_latches() { registers_.diagnostic_dev9_control_latches(); }
    void diagnostic_dev9_power_settle_cycles(std::uint32_t cycles) { registers_.diagnostic_dev9_power_settle_cycles(cycles); }
    void diagnostic_sio2_infrared_timeout(std::uint32_t cycles) { registers_.diagnostic_sio2_infrared_timeout(cycles); }
    void diagnostic_sio2_port_timing(unsigned bits) { registers_.diagnostic_sio2_port_timing(bits); }
    void diagnostic_sio2_digital_pad(unsigned port,std::uint16_t buttons,std::uint32_t cycles) { registers_.diagnostic_sio2_digital_pad(port,buttons,cycles); }
    void set_sio2_digital_pad_buttons(unsigned port,std::uint16_t buttons) { registers_.set_sio2_digital_pad_buttons(port,buttons); }
    void diagnostic_sio2_dma_word_cycles(std::uint32_t cycles) { registers_.diagnostic_sio2_dma_word_cycles(cycles); }
    void diagnostic_sio2_reset_clears_completion() { registers_.diagnostic_sio2_reset_clears_completion(); }
    void diagnostic_sio2_unplugged_ports(unsigned mask,std::uint32_t cycles) { registers_.diagnostic_sio2_unplugged_ports(mask,cycles); }
    [[nodiscard]] const Iop& iop() const noexcept { return registers_.iop(); }
    void diagnostic_spu2_7ca_latch() { registers_.diagnostic_spu2_7ca_latch(); }
    void diagnostic_spu2_reserved_latches() { registers_.diagnostic_spu2_reserved_latches(); }
    void mount_disc(std::shared_ptr<DiscImage> disc,Iop::DiscMedium medium,bool diagnostic_cav_nominal_timing=false) {
        registers_.mount_disc(std::move(disc),medium,diagnostic_cav_nominal_timing);
    }
    [[nodiscard]] const VideoEncoderBus& video_encoder() const noexcept { return registers_.video_encoder(); }
    [[nodiscard]] std::uint32_t read_vu_control(unsigned index) const { return registers_.read_vu_control(index); }
    void write_vu_control(unsigned index, std::uint32_t value) { registers_.write_vu_control(index,value); }
    [[nodiscard]] ObservedWord observe_vu_control(unsigned index) const {return registers_.observe_vu_control(index);}
    void write_vu_control_observed(unsigned index, ObservedWord word) {registers_.write_vu_control_observed(index,word);}
    void vu_advance_q(std::uint64_t cycle) { registers_.vu_advance_q(cycle); }
    [[nodiscard]] std::uint64_t vu_q_ready_cycle() const noexcept { return registers_.vu_q_ready_cycle(); }
    void vu_issue_q(std::uint64_t cycle,unsigned left,unsigned right,unsigned left_lane,unsigned right_lane,bool square_root) {
        registers_.vu_issue_q(cycle,left,right,left_lane,right_lane,square_root);
    }
    void vu_integer_store(unsigned source,unsigned base,unsigned mask) {registers_.vu_integer_store(source,base,mask);}
    [[nodiscard]] ObservedWord observe_vu_data(std::uint32_t address) const {return registers_.observe_vu_data(address);}
    // Host diagnostic snapshot at the current device time, without a CPU bus
    // access or clock advance. Unknown micro/data bits remain explicit.
    [[nodiscard]] ObservedDoubleword diagnostic_vu_memory(std::uint32_t physical,std::size_t bytes) const {
        if(bytes!=1u && bytes!=2u && bytes!=4u && bytes!=8u)throw DeviceRefusal("VU snapshot width must be 1, 2, 4 or 8 bytes");
        return registers_.read_vu_memory(physical,bytes);
    }
    [[nodiscard]] ObservedQuadword observe_vu_vector(unsigned index) const {return registers_.observe_vu_vector(index);}
    void write_vu_vector(unsigned index,ObservedQuadword word) {registers_.write_vu_vector(index,word);}
    void vu_subtract(unsigned destination,unsigned left,unsigned right,unsigned mask) {
        registers_.vu_subtract(destination,left,right,mask);
    }
    void vu_add(unsigned destination,unsigned left,unsigned right,unsigned mask,unsigned broadcast=4,bool subtract=false) {
        registers_.vu_add(destination,left,right,mask,broadcast,subtract);
    }
    void vu_rotate(unsigned destination,unsigned source,unsigned mask) {registers_.vu_rotate(destination,source,mask);}
    void vu_product(unsigned destination,unsigned left,unsigned right,unsigned mask,unsigned broadcast,bool accumulate,bool to_accumulator,bool outer=false) {
        registers_.vu_product(destination,left,right,mask,broadcast,accumulate,to_accumulator,outer);
    }
    void vu_vector_store_increment(unsigned source,unsigned base,unsigned mask) {
        registers_.vu_vector_store_increment(source,base,mask);
    }
    void vu_add_integer(unsigned destination,unsigned left,unsigned right) {registers_.vu_add_integer(destination,left,right);}
    // Runs the IOP up to the current EE time (boot mapping, clock attached).
    void sync_iop() { registers_.sync_iop(); }
    [[nodiscard]] bool intc_interrupt_pending() const;
    [[nodiscard]] bool dmac_interrupt_pending() const { return registers_.dmac_interrupt_pending(); }
    [[nodiscard]] const std::string& sio_output() const noexcept { return registers_.sio_output(); }

private:
    struct Target {
        std::uint32_t physical{};
        BusRegion region{BusRegion::Unmapped};
        std::uint32_t offset{}; // into RAM, ROM or scratchpad storage
        unsigned cache_mode{2};
    };
    [[nodiscard]] static std::size_t physical_address(std::uint32_t guest_address, std::size_t size);
    [[nodiscard]] Target resolve(std::uint32_t guest_address, std::size_t size, bool write,
                                 const char* what) const;
    [[nodiscard]] std::uint32_t translate_tlb(std::uint32_t guest_address, std::size_t size, bool write,
                                              bool& scratchpad, unsigned& cache_mode) const;
    [[nodiscard]] std::uint64_t read_le(std::uint32_t guest_address, std::size_t size, const char* what,
                                        bool notify, std::uint64_t* unknown = nullptr) const;
    void write_le(std::uint32_t guest_address, std::size_t size, std::uint64_t value, const char* what,
                  std::uint64_t unknown = 0u);
    [[nodiscard]] std::uint64_t storage_unknown(const Target& target, std::size_t size) const;
    void set_storage_unknown(const Target& target, std::size_t size, std::uint64_t unknown) const;
    struct DataCacheLine {
        std::uint32_t tag{};
        bool pfn_known{};
        std::array<std::uint8_t,64> bytes{};
        std::array<std::uint8_t,64> unknown{};
    };
    void reset_data_cache();
    [[nodiscard]] bool uses_data_cache(const Target& target) const;
    [[nodiscard]] DataCacheLine& data_cache_line(std::uint32_t address, const Target& target) const;
    void writeback_data_cache(DataCacheLine& line, unsigned index) const;
    mutable std::array<std::array<DataCacheLine,2>,64> data_cache_{};
    bool data_cache_model_{};
    bool data_cache_uncached_hits_{};
    std::uint32_t cache_config_{};
    const std::uint64_t* cache_clock_{};
    mutable std::vector<std::uint8_t> ram_;
    // Physical aligned word -> symbolic bits. Scratchpad keys have bit 31 set;
    // aliases share metadata, ordinary known RAM pays no per-byte allocation.
    mutable std::unordered_map<std::uint32_t, std::uint32_t> unknown_words_;
    // EE register reads can have side effects (timers sample the clock).
    mutable EeRegisters registers_;
    std::vector<std::uint8_t> rom_;
    std::vector<std::uint8_t> rom1_;
    std::vector<std::uint8_t> scratchpad_;
    std::array<TlbEntry, kTlbEntries> tlb_{};
    // Entries are undefined after reset until the guest writes them.
    std::array<bool, kTlbEntries> tlb_defined_{};
    Mapping mapping_{Mapping::ElfProgram};
    std::uint8_t asid_{};
    bool error_level_{};
    Privilege privilege_{Privilege::Kernel};
    BusObserver* observer_{};
};
} // namespace ps2native
