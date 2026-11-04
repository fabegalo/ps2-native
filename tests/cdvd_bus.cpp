// Authored ROM0/NVM fixture. Both processors consume one physical result FIFO.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;
int main() try {
    auto rom=m20::build_firmware().rom;
    m20::Assembler a(kEeResetVector);
    a.lui(8,0xBF40);a.lui(9,0xA000);
    a.lbu(10,0x2018,8);a.nop();a.sb(10,0x100,9);
    a.lbu(10,0x2018,8);a.nop();a.sb(10,0x101,9);
    a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto program=m20::le_words(a.finish());std::copy(program.begin(),program.end(),rom.begin());
    const auto bios=load_bios_rom0(rom);
    const ConsoleHardware profile{"SYNTHETIC","CDVD-BUS",bios.version.romver,0x4111u,
        ProfileEvidence::Measured,"authored bus test",0x30u,"authored bus test",
        MechaconInfo{{0,6,0,0},{0,0,0},{0,0},NvmLayout::Dragon}};
    std::vector<std::uint8_t> bytes(kNvmBytes);
    const auto& layout=nvm_layout_info(NvmLayout::Dragon);
    std::copy(profile.model.begin(),profile.model.end(),bytes.begin()+layout.model_name);
    bytes[0x40]=0x5A;bytes[0x41]=0xA5;
    const auto nvm=load_nvm_image(bytes,profile);
    EEState state;Memory memory;prepare_ee_boot(bios,nullptr,state,memory,&profile,&nvm);
    require(classify_physical(0x1F402005u)==BusRegion::IopRegisters &&
            classify_physical(0x1F401FFFu)==BusRegion::Unmapped && classify_physical(0x1F402020u)==BusRegion::Unmapped,
            "only the physical CDVD register window is decoded");
    require(memory.read8(0xBF402017u)==0x40u,"EE sees the real empty MECHACON result FIFO");
    // ReadNVM word 0x20: result is status, high byte, low byte.
    memory.write8(0xBF402017u,0);memory.write8(0xBF402017u,0x20);memory.write8(0xBF402016u,0x0A);
    require(memory.read8(0x1F402017u)==0 && memory.read8(0xBF402018u)==0,
            "EE submits a real NVM command and consumes exactly its status byte");
    state.retired_instructions=8u*kEeCyclesPerIopCycle;
    require(memory.read16(0xBC000100u)==0x5AA5u,"IOP executes reads of the remaining actual NVM bytes from the same FIFO");
    require(memory.read8(0xBF402017u)==0x40u,"IOP consumption is visible to the EE without duplicated results");
    device_refused([&]{(void)memory.read8(0xBF402018u);},"no result byte");
    device_refused([&]{(void)memory.read32(0xBF402004u);},"device not implemented");
    device_refused([&]{memory.write32(0xBF402014u,0);},"device not implemented");
    device_refused([&]{(void)memory.read8(0xBF40201Fu);},"device not implemented");
    // The query returns this exact profile, without a generic emulator version
    // or a synthetic success byte before its four version bytes.
    memory.write8(0xBF402017u,0);memory.write8(0xBF402016u,3);
    for(const auto byte:profile.mechacon->version)
        require(memory.read8(0xBF402018u)==byte,"MECHACON version follows the selected profile in byte order");
    require(memory.read8(0xBF402017u)==0x40u,"version response is exactly four bytes");
    Iop unknown;unknown.reset(rom,nullptr);unknown.ee_cdvd_write(0x1F402017u,1,0);
    rejects([&]{unknown.ee_cdvd_write(0x1F402016u,1,3);},"no measured MECHACON version");
    unknown.reset(rom,&profile);unknown.ee_cdvd_write(0x1F402017u,1,0x30u);
    rejects([&]{unknown.ee_cdvd_write(0x1F402016u,1,3);},"only the parameter-free MechaconVersion");
    prepare_ee_boot(bios,nullptr,state,memory,&profile);
    memory.write8(0xBF402017u,0);memory.write8(0xBF402017u,0x20);
    device_refused([&]{memory.write8(0xBF402016u,0x0A);},"no NVM image is supplied");
    Iop stopped;stopped.reset({},&profile);
    rejects([&]{(void)stopped.ee_cdvd_read(0x1F402005u,1);},"after the IOP stopped");
    rejects([&]{stopped.ee_cdvd_write(0x1F402017u,1,0);},"after the IOP stopped");
    std::cout<<"EE/IOP share physical CDVD commands, actual NVM bytes and one result FIFO\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
