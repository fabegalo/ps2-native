// Original command-framing fixtures. No firmware, NVM or success byte bundled.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> program(std::span<const std::uint8_t> input,unsigned consume=0,std::uint8_t command=0x1A) {
    m20::Assembler a(kEeResetVector);a.la(8,0xBF402000);a.la(10,0xA0000100);
    for(const auto byte:input){a.la(9,byte);a.sb(9,0x17,8);}
    a.la(9,command);a.sb(9,0x16,8);a.lbu(9,0x17,8);a.nop();a.sw(9,4,10);
    a.lbu(11,0x18,8);a.nop();a.sb(11,0,10); // Opaque transport does not consume an authentication result.
    a.lbu(9,0x17,8);a.nop();a.sw(9,8,10);a.andi(12,11,0);a.sw(12,12,10);
    a.la(9,1);a.sw(9,16,10);
    if(consume==1 || consume==4)a.addiu(12,11,1);
    if(consume==3){a.lbu(11,0,10);a.nop();a.andi(12,11,0x80);a.addiu(12,12,1);a.sw(12,20,10);}
    if(consume==2){a.lbu(11,0x18,8);a.nop();a.addiu(12,11,1);a.beq(12,0,"idle");a.nop();}
    a.label("idle");a.beq(0,0,"idle");a.nop();const auto code=m20::le_words(a.finish());
    std::vector<std::uint8_t> rom(4u*1024u*1024u);std::copy(code.begin(),code.end(),rom.begin());return rom;
}
NvmImage nvm_for(const ConsoleHardware& profile) {
    NvmImage nvm;nvm.bytes.resize(kNvmBytes);nvm.model_name=profile.model;nvm.layout=profile.mechacon->layout;
    nvm.sha256=std::string(64,'a');return nvm;
}
}
int main() try {
    const std::array<std::uint8_t,4> request{2,0,'E','C'};
    for(const auto board:{"SCPH-70004:GH-032-11","SCPH-70004:GH-032-32"})for(unsigned consume=0;consume<3;++consume) {
        const auto* profile=find_console_hardware(board);const auto nvm=nvm_for(*profile),saved=nvm;
        const auto rom=program(request,consume);Iop iop;iop.reset(rom,profile,&nvm);
        require(!iop.boot_certify_request(),"reset has no certification attempt or success");
        iop.ee_write(0x100,4,0xA55A5AA5);iop.run_until(300);
        require(iop.boot_certify_request()==request,"the actual four ROM-name bytes are recorded independently of certification outcome");
        require(!iop.diagnostic_ram32(0x100) && iop.diagnostic_ram32(0x104)==0 &&
            iop.diagnostic_ram32(0x108)==0x40 && iop.diagnostic_ram32(0x10C)==0 && iop.diagnostic_ram32(0x110)==1,
            "one opaque reply travels through the FIFO/RAM; result-ready and FIFO-empty follow actual consumption");
        if(!consume)require(!iop.stopped() && iop.ee_read(0x101,1)==0x5A && iop.ee_read(0x103,1)==0xA5,
            "unused authentication status cannot stop transport or corrupt adjacent bytes");
        else require(iop.stopped() && iop.stop_reason()->find(consume==1 ? "BootCertify result is unmeasured" : "no result byte is pending")!=std::string::npos,
            "consuming unmeasured authentication or reading beyond the single reply never fabricates data");
        require(nvm.bytes==saved.bytes && nvm.sha256==saved.sha256,"certification framing preserves all NVM bytes and identity");
        iop.reset(rom,profile,&nvm);require(!iop.boot_certify_request(),"reset removes prior certification state");
    }
    for(unsigned invalid=0;invalid<10;++invalid) {
        auto profile=*find_console_hardware("SCPH-70004:GH-032-32");auto nvm=nvm_for(profile);
        std::vector<std::uint8_t> input(request.begin(),request.end());
        if(invalid==0)input.pop_back();if(invalid==1)input.push_back(0);
        if(invalid==2)input[0]=1;if(invalid==3)input[2]='A';if(invalid==4)input[3]='T';
        if(invalid==5)nvm.model_name="SYNTHETIC-OTHER";if(invalid==6)nvm.bytes.pop_back();
        if(invalid==7)profile.mechacon.reset();if(invalid==8)profile.romver="02A0EC20040614";
        const auto rom=program(input);Iop iop;iop.reset(rom,&profile,invalid==9 ? nullptr : &nvm);iop.run_until(300);
        require(iop.stopped() && !iop.boot_certify_request(),"invalid lengths, foreign ROM names, missing profiles/NVM and malformed versions cannot certify or return success");
    }
    {
        auto profile=*find_console_hardware("SCPH-70004:GH-032-32");profile.model="SYNTHETIC";profile.romver="0170JC20000101";
        const auto nvm=nvm_for(profile);const std::array<std::uint8_t,4> decimal{1,70,'J','C'};
        const auto rom=program(decimal);Iop iop;iop.reset(rom,&profile,&nvm);iop.run_until(300);
        require(!iop.stopped() && iop.boot_certify_request()==decimal,"ROM major/minor input uses decimal pairs, independently of region and kind");
    }
    for(unsigned mode=0;mode<3;++mode) {
        const auto* profile=find_console_hardware("SCPH-70004:GH-032-32");const auto nvm=nvm_for(*profile);
        const std::array<std::uint8_t,1> invalid{0};
        const auto rom=program(mode==2 ? std::span<const std::uint8_t>{invalid} : std::span<const std::uint8_t>{},mode==1 ? 1u : 0u,0x15);
        Iop iop;iop.reset(rom,profile,&nvm);iop.run_until(300);
        if(mode==2)require(iop.stopped() && !iop.dvd_playback_forbid_requested() &&
            iop.stop_reason()->find("takes no parameters")!=std::string::npos,"ForbidDVDP rejects malformed framing without success");
        else {
            require(iop.dvd_playback_forbid_requested() && !iop.boot_certify_request() &&
                !iop.diagnostic_ram32(0x100) && iop.diagnostic_ram32(0x110)==1,
                "ForbidDVDP records its actual request and transports one opaque acknowledgement without granting permissions");
            if(mode)require(iop.stopped() && iop.stop_reason()->find("ForbidDVDP acknowledgement")!=std::string::npos,
                "an acknowledgement consumer still refuses unmeasured status");
            else require(!iop.stopped(),"an unused acknowledgement remains transportable");
        }
        iop.reset(rom,profile,&nvm);require(!iop.dvd_playback_forbid_requested(),"reset removes the playback restriction request");
    }
    for(unsigned consume:{3u,4u})for(const bool assume:{false,true}) {
        const auto* profile=find_console_hardware("SCPH-70004:GH-032-32");const auto nvm=nvm_for(*profile);
        const auto rom=program({},consume,0x15);Iop iop;iop.reset(rom,profile,&nvm);
        if(assume)iop.diagnostic_forbid_dvdp_error_bit_clear();iop.run_until(300);
        if(assume && consume==3)require(!iop.stopped() && iop.ee_read(0x114,4)==1,
            "the explicitly assumed bit can be consumed without defining any other response bit");
        else require(iop.stopped() && iop.stop_reason()->find(consume==3 ? "LBU of IOP RAM" : "ForbidDVDP acknowledgement")!=std::string::npos,
            "default error-bit consumption and full-byte consumption remain unknown even in the experiment");
        rejects([&]{iop.diagnostic_forbid_dvdp_error_bit_clear();},"before execution");
        iop.reset(rom,profile,&nvm);iop.run_until(300);require(iop.stopped(),"reset removes the single-bit reply assumption");
    }
    for(const auto board:{"SCPH-70004:GH-032-11","SCPH-70004:GH-032-32"}) {
        const auto* profile=find_console_hardware(board);const auto nvm=nvm_for(*profile);
        Iop ok;ok.reset(program({},1,0x1B),profile,&nvm);ok.ee_write(0x100,4,0xA55A5AA5);ok.run_until(300);
        require(!ok.stopped() && ok.ee_read(0x100,1)==0 && ok.ee_read(0x101,1)==0x5A &&
            ok.diagnostic_ram32(0x104)==0 && ok.diagnostic_ram32(0x108)==0x40,
            "CancelPOffRdy returns one known zero status byte when no host power-off request is pending");
        Iop overread;overread.reset(program({},2,0x1B),profile,&nvm);overread.run_until(300);
        require(overread.stopped() && overread.stop_reason()->find("no result byte is pending")!=std::string::npos,
            "CancelPOffRdy exposes a single status byte");
        const std::array<std::uint8_t,1> extra{0};
        Iop bad_param;bad_param.reset(program(extra,0,0x1B),profile,&nvm);bad_param.run_until(300);
        require(bad_param.stopped() && bad_param.stop_reason()->find("CancelPOffRdy takes no parameters")!=std::string::npos,
            "CancelPOffRdy rejects unexpected parameter bytes");
        auto no_mecha=*profile;no_mecha.mechacon.reset();
        Iop unprofiled;unprofiled.reset(program({},0,0x1B),&no_mecha,&nvm);unprofiled.run_until(300);
        require(unprofiled.stopped() && unprofiled.stop_reason()->find("selected console MECHACON profile")!=std::string::npos,
            "CancelPOffRdy requires a selected console MECHACON profile");
    }
    DiagnosticAssumptions explicit_input;require(explicit_input.empty(),"default assumptions remain empty");
    explicit_input.mechacon_forbid_dvdp_error_bit_clear=true;require(!explicit_input.empty(),"the bit experiment always marks execution diagnostic");
    std::cout<<"BootCertify framing, profile binding, opaque reply transport, FIFO bounds and reset checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
