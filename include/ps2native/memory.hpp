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
    static constexpr std::size_t kMainRamBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t kBiosRomBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t kScratchpadBytes = 16u * 1024u;
    static constexpr std::uint32_t kBiosRomPhysical = 0x1FC00000u;
    static constexpr unsigned kTlbEntries = 48u;
    enum class Mapping : std::uint8_t {
        // A loaded EE program: kuseg 0..32 MiB and KSEG0/KSEG1 reach main RAM
        // directly, as the PS2 kernel maps them for user ELFs.
        ElfProgram,
        // Power-on: KSEG0/KSEG1 decode the physical bus (RAM, ROM, devices);
        // kuseg/KSEG2/KSEG3 only through TLB entries written by the guest.
        HardwareReset
    };
    Memory();
    void load(const ElfImage& elf);
    // Zeroes RAM and scratchpad, maps the 4 MiB ROM0 at 0x1FC00000, clears the TLB
    // and resets the device registers of console `hardware` (may be nullptr),
    // whose MECHACON holds `nvm` (may be nullptr; must outlive the memory).
    void reset_for_boot(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware = nullptr,
                        const NvmImage* nvm = nullptr);
    [[nodiscard]] Mapping mapping() const noexcept { return mapping_; }
    // Naturally aligned little-endian accesses; misalignment is an explicit error
    // (the R5900 address-error exception is not modeled yet).
    [[nodiscard]] std::uint8_t read8(std::uint32_t guest_address) const;
    [[nodiscard]] std::uint16_t read16(std::uint32_t guest_address) const;
    [[nodiscard]] std::uint32_t read32(std::uint32_t guest_address) const;
    [[nodiscard]] std::uint64_t read64(std::uint32_t guest_address) const;
    void write8(std::uint32_t guest_address, std::uint8_t value);
    void write16(std::uint32_t guest_address, std::uint16_t value);
    void write32(std::uint32_t guest_address, std::uint32_t value);
    void write64(std::uint32_t guest_address, std::uint64_t value);
    // Instruction fetch: same translation and checks, not reported to the observer.
    [[nodiscard]] std::uint32_t fetch32(std::uint32_t guest_address) const;
    // Translation only (CACHE hit operations): physical address or BusError.
    [[nodiscard]] std::uint32_t translate(std::uint32_t guest_address, bool write) const;

    void tlb_write(unsigned index, const TlbEntry& entry);
    [[nodiscard]] const TlbEntry& tlb_entry(unsigned index) const;
    // Index of the entry matching EntryHi's VPN2/ASID (TLBP), if any.
    [[nodiscard]] std::optional<unsigned> tlb_probe(std::uint32_t entry_hi) const;
    // Current ASID (EntryHi) and Status.ERL, which change how addresses translate.
    void set_translation_context(std::uint8_t asid, bool error_level) noexcept;
    void set_observer(BusObserver* observer) noexcept { observer_ = observer; }
    // EE cycle counter used by the timed EE registers (boot mapping only).
    void set_clock(const std::uint64_t* ee_cycles) noexcept { registers_.set_clock(ee_cycles); }
    [[nodiscard]] const GsRegisters& gs() const noexcept { return registers_.gs(); }
    [[nodiscard]] const MchRegisters& mch() const noexcept { return registers_.mch(); }
    [[nodiscard]] const Iop& iop() const noexcept { return registers_.iop(); }
    // Runs the IOP up to the current EE time (boot mapping, clock attached).
    void sync_iop() { registers_.sync_iop(); }
    [[nodiscard]] const std::string& sio_output() const noexcept { return registers_.sio_output(); }

private:
    struct Target {
        std::uint32_t physical{};
        BusRegion region{BusRegion::Unmapped};
        std::uint32_t offset{}; // into RAM, ROM or scratchpad storage
    };
    [[nodiscard]] static std::size_t physical_address(std::uint32_t guest_address, std::size_t size);
    [[nodiscard]] Target resolve(std::uint32_t guest_address, std::size_t size, bool write,
                                 const char* what) const;
    [[nodiscard]] std::uint32_t translate_tlb(std::uint32_t guest_address, std::size_t size, bool write,
                                              bool& scratchpad) const;
    [[nodiscard]] std::uint64_t read_le(std::uint32_t guest_address, std::size_t size, const char* what,
                                        bool notify) const;
    void write_le(std::uint32_t guest_address, std::size_t size, std::uint64_t value, const char* what);
    std::vector<std::uint8_t> ram_;
    // EE register reads can have side effects (timers sample the clock).
    mutable EeRegisters registers_;
    std::vector<std::uint8_t> rom_;
    std::vector<std::uint8_t> scratchpad_;
    std::array<TlbEntry, kTlbEntries> tlb_{};
    // Entries are undefined after reset until the guest writes them.
    std::array<bool, kTlbEntries> tlb_defined_{};
    Mapping mapping_{Mapping::ElfProgram};
    std::uint8_t asid_{};
    bool error_level_{};
    BusObserver* observer_{};
};
} // namespace ps2native
