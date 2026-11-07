#pragma once
#include "ps2native/io.hpp"
#include "ps2native/iop_aot.hpp"
#include "ps2native/memory.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace ps2native::cli {
// Passive validation/preparation only; never generates host code during a session.
struct IopAotOptions {
    std::filesystem::path directory;
    std::map<std::string,std::set<std::uint32_t>> captured;
    std::vector<std::filesystem::path> protected_inputs;
    std::vector<ObservedWord> current;
    std::string identity;
    std::uint32_t base{};
    static constexpr std::size_t capture_limit=16; // <=128 MiB of images, 32 output files.
    template<class C> bool parse(const std::string& flag,const C* native) {
        if(!flag.starts_with("--iop-capture="))return false;
        directory=std::filesystem::path(native).native().substr(14);
        if(directory.empty())throw std::runtime_error("--iop-capture requires a directory");
        return true;
    }
    void protect(const std::filesystem::path& output) const {
        for(const auto& input:protected_inputs)
            if(!input.empty() && file_exists(input) && file_exists(output) && files_equivalent(input,output))
                throw std::runtime_error("IOP capture must not overwrite an input or checkpoint");
    }
    void attach(Memory& memory,std::span<const std::filesystem::path> inputs,
                std::span<const std::uint8_t> rom0={},std::span<const std::uint8_t> rom1={}) {
        if(directory.empty())return;
        protected_inputs.assign(inputs.begin(),inputs.end());
        auto& iop=const_cast<Iop&>(memory.iop());
        iop.set_instruction_observer([this,&iop,rom0,rom1](std::uint32_t pc,std::uint32_t word) {
            const auto physical=pc&0x1FFFFFFFu;
            if(current.empty() || physical<base || (physical-base)/4u>=current.size() ||
                current[(physical-base)/4u].unknown || current[(physical-base)/4u].value!=word) {
                std::vector<ObservedWord> image;
                std::uint32_t next_base{};
                if(physical<kIopRamBytes) {
                    image.reserve(kIopRamBytes/4u);
                    for(unsigned at=0;at<kIopRamBytes;at+=4)image.push_back(iop.diagnostic_ram_observed(at));
                } else {
                    const bool dvd=physical>=0x1E000000u && physical<0x1E400000u;
                    next_base=dvd ? 0x1E000000u : 0x1FC00000u;
                    const auto bytes=dvd ? rom1 : rom0;
                    if(bytes.empty() || (bytes.size()&3u) || physical<next_base || physical-next_base>=bytes.size())
                        throw std::runtime_error("IOP capture needs the matching ROM image for this PC");
                    image.reserve(bytes.size()/4u);
                    for(std::size_t at=0;at<bytes.size();at+=4)image.push_back({std::uint32_t{bytes[at]}|
                        (std::uint32_t{bytes[at+1]}<<8u)|(std::uint32_t{bytes[at+2]}<<16u)|(std::uint32_t{bytes[at+3]}<<24u),0});
                }
                const auto bytes=encode_iop_aot_image(next_base,image);
                const auto next_identity=sha256_hex(bytes);
                if(!captured.contains(next_identity) && captured.size()>=capture_limit)
                    throw std::runtime_error("IOP capture limit reached (16 loaded images); preparation is incomplete");
                const auto output=directory/("iop_"+next_identity+".iop");protect(output);
                if(file_exists(output) && read_file(output)!=bytes)
                    throw std::runtime_error("Existing IOP capture disagrees with its identity; refusing overwrite");
                const auto metadata=directory/("iop_"+next_identity+".entries");protect(metadata);
                if(!captured.contains(next_identity) && file_exists(metadata)) {
                    const auto prior=read_file(metadata);
                    std::istringstream input(std::string(prior.begin(),prior.end()));
                    for(std::size_t count=0;;++count) {
                        input>>std::ws;if(input.eof())break;
                        std::uint64_t root{};
                        if(count>=image.size() || !(input>>root) || root>UINT32_MAX || (root&3u) || root<next_base ||
                            (root-next_base)/4u>=image.size() || image[(root-next_base)/4u].unknown)
                            throw std::runtime_error("Existing IOP entry metadata is invalid; refusing overwrite");
                        captured[next_identity].insert(static_cast<std::uint32_t>(root));
                    }
                }
                (void)write_binary_if_changed(output,bytes);
                current=std::move(image);base=next_base;identity=next_identity;
                std::cout<<"IOP_CAPTURE sha256="<<identity<<" base="<<base<<" path="<<path_utf8(output)<<'\n';
            }
            // A JR target can equal delay-PC+4. Every observed PC is an entry,
            // including transfers that happen to look sequential.
            captured[identity].insert(physical);
        });
    }
    void finish() const {
        for(const auto& [hash,roots]:captured) {
            const auto output=directory/("iop_"+hash+".entries");protect(output);
            std::ostringstream text;for(const auto pc:roots)text<<pc<<'\n';
            // Reuse prior verified roots only for this exact captured image.
            (void)write_text_if_changed(output,text.str());
        }
    }
};
} // namespace ps2native::cli
