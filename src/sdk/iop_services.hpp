// SPDX-License-Identifier: GPL-3.0-only
// Host adapter for the pinned PS2Recomp IOP services; see NOTICE.
#pragma once
#include "iop_memory_native.hpp"
#include "ps2native/bios.hpp"
#include "ps2x/iop/iop_host.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/imports/iop_cdvd.h"
#include "emulator/iop_emulator.h"
#include "console_interface.hpp"
#include "iop_service.h"
#include <filesystem>

namespace ps2native {
class Memory;
struct EEState;
class Ps2RecompIopServices final : public ps2x::iop::IopHost {
public:
    explicit Ps2RecompIopServices(Memory& memory,EEState& state);
    // Adapter-owned canonical reply packet; other caller tokens remain separate.
    static constexpr std::uint64_t RpcPacketToken=std::uint64_t{1}<<63u;
    static void configure_console(Memory&,const ConsoleHardware*,std::shared_ptr<const NvmImage>);
    static void configure_spu2_quiescent_reset(Memory&);
    static std::uint32_t read_device(Iop&,std::uint32_t address,unsigned bytes);
    static void write_device(Iop&,std::uint32_t address,unsigned bytes,std::uint32_t value);
    void run_until(std::uint64_t cycle);
    void boot_modules();
    void on_vblank();
    void configure_memory_card_root(std::filesystem::path root);
    [[nodiscard]] ps2x::iop::ModuleLoadResult load_irx(std::string_view path,std::span<const std::uint8_t> image);
    [[nodiscard]] std::optional<std::pair<std::uint32_t,std::uint32_t>> module_range(std::uint32_t pc) const;
    [[nodiscard]] bool executing_native() const noexcept {return nativeDepth_!=0;}
    [[nodiscard]] bool executing_cpu() const noexcept {return emulator_->nativeCpuActive();}
    [[nodiscard]] bool rpc_server(ps2x::iop::RpcRequest& request,bool byAddress=false) const;
    [[nodiscard]] std::optional<std::pair<std::uint32_t,std::uint32_t>> rpc_call(ps2x::iop::RpcRequest request);
    [[nodiscard]] std::optional<std::pair<std::uint32_t,std::uint32_t>> rpc_poll(ps2x::iop::RpcRequest* completed=nullptr,bool transport=false);
    [[nodiscard]] bool has_native_work() const noexcept {return emulator_->threadCount()!=0 || emulator_->nativeHasAlarms() || emulator_->nativeHasSifDma() || emulator_->nativeHasRpc();}
    [[nodiscard]] bool has_cdvd() const noexcept;
    [[nodiscard]] std::vector<std::string> launch_arguments() const;
    [[nodiscard]] std::uint32_t cdvd_init(std::uint32_t mode);
    void configure_bios(std::shared_ptr<const BiosImage> bios);
    [[nodiscard]] std::array<std::uint32_t,3> cdvd_versions() const;
    [[nodiscard]] std::string reset(std::string_view arguments,std::uint32_t mode);
    [[nodiscard]] std::uint32_t allocate_heap(std::uint32_t bytes);
    [[nodiscard]] int free_heap(std::uint32_t address);
private:
    friend class Iop;
    std::pair<std::uint32_t,std::uint32_t> stage_rpc_response(const ps2x::iop::RpcResult&,std::uint32_t);
    void initialize_memory();
    void reset_interrupts();
    ps2x::iop::detail::IopMemory& nativeMemory() override {return memory_;}
    std::uint32_t nativeCdvdStatus() const override;
    bool nativeStep(ps2x::iop::detail::IopCpuState&) override;
    bool nativeInterrupt(ps2x::iop::detail::IopCpuState&) override;
    std::uint32_t nativePendingInterrupts(std::uint32_t interested) const override;
    void nativeAcknowledgeInterrupts(std::uint32_t handled) override;
    bool nativeInterruptsEnabled() const override;
    void nativePublish(const ps2x::iop::detail::IopCpuState& cpu) override {load_cpu(cpu);}
    void nativePrepareImport(ps2x::iop::detail::IopCpuState& cpu) override;
    bool nativeSif0Available() const override;
    bool nativeSif0Complete() const override;
    void nativeStartSif0(std::uint32_t) override;
    std::uint64_t nativeCycles() const override;
    void nativeTick(std::uint64_t cycle) override;
    void nativeModuleLoaded(std::uint32_t base,std::uint32_t size,std::uint32_t entry) override;
    void nativeModuleReleased(std::uint32_t base,std::uint32_t size) override;
    std::optional<std::uint32_t> nativeStartModule(std::string_view,std::uint16_t,std::uint32_t,std::uint32_t,std::int32_t) override;
    void load_cpu(const ps2x::iop::detail::IopCpuState&);
    void save_cpu(ps2x::iop::detail::IopCpuState&) const;
    [[noreturn]] static void unsupported(const char* service);
    bool readGuest(std::uint32_t,void*,std::size_t) const override;
    bool writeGuest(std::uint32_t,const void*,std::size_t) override;
    bool zeroGuest(std::uint32_t,std::size_t) override;
    bool normalizeGuestAddress(std::uint32_t,std::uint32_t&) const override;
    bool validateGuestOutput(std::uint32_t,std::size_t) const override;
    bool readRpcInput(std::uint32_t,void*,std::size_t) const override;
    bool writeRpcOutput(std::uint32_t,const void*,std::size_t) override;
    std::uint32_t allocateIopHandle(ps2x::iop::IopHandleKind) override;
    std::uint32_t allocateGuest(std::uint32_t,std::uint32_t) override;
    void freeGuest(std::uint32_t) override;
    void audioCommand(std::uint32_t,std::uint32_t,ps2x::iop::GuestBuffer,ps2x::iop::GuestBuffer) override;
    std::string hostPath(ps2x::iop::HostPathKind) const override;
    std::string translateGuestPath(std::string_view) const override;
    std::uint64_t openHostFile(std::string_view) override;
    bool hostFileSize(std::uint64_t,std::uint64_t&) const override;
    bool readHostFile(std::uint64_t,std::uint64_t,void*,std::size_t,std::size_t&) override;
    void closeHostFile(std::uint64_t) override;
    std::int32_t memoryCard(const ps2x::iop::MemoryCardRequest&) override;
    std::int32_t pad(const ps2x::iop::PadRequest&) override;
    bool readPadInput(unsigned,unsigned,std::uint8_t*,std::size_t) const override;
    bool setPadRumble(unsigned,unsigned,std::uint8_t,std::uint8_t) override;
    bool hasGuestFunction(std::uint32_t) const override;
    bool invokeGuestFunction(std::uint64_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t*) override;
    void log(ps2x::iop::LogLevel,std::string_view) override;
    Iop& iop_;
    Memory& consoleMemory_;
    EEState& eeState_;
    ps2x::iop::detail::IopMemory memory_;
    std::unique_ptr<ps2x::iop::detail::IopEmulator> emulator_;
    std::filesystem::path memoryCardRoot_;
    std::shared_ptr<void> memoryCardState_;
    bool memoryCardIo_{},memoryCardRpc_{},padIo_{},padRpc_{};
    std::shared_ptr<void> padState_;
    std::unique_ptr<ps2x::iop::detail::IopService> padService_;
    struct PadEndpoint {std::uint32_t sid{},descriptor{},buffer{};};
    std::array<PadEndpoint,4> padEndpoints_{};
    std::uint32_t padModuleBase_{};
    std::unique_ptr<ps2x::iop::detail::IopService> memoryCardService_;
    std::uint16_t memoryCardManagerVersion_{};
    std::uint32_t memoryCardManagerBase_{},memoryCardServerBase_{},memoryCardServer_{},memoryCardBuffer_{};
    unsigned nativeDepth_{};
    std::uint32_t rpcResponse_{};
    std::shared_ptr<const NvmImage> consoleNvm_;
    bool bootPending_{};
    std::vector<std::pair<std::uint32_t,std::uint32_t>> nativeModules_;
    std::shared_ptr<const BiosImage> bios_;
    std::shared_ptr<const IopRebootImage> reboot_;
};
}
