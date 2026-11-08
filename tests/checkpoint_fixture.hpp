#pragma once
#include "boot_checks.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/sha256.hpp"

namespace checkpoint_fixture {
using namespace ps2native;
using boot_checks::require;

inline std::vector<std::uint8_t> legacy_envelope(CheckpointInfo info,std::span<const std::uint8_t> payload,
    const char* schema="771b628039a2b4c73a7c836b022c56d167cf8cd22310d8fe75803cb7934d3f88",bool current_layout=false,bool keep_cpu_bounds=false) {
    std::vector<std::uint8_t> old_payload(payload.begin(),payload.end());
    const std::array<std::uint8_t,8> smap_tag{'S','M','A','P','R','S','T','1'};
    const auto smap=std::search(old_payload.begin(),old_payload.end(),smap_tag.begin(),smap_tag.end());
    std::array<std::uint8_t,43120> absent_smap{};
    std::copy(smap_tag.begin(),smap_tag.end(),absent_smap.begin());
    absent_smap[2072]=absent_smap[2073]=absent_smap[2140]=absent_smap[2141]=0xFF;
    require(smap!=old_payload.end() && smap+absent_smap.size()<=old_payload.end() &&
            std::equal(absent_smap.begin(),absent_smap.end(),smap),
            "legacy fixtures predate SMAP and must have no established register state or timing");
    old_payload.erase(smap,smap+absent_smap.size());
    const std::array<std::uint8_t,8> cpu_tag{'G','P','R','B','N','D','0','1'};
    for(unsigned reg=0;!keep_cpu_bounds && reg<32;++reg) {
        const auto at=std::search(old_payload.begin(),old_payload.end(),cpu_tag.begin(),cpu_tag.end());
        require(at!=old_payload.end() && at+12<=old_payload.end() && std::all_of(at+8,at+12,[](auto b){return b==0;}),"legacy CPU fixture has no new lane certificates");
        old_payload.erase(at,at+12);
    }
    if(!current_layout) {
    const std::array<std::uint8_t,8> bus_tag{'G','I','F','B','U','S','0','1'};
    const auto bus=std::search(old_payload.begin(),old_payload.end(),bus_tag.begin(),bus_tag.end());
    require(bus!=old_payload.end(),"current checkpoint contains the graphics arbitration cursor");old_payload.erase(bus,bus+12);
    if(std::string_view(schema)!="25e615e5098e581ee91b9dd84ffe9782be7b73f47ebfa76e377caa06fa871ffd") {
    const std::array<std::uint8_t,8> irq_tag{'V','I','F','I','R','Q','0','1'};
    for(unsigned unit=0;unit<2;++unit) {
        const auto at=std::search(old_payload.begin(),old_payload.end(),irq_tag.begin(),irq_tag.end());
        require(at!=old_payload.end() && at[8]==0 && at[9]==0 && at[10]==0,"legacy VIF has no interrupt state");
        old_payload.erase(at,at+11);
    }
    if(std::string_view(schema)!="fa9cf50ce0d0fade6cc76b98bd076cdc6fad4e6bd5db34d76b2dfbe586011827") {
    const auto u64=[&](std::size_t& pos){require(pos+8<=old_payload.size(),"legacy extension count is bounded");std::uint64_t value=0;for(unsigned b=0;b<8;++b)value|=std::uint64_t{old_payload[pos++]}<<(8u*b);return value;};
    const auto absent=[&](std::size_t& pos,std::uint64_t count){require(count<=65536,"legacy certificate count is bounded");while(count--){require(pos<old_payload.size() && old_payload[pos]==0,"legacy fixture has no newly introduced numeric certificates");++pos;}};
    const std::array<std::uint8_t,8> vif_bounds{'V','I','F','B','N','D','0','1'},vu_data{'V','U','D','B','N','D','0','1'};
    for(unsigned unit=0;unit<2;++unit) {
        const auto at=std::search(old_payload.begin(),old_payload.end(),vif_bounds.begin(),vif_bounds.end());require(at!=old_payload.end(),"current VIF bounds extension exists");
        const auto begin=static_cast<std::size_t>(at-old_payload.begin());auto pos=begin+8;const auto count=u64(pos);absent(pos,8u+4u*count);old_payload.erase(old_payload.begin()+begin,old_payload.begin()+pos);
    }
    const auto at=std::search(old_payload.begin(),old_payload.end(),vu_data.begin(),vu_data.end());require(at!=old_payload.end(),"current VU data bounds extension exists");
    const auto begin=static_cast<std::size_t>(at-old_payload.begin());auto pos=begin+8;
    for(unsigned unit=0;unit<2;++unit){const auto count=u64(pos);absent(pos,count);}old_payload.erase(old_payload.begin()+begin,old_payload.begin()+pos);
    if(std::string_view(schema)!="b36ce4ca5c0bf510d302b10b002b27d736cb1fbc4e258e8a2b8961950e2d18e3") {

    const std::array<std::uint8_t,8> bounds_tag{'V','U','M','B','N','D','0','1'};
    auto bounds_at=std::search(old_payload.begin(),old_payload.end(),bounds_tag.begin(),bounds_tag.end());
    require(bounds_at!=old_payload.end(),"current checkpoint has tagged VU numeric bounds");
    // This legacy fixture has no active numeric certificate: each optional is one byte.
    constexpr unsigned certificates=33u*4u+2u*(8u+4u)*4u;
    require(std::all_of(bounds_at+8,bounds_at+8+certificates,[](auto b){return b==0;}),"legacy fixture has no new certificates to erase");
    old_payload.erase(bounds_at,bounds_at+8+certificates);
    const std::array<std::uint8_t,8> spr_tag{0x5F,0x53,0x50,0x52,0x44,0x4D,0x41,0x31};
    const auto spr_at=std::search(old_payload.begin(),old_payload.end(),spr_tag.begin(),spr_tag.end());
    require(spr_at!=old_payload.end(),"current checkpoint has a tagged fromSPR clock extension");
    if(std::string_view(schema)!="5662ca9c89ac26516224d7aa8f3e777e8d72ec24255364cfedd3481e13db1882" && std::string_view(schema)!="fe6e8c07cc00203aa30906e155d9511c731237aa3919857a36a7098ea4409051")old_payload.erase(spr_at,spr_at+16);
    const std::array<std::uint8_t,8> voice_tag{0x53,0x50,0x32,0x4E,0x41,0x58,0x41,0x31};
    const auto voice_at=std::search(old_payload.begin(),old_payload.end(),voice_tag.begin(),voice_tag.end());
    require(voice_at!=old_payload.end(),"current checkpoint has a tagged voice readback policy");
    if(std::string_view(schema)!="fe6e8c07cc00203aa30906e155d9511c731237aa3919857a36a7098ea4409051")old_payload.erase(voice_at,voice_at+9);
    const std::array<std::uint8_t,8> vif_tag{'V','I','F','U','N','K','0','1'};
    for(unsigned unit=0;unit<2;++unit) {
        const auto at=std::search(old_payload.begin(),old_payload.end(),vif_tag.begin(),vif_tag.end());
        require(at!=old_payload.end(),"current checkpoint has tagged VIF uncertainty");
        std::uint64_t count=0;for(unsigned b=0;b<8;++b)count|=std::uint64_t{at[8+b]}<<(8u*b);
        require(count<=16,"synthetic VIF FIFO count is bounded");
        old_payload.erase(at,at+48+count*16);
    }}
    }
    }
    }
    payload=old_payload;
    std::vector<std::uint8_t> bytes{'P','S','2','N','C','P','0','1'};
    const auto integer=[&](std::uint64_t value,unsigned size){for(unsigned i=0;i<size;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(8u*i)));};
    info.schema_sha256=schema;
    info.payload_sha256=sha256_hex(payload);info.payload_bytes=payload.size();
    for(const auto* value:{&info.schema_sha256,&info.runtime_sha256,&info.bindings_sha256,&info.scenario_sha256,&info.host_metadata,&info.payload_sha256}) {
        integer(value->size(),8);bytes.insert(bytes.end(),value->begin(),value->end());
    }
    integer(info.retired,8);integer(info.cycles,8);integer(info.payload_bytes,8);integer(info.pc,4);
    bytes.insert(bytes.end(),payload.begin(),payload.end());return bytes;
}
} // namespace checkpoint_fixture
