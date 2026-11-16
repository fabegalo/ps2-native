#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>
namespace ps2::gs::queue {
enum Type : std::uint32_t {kReset,kSoftReset,kVblank,kDownload,kDownloadAsync,kGif,kGifReset,kPath3Idle};
enum class Role {Emulation,Vu,Render};
inline constexpr Role role(){return Role::Emulation;}
inline constexpr bool vu_active(){return false;}
inline constexpr bool active(){return false;}
inline void post(Type,std::uint32_t,std::uint32_t,const void*,std::uint32_t){throw std::runtime_error("External GS runtime queue is not part of the engine");}
inline void wait(std::atomic<std::uint32_t>&,std::uint32_t){throw std::runtime_error("External GS runtime wait is not part of the engine");}
}
