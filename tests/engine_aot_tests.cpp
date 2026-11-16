#include "analysis.hpp"
#include "elf.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

int main() {
    try {
        constexpr std::uint32_t base=0x200000;
        // Two taken branches: the second is in the first one's delay slot.
        const std::uint32_t words[]={0x24080000,0x11080004,0x11080006,0,0,0,
                                     0x24040001,0x03e00008,0,0x24040002,0x03e00008,0};
        std::vector<std::uint8_t> bytes(sizeof(words));
        std::memcpy(bytes.data(),words,sizeof(words));
        const auto image=recomp::Image::raw(base,bytes,{base,base+sizeof(words)});
        const auto p=recomp::analyse(image,{base},{},recomp::Search{.whole_image=false});
        if (!p.is_code(base+36) || !p.is_code(base+40)) throw std::runtime_error("Lost target of branch in delay slot");
        // JAL in a slot needs its static callee and its distinct link continuation.
        auto call_words=std::vector<std::uint32_t>(std::begin(words),std::end(words));
        call_words[2]=0x0c000000|((base+36)>>2);
        std::memcpy(bytes.data(),call_words.data(),bytes.size());
        const auto call_image=recomp::Image::raw(base,bytes,{base,base+sizeof(words)});
        const auto call=recomp::analyse(call_image,{base},{},recomp::Search{.whole_image=false});
        if (!call.is_code(base+36) || !call.entries.contains(base+16)) throw std::runtime_error("Lost nested-call target/continuation");
        std::puts("engine-aot: nested delay targets and call continuations PASS");
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
