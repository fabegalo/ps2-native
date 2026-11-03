#include "ps2native/executable.hpp"
#include "ps2native/sha256.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace ps2native {
ElfImage snapshot_ee_code(const Memory& memory, std::uint32_t address, std::uint32_t bytes,
                           std::string_view expected_code_sha256) {
    if (memory.mapping()!=Memory::Mapping::DirectElf)
        throw std::runtime_error("code snapshots require Direct ELF mode (firmware provenance is not inferred)");
    if (!bytes || (address&3u) || (bytes&3u) || address>=Memory::kMainRamBytes ||
        bytes>Memory::kMainRamBytes-address)
        throw std::runtime_error("invalid executable snapshot range");
    std::vector<std::uint8_t> code(bytes);
    for(std::uint32_t i=0;i<bytes;++i) code[i]=memory.read8(address+i);
    if (expected_code_sha256.size()!=64u || sha256_hex(code)!=expected_code_sha256)
        throw std::runtime_error("executable snapshot SHA-256 differs from independently verified code");
    std::vector<std::uint8_t> elf(0x100u+bytes);
    const auto put=[&](std::size_t offset,std::uint32_t value,unsigned size=4) {
        for(unsigned i=0;i<size;++i)elf.at(offset+i)=static_cast<std::uint8_t>(value>>(i*8u));
    };
    elf[0]=0x7F;elf[1]='E';elf[2]='L';elf[3]='F';elf[4]=elf[5]=elf[6]=1;
    put(16,2,2);put(18,8,2);put(20,1);put(24,address);put(28,52);put(40,52,2);put(42,32,2);put(44,1,2);
    put(52,1);put(56,0x100);put(60,address);put(64,address);put(68,bytes);put(72,bytes);put(76,5);put(80,4);
    std::copy(code.begin(),code.end(),elf.begin()+0x100);
    return parse_ee_elf(elf);
}
VerifiedAotOverlay VerifiedAotOverlay::verify(const ElfImage& image,const AotModule& module,const Memory& memory) {
    if(memory.mapping()!=Memory::Mapping::DirectElf)throw std::runtime_error("AOT overlays require Direct ELF mode");
    const auto checked=parse_ee_elf(image.bytes);
    if(std::string_view(module.source_kind)!="elf" || sha256_hex(checked.bytes)!=module.source_sha256)
        throw std::runtime_error("AOT overlay provenance mismatch");
    for(const auto& s:checked.segments) {
        if(!(s.flags&1u) || s.file_size!=s.memory_size)
            throw std::runtime_error("AOT overlay must contain only file-backed executable regions");
        for(std::uint32_t i=0;i<s.file_size;++i)
            if(memory.read8(s.vaddr+i)!=checked.bytes[s.file_offset+i])
                throw std::runtime_error("AOT overlay bytes differ from loaded RAM");
    }
    return VerifiedAotOverlay(module,memory);
}
const AotModule& VerifiedAotOverlay::module_for(const Memory& memory) const {
    if(&memory!=memory_)throw std::runtime_error("AOT overlay was verified in a different runtime");
    return *module_;
}
} // namespace ps2native
