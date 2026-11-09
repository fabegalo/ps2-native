#pragma once
#include "ps2native/iop_aot.hpp"
#include "ps2native/sha256.hpp"
#include <filesystem>

namespace ps2native {
struct NativeIopModuleDescriptor {
    std::uint32_t version,size;
    const char* runtime_abi;
    const IopAotModule* module;
};
inline std::string native_iop_cache_key(std::string_view image,std::string_view abi) {
    const auto text="iop:"+std::string(image)+std::string(abi);
    return sha256_hex({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
}
namespace frontend {
class NativeIopCode {
public:
    NativeIopCode(const std::filesystem::path& receipt,const std::string& image_hash);
    ~NativeIopCode();
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
}
