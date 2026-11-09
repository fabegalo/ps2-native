#pragma once
#include "ps2native/native_module.hpp"
#include <filesystem>
#include <memory>
#include <string>
namespace ps2native {class Memory;struct BootOptions;}
namespace ps2native::frontend {
class LoadedNativeModule {
public:
    LoadedNativeModule(const std::filesystem::path& receipt,const std::string& input_sha256,
                       const std::string& elf_sha256,std::uint32_t execution_alias=0);
    ~LoadedNativeModule();
    LoadedNativeModule(const LoadedNativeModule&)=delete;
    LoadedNativeModule& operator=(const LoadedNativeModule&)=delete;
    const AotModule& module() const noexcept;
    // Called at a strict execution stop. Only matching, offline-built regions activate.
    bool activate_overlays(const Memory& memory,std::uint32_t pc,BootOptions& options);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
