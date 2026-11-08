#pragma once
#include "ps2native/io.hpp"
#include "ps2native/memory.hpp"
#include "ps2native/vu_aot.hpp"
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace ps2native::cli {
// Explicit, local offline workflow. Capture is opt-in and never compiles code.
struct VuAotOptions {
    std::filesystem::path directory;
    std::map<std::string,std::set<std::uint32_t>> captured;
    std::vector<std::filesystem::path> protected_inputs;
    bool capture_limit_reported{};
    static constexpr std::size_t capture_limit=256;
    template<class C> bool parse(const std::string& flag,const C* native) {
        if(flag.starts_with("--vu1-capture=")) {
            directory=std::filesystem::path(native).native().substr(14);
            if(directory.empty())throw std::runtime_error("--vu1-capture requires a directory");
        } else return false;
        return true;
    }
    void protect(const std::filesystem::path& output) const {
        for(const auto& input:protected_inputs)
            if(!input.empty() && file_exists(input) && file_exists(output) && files_equivalent(input,output))
                throw std::runtime_error("VU1 capture must not overwrite an input or checkpoint");
    }
    void attach(Memory& memory,std::span<const std::filesystem::path> inputs) {
        protected_inputs.assign(inputs.begin(),inputs.end());
        if(directory.empty())return;
        memory.set_vu1_capture([this](const VuControl& vu,std::uint32_t entry) {
            const auto image=vu.capture_vu1_micro();
            const bool carry=vu.vu1_diagnostic_power_two_carry();
            const auto identity=vu1_aot_identity(image,carry);
            auto previous=captured.find(identity);
            if(previous!=captured.end() && previous->second.contains(entry))return;
            if(previous==captured.end() && captured.size()>=capture_limit) {
                if(!capture_limit_reported) {
                    std::cout<<"VU1_CAPTURE_LIMIT unique_images="<<capture_limit<<"; further exports omitted\n";
                    capture_limit_reported=true;
                }
                return;
            }
            const auto capture=directory/("vu1_"+identity+".vu1");
            const auto metadata=directory/("vu1_"+identity+".entries");
            protect(capture);protect(metadata);
            auto bytes=encode_vu1_aot_image(image);
            if(file_exists(capture) && read_file(capture)!=bytes)
                throw std::runtime_error("Existing VU1 capture disagrees with its identity; refusing overwrite");
            auto& entries=captured[identity];
            if(file_exists(metadata)) {
                const auto prior=read_file(metadata);
                std::istringstream input(std::string(prior.begin(),prior.end()));
                std::string version,stored_identity,mode,label;
                if(!(input>>version>>stored_identity>>mode) || version!="ps2n-vu1-entries-v1" || stored_identity!=identity ||
                   mode!=(carry ? "power-two-carry" : "strict"))
                    throw std::runtime_error("Existing VU1 entry metadata is invalid; refusing overwrite");
                while(input>>label) {
                    std::uint32_t root{};
                    if(label!="entry" || !(input>>root) || root>=16384u || (root&7u))
                        throw std::runtime_error("Existing VU1 entry metadata is invalid; refusing overwrite");
                    entries.insert(root);
                }
            }
            entries.insert(entry);
            std::ostringstream text;
            text<<"ps2n-vu1-entries-v1 "<<identity<<' '<<(carry ? "power-two-carry" : "strict")<<'\n';
            for(const auto root:entries)text<<"entry "<<root<<'\n';
            (void)write_binary_if_changed(capture,bytes);
            (void)write_text_if_changed(metadata,text.str());
            std::cout<<"VU1_CAPTURE identity="<<identity<<" entry_byte_pc="<<entry
                     <<" mode="<<(carry ? "power-two-carry" : "strict")
                     <<" path="<<path_utf8(capture)<<'\n';
        });
    }
    void print(const Memory& memory) const {
        const auto& stats=memory.vu1_aot_stats();
        std::cout<<"VU1_EXECUTION native="<<stats.native_instructions
                 <<" interpreted="<<stats.interpreted_instructions
                 <<" identity_checks="<<stats.identity_checks
                 <<" missing_program="<<stats.missing_program<<" untranslated_pc="<<stats.untranslated_pc
                 <<" mode="<<"verified-offline-aot"<<'\n';
    }
};
} // namespace ps2native::cli
