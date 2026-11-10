#include "ps2native/disc.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace ps2native {
namespace {
constexpr std::uint32_t sector_size=2048;
constexpr std::uint32_t read_limit=128u*1024u*1024u;
std::uint32_t both32(std::span<const std::uint8_t> b, std::size_t offset) {
    std::uint32_t lo=0,hi=0;
    for(unsigned i=0;i<4;++i){lo|=std::uint32_t{b[offset+i]}<<(8u*i);hi=(hi<<8u)|b[offset+4+i];}
    if(lo!=hi) throw std::runtime_error("ISO 9660 inconsistent both-endian integer");
    return lo;
}
std::string upper(std::string_view input) {
    std::string s(input);
    for(auto& c:s)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(std::string_view input) {
    const auto first=input.find_first_not_of(" \t\r\n");
    if(first==std::string_view::npos)return {};
    return std::string(input.substr(first,input.find_last_not_of(" \t\r\n")-first+1));
}
std::vector<std::string> components(std::string_view path) {
    std::vector<std::string> result;
    while(!path.empty()) {
        const auto slash=path.find_first_of("/\\");
        const auto part=path.substr(0,slash);
        if(!part.empty()) {
            if(part=="."||part==".."||part.find(':')!=part.npos||part.find('\0')!=part.npos)
                throw std::runtime_error("invalid ISO 9660 guest path component");
            result.push_back(upper(part));
            if(result.size()>64)throw std::runtime_error("ISO 9660 path depth limit");
        }
        if(slash==path.npos)break;
        path.remove_prefix(slash+1);
    }
    return result;
}
bool descriptor(std::span<const std::uint8_t> b) {
    return b[0]==1 && std::memcmp(b.data()+1,"CD001",5)==0 && b[6]==1;
}
}
DiscImage::DiscImage(const std::filesystem::path& path):file_(path) {
    std::array<std::uint8_t,2048> pvd{};
    bool found=file_.read_at(16u*2048,pvd)==pvd.size() && descriptor(pvd);
    if(!found) {
        std::array<std::uint8_t,2352> raw{};
        if(file_.read_at(16u*2352,raw)!=raw.size() || raw[0]!=0 || raw[11]!=0 ||
           !std::all_of(raw.begin()+1,raw.begin()+11,[](auto b){return b==255;}) ||
           (raw[15]!=1 && raw[15]!=2))throw std::runtime_error("unsupported disc image: ISO 9660 PVD missing");
        sector_bytes_=2352;data_offset_=raw[15]==1?16u:24u;
        if(raw[15]==2 && (raw[18]&0x20u))throw std::runtime_error("Mode 2 Form 2 is not an ISO user-data sector");
        std::copy_n(raw.begin()+data_offset_,2048,pvd.begin());
        if(!descriptor(pvd))throw std::runtime_error("unsupported raw disc: ISO 9660 PVD missing");
    }
    if(pvd[128]!=0 || pvd[129]!=8 || pvd[130]!=8 || pvd[131]!=0)
        throw std::runtime_error("ISO 9660 requires 2048-byte logical blocks");
    sectors_=both32(pvd,80);
    if(!sectors_ || std::uint64_t{sectors_}*sector_bytes_>file_.size())
        throw std::runtime_error("ISO 9660 volume exceeds disc image");
    root_=record(std::span(pvd).subspan(156,pvd[156]));
    if(!root_.directory)throw std::runtime_error("ISO 9660 root is not a directory");
    volume_id_=trim(std::string_view(reinterpret_cast<const char*>(pvd.data()+40),32));
}
void DiscImage::read_sector(std::uint32_t lba,std::span<std::uint8_t,2048> out) {
    if(lba>=sectors_)throw std::runtime_error("disc LBA outside ISO 9660 volume");
    if(sector_bytes_==2352) {
        std::array<std::uint8_t,2352> raw{};
        if(file_.read_at(std::uint64_t{lba}*2352u,raw)!=raw.size() || raw[0]!=0 || raw[11]!=0 ||
           !std::all_of(raw.begin()+1,raw.begin()+11,[](auto b){return b==255;}) ||
           raw[15]!=(data_offset_==16u?1u:2u) || (raw[15]==2 && (raw[18]&0x20u)))
            throw std::runtime_error("invalid raw CD user-data sector");
        std::copy_n(raw.begin()+data_offset_,2048,out.begin());
        return;
    }
    if(file_.read_at(std::uint64_t{lba}*sector_bytes_+data_offset_,out)!=out.size())
        throw std::runtime_error("short disc sector read");
}
DiscEntry DiscImage::record(std::span<const std::uint8_t> b) {
    if(b.size()<34 || b[0]!=b.size() || !b[32] || 33u+b[32]>b.size())
        throw std::runtime_error("malformed ISO 9660 directory record");
    if(b[1] || b[26] || b[27] || (b[25]&0x80u))
        throw std::runtime_error("ISO 9660 extended attributes/interleaved/multi-extent file unsupported");
    const auto lba=both32(b,2), size=both32(b,10);
    if(lba>=sectors_ || std::uint64_t{size}>std::uint64_t{sectors_-lba}*2048u)
        throw std::runtime_error("ISO 9660 extent outside volume");
    return {std::string(reinterpret_cast<const char*>(b.data()+33),b[32]),lba,size,(b[25]&2u)!=0u};
}
std::vector<DiscEntry> DiscImage::entries(const DiscEntry& dir) {
    if(!dir.directory)throw std::runtime_error("ISO 9660 path is not a directory");
    if(dir.size>4u*1024u*1024u)throw std::runtime_error("ISO 9660 directory exceeds read limit");
    std::vector<DiscEntry> result;
    std::array<std::uint8_t,2048> bytes{};
    for(std::uint64_t start=0;start<dir.size;start+=2048u) {
        read_sector(dir.lba+static_cast<std::uint32_t>(start/2048u),bytes);
        const auto limit=static_cast<std::size_t>(std::min<std::uint64_t>(2048u,dir.size-start));
        for(std::size_t pos=0;pos<limit && bytes[pos];) {
            const auto length=bytes[pos];
            if(pos+length>limit)throw std::runtime_error("ISO 9660 record crosses directory sector");
            auto entry=record(std::span(bytes).subspan(pos,length));pos+=length;
            if(entry.name.size()==1 && (entry.name[0]==0||entry.name[0]==1))continue;
            result.push_back(std::move(entry));
            if(result.size()>65536)throw std::runtime_error("ISO 9660 directory entry limit");
        }
    }
    return result;
}
DiscEntry DiscImage::find(std::string_view path) {
    auto current=root_;
    for(const auto& part:components(path)) {
        const auto files=entries(current);
        std::optional<DiscEntry> match;
        for(const auto& entry:files) {
            auto name=upper(entry.name);
            if(part.find(';')==part.npos)name=name.substr(0,name.find(';'));
            if(name!=part)continue;
            if(match)throw std::runtime_error("ambiguous ISO 9660 file version");
            match=entry;
        }
        if(!match)throw std::runtime_error("ISO 9660 file missing: "+part);
        current=*match;
    }
    return current;
}
std::vector<DiscEntry> DiscImage::list(std::string_view path){return entries(find(path));}
std::uint32_t DiscImage::file_size(std::string_view path) {
    const auto entry=find(path);
    if(entry.directory)throw std::runtime_error("ISO 9660 directory is not a file");
    return entry.size;
}
std::vector<std::uint8_t> DiscImage::read(std::string_view path) {
    const auto entry=find(path);
    if(entry.directory)throw std::runtime_error("cannot read ISO 9660 directory as a file");
    if(entry.size>read_limit)throw std::runtime_error("ISO 9660 file exceeds 128 MiB read limit");
    std::vector<std::uint8_t> result(entry.size);
    std::array<std::uint8_t,2048> bytes{};
    for(std::size_t at=0;at<result.size();at+=2048u) {
        read_sector(entry.lba+static_cast<std::uint32_t>(at/2048u),bytes);
        std::copy_n(bytes.begin(),std::min<std::size_t>(2048u,result.size()-at),result.begin()+at);
    }
    return result;
}
std::string DiscImage::boot_path() {
    const auto bytes=read("SYSTEM.CNF;1");
    if(bytes.size()>65536)throw std::runtime_error("SYSTEM.CNF exceeds size limit");
    std::string_view text(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    std::string boot;
    bool ps1=false;
    while(!text.empty()) {
        const auto end=text.find_first_of("\r\n");const auto line=text.substr(0,end);
        const auto equal=line.find('=');
        if(equal!=line.npos) {
            const auto key=upper(trim(line.substr(0,equal)));
            if(key=="BOOT")ps1=true;
            if(key=="BOOT2") {
                if(!boot.empty())throw std::runtime_error("duplicate BOOT2 in SYSTEM.CNF");
                boot=trim(line.substr(equal+1));
            }
        }
        if(end==text.npos)break;
        text.remove_prefix(end+1);
    }
    if(boot.empty())throw std::runtime_error(ps1?"PlayStation 1 BOOT executable is not an EE ELF":"SYSTEM.CNF has no BOOT2");
    if(!upper(boot).starts_with("CDROM0:"))throw std::runtime_error("BOOT2 device must be cdrom0:");
    boot.erase(0,7);
    if(components(boot).empty())throw std::runtime_error("empty BOOT2 path");
    return boot;
}
ElfImage DiscImage::boot_elf(){return parse_ee_elf(read(boot_path()));}
} // namespace ps2native
