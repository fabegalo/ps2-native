// CDVD register/device behavior reuses ps2-native b2959c8^ src/iop.cpp (BSD-3-Clause).
#include "iop_cdvd.hpp"
#include "ps2native/engine/console.hpp"
#include <format>
#include <chrono>
namespace ps2native::engine {
// CAV timing, DVD TOC and cooked-sector framing adapt pinned PCSX2 CDVD.cpp/CDVDisoReader.cpp (GPL-3.0-or-later).
// Its CD radius approximation is a deterministic device model, not a hardware timing certification.
static std::uint64_t sector_duration(bool dvd,std::uint8_t mode,std::uint32_t lba,std::uint32_t layer1=0){
    const auto spindle=mode&0x3f;
    constexpr unsigned cd_speed[]={24,1,2,4,12,24},dvd_speed[]={4,1,2,4};
    if((mode&0x40) || spindle>=(dvd?4u:6u))throw std::runtime_error("Unsupported CDVD spindle mode");
    if(mode&0x80){
        const auto offset=dvd && layer1 && lba>=layer1?layer1:0;
        const float radial=(static_cast<float>(lba-offset)/(dvd?2298496.0f:360000.0f))*0.60f+0.40f;
        const auto rate=dvd?675u*dvd_speed[spindle]:75u*cd_speed[spindle];
        return static_cast<std::uint32_t>(36864000.0f/(static_cast<float>(rate)*radial))*8ull;
    }
    return dvd?((36864000ull*2048)/(1382400ull*dvd_speed[spindle]))*8:(491520ull/cd_speed[spindle])*8;
}
void IopCdvd::mount(const std::filesystem::path& path,Medium kind){
    if(due!=~0ull)throw std::runtime_error("CDVD mount during an operation");
    auto image=std::make_unique<DiscImage>(path);
    if(kind==Medium::Auto){if(!image->raw_sectors())throw std::runtime_error("Cooked CD/DVD image needs explicit --disc-type cd or dvd");kind=Medium::Cd;}
    if(image->raw_sectors() && kind!=Medium::Cd)throw std::runtime_error("Raw CD sectors cannot be DVD media");
    media=std::move(image);medium=kind;status=0x0a;
}
std::uint32_t IopCdvd::irq_line() const {
    if(!console.iop_intr_enabled)return ~0u;
    const std::uint32_t banks[]={dma.at(0x1f8010f4),dma.at(0x1f801574)};
    for(unsigned bank=0;bank<2;++bank){
        const auto value=banks[bank];if(!(value&0x80000000u))continue;
        const auto pending=(value>>24)&(value>>16)&(bank?0x3fu:0x7fu);
        for(unsigned bit=0;bit<(bank?6u:7u);++bit){
            if(!(pending&(1u<<bit)))continue;
            const auto code=(bank?40u:32u)+bit;const auto it=console.iop_irqs.find(code);
            if(it!=console.iop_irqs.end() && it->second.enabled)return code;
        }
    }
    if(irq){const auto it=console.iop_irqs.find(2);if(it!=console.iop_irqs.end() && it->second.enabled)return 2;}
    if((banks[0]|banks[1])&0x80000000u){const auto it=console.iop_irqs.find(3);if(it!=console.iop_irqs.end() && it->second.enabled)return 3;}
    return ~0u;
}
bool IopCdvd::irq_ready() const{return irq_line()!=~0u;}
void IopCdvd::update_dicr(){
    // Each DICR bank owns its master condition; either bank can assert the shared DMA line.
    for(const auto reg:{0x1f8010f4u,0x1f801574u}){
        auto& value=dma.at(reg);const auto mask=reg==0x1f8010f4?0x7fu:0x3fu;
        const bool active=(value&0x8000) || ((value&0x800000) && ((value>>16)&(value>>24)&mask));
        value=(value&0x7fffffff)|(active?0x80000000u:0);
    }
}

std::uint32_t IopCdvd::read(std::uint32_t a,unsigned bytes){
    if(const auto it=dma.find(a&~3u);it!=dma.end()){
        if((bytes!=1 && bytes!=2 && bytes!=4) || (a&(bytes-1)) || (a&3)+bytes>4)throw std::runtime_error("Invalid IOP DMA access width/alignment");
        return (it->second>>((a&3)*8))&static_cast<std::uint32_t>((1ull<<(bytes*8))-1);
    }
    if(bytes!=1)throw std::runtime_error("CDVD requires byte access");
    switch(a){
    case 0x1f402004:return ncommand;
    case 0x1f402005:return due!=~0ull ? 0x80:0x4c;
    case 0x1f402006:return error;
    case 0x1f402008:return irq;
    case 0x1f40200a:case 0x1f40200b:return status;
    case 0x1f40200f:return media ? (medium==Medium::Cd?0x12:0x14):0;
    case 0x1f402013:{
        if(!media || !spinning)return 0;
        const auto speed=spindle?spindle:(medium==Medium::Dvd?3:5);
        return medium==Medium::Dvd?speed+0xf:speed-1;
    }
    case 0x1f402016:return scommand;
    case 0x1f402017:return sresults.empty()?0x40:0;
    case 0x1f402018:{if(sresults.empty())return 0; // SDATAOUT empty read, as in pinned PCSX2 cdvdRead18.
        const auto v=sresults.front();sresults.pop_front();return v;}
    default:throw std::runtime_error(std::format("Unsupported CDVD read {:08X}",a));
    }
}
void IopCdvd::write(std::uint32_t a,std::uint32_t value,unsigned bytes){
    if(auto it=dma.find(a&~3u);it!=dma.end()){
        if((bytes!=1 && bytes!=2 && bytes!=4) || (a&(bytes-1)) || (a&3)+bytes>4)throw std::runtime_error("Invalid IOP DMA access width/alignment");
        const auto shift=(a&3)*8,mask=static_cast<std::uint32_t>(((1ull<<(bytes*8))-1)<<shift),payload=(value<<shift)&mask;a&=~3u;
        if(a==0x1f8010f4 || a==0x1f801574){const auto controls=mask&0xffffff;it->second=((it->second&0xffffff)&~controls)|(payload&controls)|((it->second&0x7f000000)&~payload);update_dicr();}else it->second=(it->second&~mask)|payload;
        if(a==0x1f8010c8 || a==0x1f801508 || a==0x1f8010f0 || a==0x1f801570){console.start_audio_dma(0);console.start_audio_dma(1);}
        if(a==0x1f801548 || a==0x1f801558 || a==0x1f801570){console.start_sio2_dma(0);console.start_sio2_dma(1);}return;}
    if(bytes!=1)throw std::runtime_error("CDVD requires byte access");
    switch(a){
    case 0x1f402008:irq&=~value;return;
    case 0x1f402005:if(nparams.size()>=16)throw std::runtime_error("CDVD N parameter FIFO overflow");nparams.push_back(value);return;
    case 0x1f402006:howto=value;return;
    case 0x1f402017:if(sparams.size()>=16)throw std::runtime_error("CDVD S parameter FIFO overflow");sparams.push_back(value);return;
    case 0x1f402016:scmd(value);return;
    case 0x1f402004:
        if(due!=~0ull)throw std::runtime_error("CDVD command while busy");
        if(value>6 && value!=8 && value!=9)throw std::runtime_error(std::format("Unsupported CDVD N command {:02X}",value));
        ncommand=value;error=0;
        if(value==9){if(nparams.size()!=1 || nparams[0])throw std::runtime_error("CDVD TOC parameters not integrated");due=console.ticks+8;}
        else if(value==6 || value==8){
            if(nparams.size()!=11 || howto!=(value==8?0x8c:0x80) || nparams[10]){std::string detail=std::format("CDVD read mode HOWTO={:02X} params",howto);for(const auto byte:nparams)detail+=std::format(" {:02X}",byte);throw std::runtime_error(detail);}
            const auto count=ps2::load<std::uint32_t>(nparams.data()+4);if(!count || count>1024)throw std::runtime_error("CDVD read exceeds bounded IOP transfer");
            read_lba=ps2::load<std::uint32_t>(nparams.data());remaining=count;
            const bool dvd=media && medium==Medium::Dvd;
            sector_ticks=sector_duration(dvd,nparams[9],read_lba,dvd?media->dvd_layer1_start().value_or(0):0);spindle=nparams[9]&0x3f;spinning=true;
            const auto delta=position>read_lba?position-read_lba:read_lba-position;
            const auto seek=delta<(dvd?16u:8u)?delta*sector_ticks:delta<(dvd?14764u:4371u)?1105920ull*8:3686400ull*8;
            due=console.ticks+seek+sector_ticks;status=delta?0x12:6;
        } else if(value==5){if(nparams.size()!=4)throw std::runtime_error("CDVD Seek requires an LBA");due=console.ticks+1105920ull*8;status=0x12;}
        else {if(value==3 || !nparams.empty())throw std::runtime_error("Unsupported CDVD command/parameters");due=console.ticks+8;}
        return;
    default:throw std::runtime_error(std::format("Unsupported CDVD write {:08X}",a));
    }
}
// DVD structure encoding adapted from pinned PCSX2 CDVDisoReader (GPL-3.0-or-later).
std::array<std::uint8_t,2048> IopCdvd::dvd_toc(){
    if(!media || medium!=Medium::Dvd)throw std::runtime_error("DVD TOC requires mounted DVD media");
    const auto layer=media->dvd_layer1_start();std::array<std::uint8_t,2048> toc{};
    toc[0]=layer?0x24:4;toc[1]=2;toc[2]=0xf2;toc[4]=layer?0x41:0x86;toc[5]=layer?0x95:0x72;
    toc[12]=1;toc[13]=2;toc[14]=layer?0x21:1;toc[15]=layer?0x10:0;toc[17]=3;
    const auto end=layer.value_or(media->user_sector_count())+0x30000u-1;
    for(unsigned i=0;i<4;++i)toc[20+i]=static_cast<std::uint8_t>(end>>(24-i*8));
    return toc;
}
void IopCdvd::complete(){
    due=~0ull;
    if(ncommand==9){
        if(!media){error=0x12;status=0;}
        else {
            auto& madr=dma.at(0x1f8010b0);auto& bcr=dma.at(0x1f8010b4);auto& chcr=dma.at(0x1f8010b8);
            if(!(chcr&0x1000000) || !(dma.at(0x1f8010f0)&0x8000)){due=console.ticks+8;return;}
            if(madr>console.iop_ram.size()-2048 || std::uint64_t{bcr&0xffff}*(bcr>>16)<512)throw std::runtime_error(std::format("Invalid CDVD TOC DMA descriptor MADR={:08X} BCR={:08X} CHCR={:08X}",madr,bcr,chcr));
            const auto toc=dvd_toc();spinning=true;std::copy(toc.begin(),toc.end(),console.iop_ram.begin()+madr);
            madr+=2048;bcr=0;chcr&=~0x1000000u;dma.at(0x1f8010f4)|=1u<<27;update_dicr();status=0x0a;
        }
    } else if(ncommand==6 || ncommand==8){
        auto& madr=dma.at(0x1f8010b0);auto& bcr=dma.at(0x1f8010b4);auto& chcr=dma.at(0x1f8010b8);
        if(!media){error=0x12;status=0;}
        else if(read_lba>=media->user_sector_count() || remaining>media->user_sector_count()-read_lba){error=0x20;status=0x0a;}
        else {
            if(!(chcr&0x1000000) || !(dma.at(0x1f8010f0)&0x8000)){due=console.ticks+8;return;}
            const auto bytes=ncommand==8?2064u:2048u,sector_words=bytes/4;
            const auto words=bcr&0xffff,blocks=bcr>>16;
            if(!words || words>sector_words || sector_words%words || blocks<sector_words/words || madr>console.iop_ram.size()-bytes)throw std::runtime_error("Invalid CDVD DMA descriptor/geometry");
            unsigned prefix=0;
            if(ncommand==8){
                if(medium!=Medium::Dvd)throw std::runtime_error("DVD read requires DVD media");
                const auto layer=media->dvd_layer1_start();const bool second=layer && read_lba>=*layer;
                const auto address=read_lba-(second?*layer:0)+0x30000;
                auto* frame=console.iop_ram.data()+madr;std::fill_n(frame,bytes,0);
                frame[0]=0x20|second;frame[1]=address>>16;frame[2]=address>>8;frame[3]=address;
                prefix=12; // PCSX2 cooked-image profile: physical IED/CPR/EDC are not supplied by ISO.
            }
            std::span<std::uint8_t,2048> target(console.iop_ram.data()+madr+prefix,2048);media->read_sector(read_lba,target);madr+=bytes;position=++read_lba;
            bcr-=((sector_words/words)<<16);if(!(bcr>>16)){chcr&=~0x1000000u;dma.at(0x1f8010f4)|=1u<<27;update_dicr();}
            if(--remaining){sector_ticks=sector_duration(medium==Medium::Dvd,nparams[9],read_lba,medium==Medium::Dvd?media->dvd_layer1_start().value_or(0):0);due=console.ticks+sector_ticks;status=6;return;}status=0x0a;
        }
        remaining=0;
    } else if(!media && ncommand>=2){error=0x12;status=0;}
    else if(ncommand==5){const auto lba=ps2::load<std::uint32_t>(nparams.data());if(lba>=media->user_sector_count())error=0x20;else position=lba;status=0x0a;}
    else if(ncommand==2 || ncommand==4){status=0x0a;spinning=ncommand==2;}
    nparams.clear();irq|=error?1:2;if(console.iop_next_tick<console.ticks)console.iop_next_tick=console.ticks+8;
}
void IopCdvd::scmd(std::uint8_t cmd){
    scommand=cmd;sresults.clear();
    switch(cmd){
    case 0x03:
        if(sparams.size()!=1 || sparams[0] || !hardware || !hardware->mechacon)throw std::runtime_error("CDVD version requires a matching measured console profile");
        for(const auto b:hardware->mechacon->version)sresults.push_back(b);break;
    case 0x05:if(!sparams.empty())throw std::runtime_error("CDVD sticky flags command has parameters");sresults.push_back(due!=~0ull?1:0);break;
    case 0x1b: // CancelPOffRdy; no physical power-off input is currently connected.
        if(!sparams.empty() || (irq&0x80))throw std::runtime_error("CDVD power-off cancellation with arguments/pending power-off is not integrated");
        sresults.push_back(0);break;
    case 0x08:{
        if(!sparams.empty())throw std::runtime_error("CDVD clock command has parameters");
        using namespace std::chrono;const sys_seconds now{seconds{rtc_epoch+console.ticks/294912000ull+9*3600}};const auto day=floor<days>(now);const year_month_day date{day};const hh_mm_ss time{now-day};
        const auto bcd=[](unsigned v){return static_cast<std::uint8_t>((v/10)*16+v%10);};
        sresults={0,bcd(time.seconds().count()),bcd(time.minutes().count()),bcd(time.hours().count()),0,bcd(unsigned(date.day())),bcd(unsigned(date.month())),bcd(int(date.year())-2000)};break;
    }
    default:throw std::runtime_error(std::format("Unsupported CDVD S command {:02X}",cmd));
    }
    sparams.clear();
}
}
