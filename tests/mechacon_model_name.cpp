// Original synthetic EEPROM/FIFO fixtures. No Sony firmware or NVM data.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> program(unsigned mode) {
    m20::Assembler a(kEeResetVector);a.la(8,0xBF402000);a.la(10,0xA0000100);
    for(unsigned part=0;part<2u;++part) {
        if(mode!=1){a.la(9,mode==3 ? 1u : mode==10 ? 255u : part*8u);a.sb(9,0x17,8);}
        if(mode==2)a.sb(9,0x17,8);
        a.la(9,0x17);a.sb(9,0x16,8);a.lbu(9,0x17,8);a.nop();a.sw(9,0x20+part*4,10);
        a.lbu(11,0x18,8);a.nop();a.sw(11,0x28+part*4,10);
        for(unsigned n=0;n<8u;++n){a.lbu(11,0x18,8);a.nop();a.sb(11,part*8+n,10);}
        a.lbu(9,0x17,8);a.nop();a.sw(9,0x30+part*4,10);
        if(mode==11){a.lbu(11,0x18,8);a.nop();}
    }
    a.la(9,1);a.sw(9,0x38,10);a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
}
int main() try {
    for(const auto board:{"SCPH-70004:GH-032-11","SCPH-70004:GH-032-32"})for(unsigned mode=0;mode<12u;++mode) {
        auto profile=*find_console_hardware(board);NvmImage nvm;nvm.bytes.resize(kNvmBytes,0xA5);
        nvm.model_name=profile.model;nvm.layout=profile.mechacon->layout;nvm.sha256=std::string(64,'a');
        const auto at=nvm_layout_info(nvm.layout).model_name;
        for(unsigned n=0;n<16u;++n)nvm.bytes[at+n]=n==6u ? 0u : static_cast<std::uint8_t>(0xF0u+n);
        if(mode==5)nvm.model_name="SYNTHETIC-FOREIGN";
        if(mode==6)nvm.layout=static_cast<NvmLayout>(255);
        if(mode==7)nvm.bytes.pop_back();if(mode==8)profile.mechacon.reset();const auto saved=nvm;
        const auto rom=program(mode);Iop iop;iop.reset(rom,mode==9 ? nullptr : &profile,mode==4 ? nullptr : &nvm);
        iop.ee_write(0x110,4,0x12345678);iop.run_until(600);
        if(mode==0) {
            require(!iop.stopped() && iop.ee_read(0x138,4)==1,"both model-name chunks complete through the real FIFO");
            for(unsigned n=0;n<16u;++n)require(iop.ee_read(0x100+n,1)==nvm.bytes[at+n],
                "the exact NVM bytes, NUL and padding are transferred without substituting the console label");
            require(iop.ee_read(0x110,4)==0x12345678,"model-name reads do not extend beyond sixteen bytes");
            for(unsigned p=0;p<2u;++p)require(iop.ee_read(0x120+p*4u,4)==0 && iop.ee_read(0x128+p*4u,4)==0 &&
                iop.ee_read(0x130+p*4u,4)==0x40,"each validated storage read has one status and exactly eight data bytes");
        } else {
            const auto reason=mode==1 || mode==2 || mode==3 || mode==10 ? "supported chunk offset" :
                mode==8 || mode==9 ? "selected MECHACON profile" : mode==11 ? "no result byte is pending" : "matching console NVM";
            require(iop.stopped() && iop.stop_reason()->find(reason)!=std::string::npos,
                "invalid requests and incompatible firmware data cannot fabricate model-name responses");
        }
        require(nvm.bytes==saved.bytes && nvm.sha256==saved.sha256,"model-name reads preserve every NVM byte and hash");
    }
    std::cout<<"MECHACON model-name chunks, byte fidelity, profile isolation and FIFO bounds passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
