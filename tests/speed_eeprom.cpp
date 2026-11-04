#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/microwire.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void command(Microwire64x16& chip,unsigned word) {
    chip.drive(false,false,false);
    for(int b=8;b>=0;--b){const bool bit=(word&(1u<<b))!=0;chip.drive(true,false,bit);chip.drive(true,true,bit);}
}
std::uint16_t read_word(Microwire64x16& chip) {
    std::uint16_t value=0;
    for(unsigned bit=0;bit<16;++bit) {
        chip.drive(true,false,false);chip.drive(true,true,false);
        const auto sample=chip.data_out();require(sample.has_value(),"backed EEPROM supplies each real bit");
        value=static_cast<std::uint16_t>((value<<1u)|(*sample ? 1u : 0u));
        chip.drive(true,true,true);require(chip.data_out()==sample,"held high clock never shifts another bit");
    }
    return value;
}
void protocol() {
    std::array<std::uint16_t,64> data{};for(unsigned n=0;n<64;++n)data[n]=static_cast<std::uint16_t>((n*0x421u)^0xA55Au);
    const auto original=data;Microwire64x16 chip;require(!chip.data_out(),"missing image never implies an erased or absent device");
    chip.attach(data);require(!chip.data_out(),"deselected EEPROM output is high impedance");
    for(unsigned address=0;address<64;++address) {
        command(chip,0x180u|address);require(chip.data_out().has_value() && !*chip.data_out(),"READ starts with its documented dummy zero");
        require(read_word(chip)==data[address],"all word addresses preserve MSB-first payloads");
    }
    command(chip,0x180u|63u);for(unsigned n=0;n<130;++n)require(read_word(chip)==data[(63u+n)&63u],"sequential reads wrap exactly at word 64");
    require(chip.words_read()==194 && chip.commands()==65 && data==original,"counters count clocked words and reads never modify the supplied image");
    rejects([&]{chip.attach(std::span<const std::uint16_t>(data).first(63));},"exactly 64");
    rejects([&]{command(chip,0x140);},"programming/control opcode");command(chip,0x180);require(read_word(chip)==data[0],"unsupported programming cannot mutate data");
    chip.reset();command(chip,0x180);require(!chip.data_out(),"reset removes both image and presence assumptions");
    chip.diagnostic_presence(true);command(chip,0x180);require(chip.data_out().has_value() && !*chip.data_out(),"explicit presence defines the protocol dummy, never contents");
    chip.drive(true,false,false);chip.drive(true,true,false);require(!chip.data_out(),"unprovided content remains unknown on the first data edge");
    chip.diagnostic_presence(false);command(chip,0x180);require(chip.data_out()==true && !chip.commands(),"explicit disconnected-wire fault reports a high input without a fabricated read completion");
}
std::vector<std::uint8_t> gpio_program(unsigned delay) {
    m20::Assembler a(kEeResetVector);a.la(8,0xB0000000);a.la(10,0xA0000100);
    const auto out=[&](unsigned offset,unsigned value){a.la(9,value);a.sb(9,offset,8);for(unsigned n=0;n<delay;++n)a.nop();};
    out(0x2C,0xE1);out(0x2E,0);out(0x2E,0x80);
    for(int b=8;b>=0;--b){const auto low=0x80u|((0x180u&(1u<<b)) ? 0x20u : 0u);out(0x2E,low);out(0x2E,low|0x40u);}
    // An identical clock-high write is the same edge, including the dummy.
    out(0x2E,0xC0);a.lbu(11,0x2E,8);a.nop();a.andi(11,11,0x10);a.addiu(12,11,1);a.sw(11,8,10);
    out(0x2E,0x80);a.la(12,0);
    for(unsigned bit=0;bit<16;++bit) {
        out(0x2E,0xC0);a.lbu(11,0x2E,8);a.nop();a.andi(11,11,0x10);
        a.r(0,12,12,1,0);a.r(0,11,11,4,2);a.r(12,11,12,0,0x25);out(0x2E,0x80);
    }
    a.sw(12,0,10);out(0x2E,0);out(0x2C,1);a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto bytes=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(bytes.begin(),bytes.end(),rom.begin());return rom;
}
void gpio() {
    const auto* profile=find_console_hardware("SCPH-70004:GH-032-32");
    NvmImage nvm;nvm.bytes.resize(kNvmBytes);nvm.sha256=std::string(64,'a');nvm.model_name="SCPH-70004";
    const auto saved=nvm.bytes;std::array<std::uint16_t,64> data{};data[0]=0xA55A;
    const auto rom=gpio_program(20);Iop iop;iop.reset(rom,profile,&nvm);iop.diagnostic_dev9_power_reset(5);
    rejects([&]{iop.attach_speed_eeprom_words(data,std::string(64,'b'));},"NVM hash binding");
    iop.attach_speed_eeprom_words(data,nvm.sha256);iop.run_until(3000);
    require(!iop.stopped() && iop.ee_read(0x100,4)==0xA55A && !iop.ee_read(0x108,4) && iop.speed_eeprom_words_read()==1,
            "IOP GPIO clocks the actual backed word with a real dummy bit and proper propagation waits");
    require(nvm.bytes==saved && data[0]==0xA55A,"GPIO reads preserve user NVM and EEPROM source contents");
    rejects([&]{iop.attach_speed_eeprom_words(data,nvm.sha256);},"before execution");
    iop.reset(rom,profile,&nvm);iop.diagnostic_dev9_power_reset(5);iop.run_until(3000);
    require(iop.stopped() && iop.stop_reason()->find("SPEED GPIO input/output bits are unestablished")!=std::string::npos,
            "no image or explicit presence cannot silently return valid MAC bits");
    iop.reset(rom,profile,&nvm);iop.diagnostic_dev9_power_reset(5);iop.diagnostic_speed_eeprom_presence(false);iop.run_until(3000);
    require(!iop.stopped() && iop.ee_read(0x100,4)==0xFFFF && iop.ee_read(0x108,4)==0x10 && !iop.speed_eeprom_words_read(),
            "the isolated disconnected-input fault remains an error dummy, not fabricated EEPROM success");
    const auto fast=gpio_program(0);iop.reset(fast,profile,&nvm);iop.diagnostic_dev9_power_reset(5);iop.attach_speed_eeprom_words(data,nvm.sha256);iop.run_until(1000);
    require(iop.stopped() && iop.stop_reason()->find("propagation boundary")!=std::string::npos,"too-early samples cannot invent instant hardware propagation");
}
}
int main() try {protocol();gpio();std::cout<<"Microwire READ, actual GPIO words, dummy/hold/wrap, binding and unknown/fault inputs passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
