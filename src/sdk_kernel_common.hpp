#pragma once
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "ps2_log.h"
#include "sdk/kernel_interface.hpp"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <chrono>
#include <string>
#include <type_traits>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "Helpers/State.h"

// Native syscall structures use checked bus transactions, never a private RAM.
void* ps2n_sdk_guest_view(std::uint32_t address,std::size_t size,bool output,std::uint64_t inputWords=~0ull);
inline bool resolveEeGuestRange(std::uint32_t address,std::size_t size,std::uint32_t& offset,bool& scratch) {
    scratch=address>=0x70000000u && address<0x70000000u+ps2native::Memory::kScratchpadBytes;
    if(scratch){offset=address-0x70000000u;return size<=ps2native::Memory::kScratchpadBytes-offset;}
    offset=address>=0x80000000u && address<0xC0000000u ? address&0x1FFFFFFFu:address;
    return offset<=PS2_RAM_SIZE && size<=PS2_RAM_SIZE-offset;
}
template<class T> T* getEeGuestStruct(std::uint8_t*,std::uint32_t address) {
    if(address&(alignof(T)-1u))return nullptr;
    constexpr auto words=std::is_same_v<T,ee_thread_t>?0x1BEull:std::is_same_v<T,ee_sema_t>?0x36ull:~0ull;
    return static_cast<T*>(ps2n_sdk_guest_view(address,sizeof(T),false,words));
}
template<class T> T* getEeGuestOutput(std::uint8_t*,std::uint32_t address) {
    if(address&(alignof(T)-1u))return nullptr;
    return static_cast<T*>(ps2n_sdk_guest_view(address,sizeof(T),true));
}

inline void setRegU32(R5900Context* context,int reg,std::uint32_t value) {SET_GPR_U32(context,reg,value);}
inline std::uint8_t* getMemPtr(std::uint8_t*,std::uint32_t address) {return static_cast<std::uint8_t*>(ps2n_sdk_guest_view(address,4,true));}
inline const std::uint8_t* getConstMemPtr(const std::uint8_t*,std::uint32_t address) {return static_cast<const std::uint8_t*>(ps2n_sdk_guest_view(address,4,false));}
#include "Helpers/Runtime.h"

inline std::string readGuestCStringBounded(const uint8_t*,std::uint32_t address,std::size_t limit) {
    std::string text;
    for(std::size_t i=0;i<limit;++i){const auto byte=ps2native::sdk_kernel_memory().read8(address+static_cast<std::uint32_t>(i));if(!byte)return text;text.push_back(static_cast<char>(byte));}
    throw std::runtime_error("PS2Recomp guest string exceeds its declared bound");
}
