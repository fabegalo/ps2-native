#pragma once
#include "ps2native/device_timing.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <vector>

namespace ps2native {
// PS2SDK smapregs.h: TX/RX FIFO CTRL bit 0 requests a self-clearing reset.
// Completion latency is an explicit experiment, not a measured reset default.
class SmapFifos {
    friend struct CheckpointAccess;
    struct Fifo {
        std::optional<std::uint8_t> control;
        std::optional<std::uint64_t> complete_at;
        std::optional<bool> empty;
    };
    std::array<Fifo,2> fifos_{};
    std::uint32_t reset_cycles_{};
    std::optional<std::uint64_t> emac_reset_at_;
    std::optional<std::uint8_t> bd_mode_;
    std::array<std::uint16_t,512> descriptors_{};
    std::array<std::uint16_t,512> descriptor_known_{};
    ObservedWord interrupt_status_{0,0xFFFFu};
    std::array<std::optional<ObservedWord>,52> emac_configuration_{};
    std::uint32_t phy_cycles_{};
    std::optional<ObservedWord> phy_data_;
    std::optional<std::uint16_t> phy_command_;
    std::optional<std::uint64_t> phy_complete_at_,phy_reset_at_;
    ObservedWord phy_bmcr_{0,0xFFFFu};
    std::uint32_t loopback_cycles_{};
    std::optional<std::uint16_t> mac_enable_;
    std::array<std::uint8_t,4096> tx_data_{},tx_known_{};
    std::array<std::uint8_t,16384> rx_data_{},rx_known_{};
    std::optional<std::uint16_t> tx_write_,tx_read_,tx_used_,rx_read_,rx_write_,rx_used_;
    std::array<std::optional<std::uint8_t>,2> frame_count_{};
    std::uint8_t tx_descriptor_{},rx_descriptor_{};
    std::optional<std::uint64_t> tx_complete_at_;
    // RUNTIME_HELPERS_BEGIN
    void invalidate_packets() {
        mac_enable_.reset();tx_data_={};tx_known_={};rx_data_={};rx_known_={};
        tx_write_.reset();tx_read_.reset();tx_used_.reset();rx_read_.reset();rx_write_.reset();rx_used_.reset();
        frame_count_={};tx_descriptor_=rx_descriptor_=0;tx_complete_at_.reset();
    }
    std::uint16_t descriptor_value(unsigned index) const {
        if(descriptor_known_[index]!=0xFFFFu)throw DeviceRefusal("SMAP transfer consumes an unknown descriptor field");
        return descriptors_[index];
    }
    std::uint32_t configuration(std::uint32_t offset,std::uint32_t mask) const {
        const auto hi=read_configuration(offset),lo=read_configuration(offset+2u);
        if(((hi.unknown<<16u)|lo.unknown)&mask)throw DeviceRefusal("SMAP loopback consumes unestablished MAC configuration");
        return ((hi.value<<16u)|lo.value)&mask;
    }
    void finish_loopback(std::uint64_t now) {
        if(!tx_complete_at_ || now<*tx_complete_at_)return;
        const auto tx=unsigned(tx_descriptor_)*4u,rx=256u+unsigned(rx_descriptor_)*4u;
        const auto control=descriptor_value(tx),length=descriptor_value(tx+2u),pointer=descriptor_value(tx+3u);
        if((control&~0x8300u) || !(control&0x8000u) || !(control&0x200u))throw DeviceRefusal("SMAP loopback supports ready descriptors with generated FCS and optional padding only");
        const auto next=(unsigned(tx_descriptor_)+1u)%64u;
        if(descriptor_value(next*4u)&0x8000u)throw DeviceRefusal("SMAP multi-packet descriptor chaining is not implemented");
        if(!(descriptor_value(rx)&0x8000u))throw DeviceRefusal("SMAP loopback has no empty RX descriptor; descriptor exhaustion is not modeled");
        const auto allocated=(unsigned(length)+3u)&~3u;
        if(!length || length>4096u || pointer!=0x1000u+*tx_read_ || allocated>*tx_used_)
            throw DeviceRefusal("SMAP loopback descriptor does not identify the queued TX FIFO bytes");
        const auto padded=(control&0x100u) ? std::max<unsigned>(length,60u) : unsigned(length);
        const bool strip=configuration(0x10,0x40000000u)!=0;
        const auto received=padded+(strip ? 0u : 4u),storage=(received+3u)&~3u;
        if(storage>16384u-*rx_used_ || *frame_count_[1]==255u)throw DeviceRefusal("SMAP loopback RX FIFO overflow is not modeled");
        if(padded<60u && !configuration(0x10,0x20000000u))throw DeviceRefusal("SMAP runt-frame filtering is not implemented");
        std::vector<std::uint8_t> data(padded+4u),known(padded+4u,0xFFu);
        std::uint32_t crc=~0u;bool crc_known=true;
        for(unsigned n=0;n<padded;++n) {
            if(n<length){const auto at=(*tx_read_+n)&4095u;data[n]=tx_data_[at];known[n]=tx_known_[at];}
            crc_known&=known[n]==0xFFu;crc^=data[n];
            for(unsigned b=0;b<8;++b)crc=(crc>>1u)^((crc&1u) ? 0xEDB88320u : 0u);
        }
        crc=~crc;
        for(unsigned n=0;n<4;++n){data[padded+n]=static_cast<std::uint8_t>(crc>>(8u*n));known[padded+n]=crc_known ? 0xFFu : 0u;}
        for(unsigned n=0;n<storage;++n){const auto at=(*rx_write_+n)&16383u;rx_data_[at]=n<received ? data[n] : 0;rx_known_[at]=n<received ? known[n] : 0;}
        descriptors_[rx]=static_cast<std::uint16_t>((descriptors_[rx]&~0x83FFu)|(padded<60u ? 0x40u : 0u));descriptor_known_[rx]=0xFFFFu;
        descriptors_[rx+2u]=static_cast<std::uint16_t>(received);descriptor_known_[rx+2u]=0xFFFFu;
        descriptors_[rx+3u]=static_cast<std::uint16_t>(0x4000u+*rx_write_);descriptor_known_[rx+3u]=0xFFFFu;
        descriptors_[tx]&=static_cast<std::uint16_t>(~0x83FFu);descriptor_known_[tx]=0xFFFFu;
        *tx_read_=static_cast<std::uint16_t>((*tx_read_+allocated)&4095u);*tx_used_-=static_cast<std::uint16_t>(allocated);
        *rx_write_=static_cast<std::uint16_t>((*rx_write_+storage)&16383u);*rx_used_+=static_cast<std::uint16_t>(storage);
        --*frame_count_[0];++*frame_count_[1];tx_descriptor_=static_cast<std::uint8_t>(next);rx_descriptor_=static_cast<std::uint8_t>((rx_descriptor_+1u)%64u);
        fifos_[0].empty=!*tx_used_;fifos_[1].empty=false;tx_complete_at_.reset();
        interrupt_status_.value|=0x34u;interrupt_status_.unknown&=~0x34u;
    }
    void advance_phy(std::uint64_t now) {
        const auto settle_reset=[&](std::uint64_t at){if(phy_reset_at_ && at>=*phy_reset_at_){phy_bmcr_.value&=~0x8000u;phy_reset_at_.reset();}};
        if(phy_complete_at_ && now>=*phy_complete_at_) {
            const auto at=*phy_complete_at_;
            settle_reset(at);
            if((*phy_command_&0x3000u)==0x2000u) {
                if(phy_data_->value==0x8000u){phy_bmcr_={0x8000u,0x7FFFu};phy_reset_at_=at+phy_cycles_;}
                else {phy_bmcr_={phy_data_->value,0x7Fu};phy_reset_at_.reset();}
            } else phy_data_=phy_bmcr_;
            *phy_command_|=0x8000u;phy_complete_at_.reset();
        }
        settle_reset(now);
    }
    std::uint16_t descriptor_bus(std::uint16_t value) const {
        if(!bd_mode_)throw DeviceRefusal("SMAP descriptor access requires an established byte-swap mode");
        return *bd_mode_ ? static_cast<std::uint16_t>((value<<8u)|(value>>8u)) : value;
    }
    Fifo& advance(bool receive,std::uint64_t now) {
        auto& fifo=fifos_[receive ? 1u : 0u];
        if(fifo.complete_at && now>=*fifo.complete_at) {
            fifo.control=0;fifo.complete_at.reset();fifo.empty=true;
            if(receive){rx_data_={};rx_known_={};rx_read_=rx_write_=rx_used_=0;frame_count_[1]=0;rx_descriptor_=0;}
            else {tx_data_={};tx_known_={};tx_read_=tx_write_=tx_used_=0;frame_count_[0]=0;tx_descriptor_=0;}
        }
        return fifo;
    }
public:
    void reset() {power_reset();reset_cycles_=0;phy_cycles_=0;loopback_cycles_=0;}
    void power_reset() {fifos_={};emac_reset_at_.reset();bd_mode_.reset();descriptors_={};descriptor_known_={};interrupt_status_={0,0xFFFFu};emac_configuration_={};phy_data_.reset();phy_command_.reset();phy_complete_at_.reset();phy_reset_at_.reset();phy_bmcr_={0,0xFFFFu};invalidate_packets();}
    void diagnostic_loopback_cycles(std::uint32_t cycles) {
        if(!cycles || tx_complete_at_ || tx_write_)throw DeviceRefusal("SMAP loopback timing requires positive IOP cycles before FIFO use");
        loopback_cycles_=cycles;
    }
    void tick(std::uint64_t now) {advance(false,now);advance(true,now);advance_phy(now);finish_loopback(now);}
    void diagnostic_phy_cycles(std::uint32_t cycles) {
        if(!cycles || phy_command_)throw DeviceRefusal("SMAP PHY timing requires positive IOP cycles before management use");
        phy_cycles_=cycles;
    }
    void diagnostic_reset_cycles(std::uint32_t cycles) {
        if(!cycles || fifos_[0].control || fifos_[1].control || emac_reset_at_)
            throw DeviceRefusal("SMAP reset timing requires positive IOP cycles before FIFO use");
        reset_cycles_=cycles;
    }
    void write(bool receive,ObservedWord word,std::uint64_t now) {
        if(tx_complete_at_)throw DeviceRefusal("SMAP FIFO reset during transmit is not modeled");
        if(word.unknown || word.value!=1u)
            throw DeviceRefusal("SMAP FIFO supports only a known RESET request; DMA and data transfers are not modeled");
        if(!reset_cycles_)
            throw DeviceRefusal("SMAP FIFO reset completion needs explicit --assume-smap-reset-cycles=N");
        if(now>std::numeric_limits<std::uint64_t>::max()-reset_cycles_)
            throw DeviceRefusal("SMAP FIFO reset deadline overflows the IOP clock");
        auto& fifo=fifos_[receive ? 1u : 0u];
        fifo.control=1;fifo.empty.reset();fifo.complete_at=now+reset_cycles_;
    }
    ObservedWord read(bool receive,std::uint64_t now) {
        const auto& fifo=advance(receive,now);
        if(!fifo.control)throw DeviceRefusal("SMAP FIFO control before a known write has no established value");
        return {*fifo.control,0xFCu}; // RESET/DMAEN known; unused byte bits remain unknown.
    }
    std::optional<bool> empty(bool receive,std::uint64_t now) {return advance(receive,now).empty;}
    // EMAC3 MODE0 is high-halfword first on the IOP bus (ps2sdk smapregs.h).
    // Only reset completion is established here; other MODE0 bits stay unknown.
    void write_mode0(bool low,ObservedWord word,std::uint64_t now) {
        tick(now);
        if(tx_complete_at_)throw DeviceRefusal("SMAP MAC reconfiguration during transmit is not modeled");
        if(!low && !word.unknown && !(word.value&~0x1800u)) {
            if(!emac_reset_at_ || now<*emac_reset_at_ || tx_complete_at_)throw DeviceRefusal("SMAP MAC enable requires a completed reset and no pending transmit");
            mac_enable_=static_cast<std::uint16_t>(word.value);return;
        }
        if(word.unknown || word.value!=(low ? 0u : 0x2000u))
            throw DeviceRefusal("SMAP EMAC MODE0 supports only a known software reset request and zero low halfword");
        if(low)return;
        if(!reset_cycles_)throw DeviceRefusal("SMAP EMAC reset completion needs explicit --assume-smap-reset-cycles=N");
        if(now>std::numeric_limits<std::uint64_t>::max()-reset_cycles_)
            throw DeviceRefusal("SMAP EMAC reset deadline overflows the IOP clock");
        emac_reset_at_=now+reset_cycles_;
        emac_configuration_={};mac_enable_.reset();
    }
    ObservedWord read_mode0(bool low,std::uint64_t now) const {
        if(!emac_reset_at_)throw DeviceRefusal("SMAP EMAC MODE0 before a known reset has no established value");
        if(low)return {0,0xFFFFu};
        return {(now<*emac_reset_at_ ? 0x2000u : 0u)|mac_enable_.value_or(0),mac_enable_ ? 0xC7FFu : 0xDFFFu};
    }
    void write_bd_mode(ObservedWord word) {
        if(word.unknown || (word.value&~1u))throw DeviceRefusal("SMAP descriptor mode requires a known swap bit only");
        bd_mode_=static_cast<std::uint8_t>(word.value);
    }
    ObservedWord read_bd_mode() const {
        if(!bd_mode_)throw DeviceRefusal("SMAP descriptor mode before a known write has no established value");
        return {*bd_mode_,0xFEu};
    }
    // PS2SDK smapregs.h: 64 eight-byte TX and RX descriptors, swapped per halfword.
    void write_descriptor(std::uint32_t offset,ObservedWord word) {
        if(tx_complete_at_)throw DeviceRefusal("SMAP descriptor mutation during transmit is not modeled");
        if(offset>=1024u || (offset&1u))throw DeviceRefusal("SMAP descriptor requires an aligned halfword");
        descriptors_[offset/2u]=descriptor_bus(static_cast<std::uint16_t>(word.value&~word.unknown));
        descriptor_known_[offset/2u]=descriptor_bus(static_cast<std::uint16_t>(~word.unknown));
    }
    ObservedWord read_descriptor(std::uint32_t offset) const {
        if(offset>=1024u || (offset&1u))throw DeviceRefusal("SMAP descriptor requires an aligned halfword");
        return {descriptor_bus(descriptors_[offset/2u]),static_cast<std::uint16_t>(~descriptor_bus(descriptor_known_[offset/2u]))};
    }
    void clear_interrupts(ObservedWord word) {
        if((word.unknown&0xFFFFu) || (word.value&~0x7Cu))throw DeviceRefusal("SMAP interrupt clear requires known interrupt bits only");
        interrupt_status_.value&=~word.value;interrupt_status_.unknown&=~word.value;
    }
    ObservedWord interrupt_status() const {return interrupt_status_;}
    std::uint16_t tx_write_pointer(std::uint64_t now) {tick(now);if(!tx_write_ || fifos_[0].control!=0u)throw DeviceRefusal("SMAP TX pointer requires completed FIFO reset");return *tx_write_;}
    void write_tx_data(ObservedWord word,std::uint64_t now) {
        tick(now);
        if(!tx_write_ || !tx_used_ || fifos_[0].control!=0u || tx_complete_at_)throw DeviceRefusal("SMAP TX data requires reset FIFO and no pending transmit");
        if(*tx_used_>4092u)throw DeviceRefusal("SMAP TX FIFO overflow");
        for(unsigned n=0;n<4;++n){const auto at=(*tx_write_+n)&4095u;tx_data_[at]=static_cast<std::uint8_t>(word.value>>(8u*n));tx_known_[at]=static_cast<std::uint8_t>(~(word.unknown>>(8u*n)));}
        *tx_write_=static_cast<std::uint16_t>((*tx_write_+4u)&4095u);*tx_used_+=4u;fifos_[0].empty=false;
    }
    void increment_tx_frame(ObservedWord word,std::uint64_t now) {
        tick(now);if(word.unknown || word.value || fifos_[0].control!=0u || !frame_count_[0] || *frame_count_[0]==255u)throw DeviceRefusal("SMAP TX frame increment requires known zero and a reset FIFO");
        ++*frame_count_[0];
    }
    void write_tx_mode0(bool low,ObservedWord word,std::uint64_t now) {
        tick(now);
        if(word.unknown || word.value!=(low ? 0u : 0x8000u))throw DeviceRefusal("SMAP TxMODE0 supports packet-zero requests and a zero low halfword only");
        if(low)return;
        if(!loopback_cycles_)throw DeviceRefusal("SMAP packet loopback requires explicit --assume-smap-loopback-cycles=N");
        if(now>std::numeric_limits<std::uint64_t>::max()-loopback_cycles_)throw DeviceRefusal("SMAP loopback deadline overflows the IOP clock");
        if(tx_complete_at_ || mac_enable_!=0x1800u || phy_bmcr_.unknown&0xFF80u || phy_bmcr_.value!=0x6100u || phy_reset_at_ || phy_complete_at_)
            throw DeviceRefusal("SMAP transmit requires enabled MAC and established 100M full-duplex PHY loopback");
        if(fifos_[0].control!=0u || fifos_[1].control!=0u || !tx_read_ || !tx_used_ || !rx_write_ || !rx_used_ || !frame_count_[0] || !frame_count_[1] || !*frame_count_[0])throw DeviceRefusal("SMAP transmit requires reset FIFOs and a queued frame");
        if(configuration(0x04,0xC0C00000u)!=0x80400000u || !configuration(0x10,0x01000000u))throw DeviceRefusal("SMAP loopback supports 100M full duplex and promiscuous receive configuration only");
        tx_complete_at_=now+loopback_cycles_;
    }
    ObservedWord read_tx_mode0(bool low,std::uint64_t now) {tick(now);return low ? ObservedWord{0,0xFFFFu} : ObservedWord{tx_complete_at_ ? 0x8000u : 0u,0x7FFFu};}
    ObservedWord read_rx_data(std::uint64_t now) {
        tick(now);if(fifos_[1].control!=0u || !rx_read_ || !rx_used_ || *rx_used_<4u)throw DeviceRefusal("SMAP RX data requires queued FIFO bytes");
        ObservedWord word{};
        for(unsigned n=0;n<4;++n){const auto at=(*rx_read_+n)&16383u;word.value|=std::uint32_t{rx_data_[at]}<<(8u*n);word.unknown|=std::uint32_t{static_cast<std::uint8_t>(~rx_known_[at])}<<(8u*n);}
        *rx_read_=static_cast<std::uint16_t>((*rx_read_+4u)&16383u);*rx_used_-=4u;fifos_[1].empty=!*rx_used_;return word;
    }
    void write_phy(bool low,ObservedWord word,std::uint64_t now) {
        advance_phy(now);
        if(tx_complete_at_)throw DeviceRefusal("SMAP PHY reconfiguration during transmit is not modeled");
        if(phy_complete_at_)throw DeviceRefusal("SMAP PHY management write while a transaction is pending");
        if(!low){phy_data_=ObservedWord{word.value&0xFFFFu,word.unknown&0xFFFFu};return;}
        if(word.unknown || (word.value!=0x1020u && word.value!=0x2020u))
            throw DeviceRefusal("SMAP PHY management currently supports known BMCR transactions at PHY address one");
        if(!phy_cycles_)throw DeviceRefusal("SMAP PHY management requires explicit --assume-smap-phy-cycles=N");
        if(now>std::numeric_limits<std::uint64_t>::max()-2ull*phy_cycles_)throw DeviceRefusal("SMAP PHY management deadline overflows the IOP clock");
        if(word.value==0x2020u && (!phy_data_ || phy_data_->unknown || (phy_data_->value!=0x8000u && phy_data_->value!=0x6100u)))
            throw DeviceRefusal("SMAP PHY BMCR supports reset and 100M full-duplex loopback only");
        phy_command_=static_cast<std::uint16_t>(word.value);phy_complete_at_=now+phy_cycles_;
    }
    ObservedWord read_phy(bool low,std::uint64_t now) {
        advance_phy(now);
        if(low)return phy_command_ ? ObservedWord{*phy_command_,0} : ObservedWord{0,0xFFFFu};
        return phy_data_.value_or(ObservedWord{0,0xFFFFu});
    }
    static std::uint32_t configuration_mask(std::uint32_t offset) {
        switch(offset&~3u) {
        case 0x04:return 0xFBFDF000u; // MODE1
        case 0x0C:return 0xF8FF0000u; // TxMODE1
        case 0x10:return 0xFFF80000u; // RxMODE
        case 0x14:case 0x18:return 0x03FF03FBu; // interrupt status/enable
        case 0x1C:return 0xFFFFu;case 0x20:return ~0u; // station address
        case 0x2C:return 0xFFFFu;case 0x58:return 0x3Fu;
        case 0x60:return 0xF8000000u;case 0x64:return 0xFF80FF80u;
        default:throw DeviceRefusal("SMAP EMAC configuration register is not implemented");
        }
    }
    void write_configuration(std::uint32_t offset,ObservedWord word) {
        if(tx_complete_at_)throw DeviceRefusal("SMAP MAC reconfiguration during transmit is not modeled");
        if(offset>=104u || (offset&1u))throw DeviceRefusal("SMAP EMAC configuration requires an aligned halfword");
        const auto mask=(configuration_mask(offset) >> ((offset&2u) ? 0u : 16u))&0xFFFFu;
        if((offset&~3u)==0x14u) {
            if(word.unknown&mask)throw DeviceRefusal("SMAP EMAC interrupt clear consumes unknown bits");
            auto previous=emac_configuration_[offset/2u].value_or(ObservedWord{0,0xFFFFu});
            previous.value&=~(word.value&mask);previous.unknown&=~(word.value&mask);
            emac_configuration_[offset/2u]=previous;return;
        }
        emac_configuration_[offset/2u]=ObservedWord{word.value&mask,((word.unknown&mask)|(~mask&0xFFFFu))};
    }
    ObservedWord read_configuration(std::uint32_t offset) const {
        if(offset>=104u || (offset&1u))throw DeviceRefusal("SMAP EMAC configuration requires an aligned halfword");
        (void)configuration_mask(offset);
        return emac_configuration_[offset/2u].value_or(ObservedWord{0,0xFFFFu});
    }
    bool valid() const {
        if(tx_descriptor_>=64u || rx_descriptor_>=64u || (mac_enable_ && (*mac_enable_&~0x1800u)))return false;
        if(tx_write_ && (*tx_write_>=4096u || (*tx_write_&3u)))return false;
        if(tx_read_ && (*tx_read_>=4096u || (*tx_read_&3u)))return false;
        if(tx_used_ && (*tx_used_>4096u || (*tx_used_&3u)))return false;
        for(const auto p:{rx_read_,rx_write_})if(p && (*p>=16384u || (*p&3u)))return false;
        if(rx_used_ && (*rx_used_>16384u || (*rx_used_&3u)))return false;
        if(tx_write_.has_value()!=tx_read_.has_value() || tx_write_.has_value()!=tx_used_.has_value() || tx_write_.has_value()!=frame_count_[0].has_value())return false;
        if(rx_write_.has_value()!=rx_read_.has_value() || rx_write_.has_value()!=rx_used_.has_value() || rx_write_.has_value()!=frame_count_[1].has_value())return false;
        if(tx_complete_at_ && (!loopback_cycles_ || mac_enable_!=0x1800u || !tx_used_ || !rx_used_ || !frame_count_[0] || !frame_count_[1] || !*frame_count_[0]))return false;
        if((phy_bmcr_.value|phy_bmcr_.unknown)&~0xFFFFu)return false;
        if(phy_data_ && ((phy_data_->value|phy_data_->unknown)&~0xFFFFu))return false;
        if(phy_complete_at_ && (!phy_cycles_ || !phy_command_ || (*phy_command_&0x8000u)))return false;
        if(phy_complete_at_ && (*phy_command_&0x3000u)==0x2000u && !phy_data_)return false;
        if(phy_reset_at_ && (!phy_cycles_ || !(phy_bmcr_.value&0x8000u) || (phy_bmcr_.unknown&0x8000u)))return false;
        if(phy_command_ && ((*phy_command_&~0x8000u)!=0x1020u && (*phy_command_&~0x8000u)!=0x2020u))return false;
        if(phy_command_ && !(*phy_command_&0x8000u) && !phy_complete_at_)return false;
        if((interrupt_status_.value|interrupt_status_.unknown)&~0xFFFFu)return false;
        for(unsigned n=0;n<descriptors_.size();++n)if(descriptors_[n]&~descriptor_known_[n])return false;
        for(unsigned n=0;n<emac_configuration_.size();++n)if(emac_configuration_[n]) {
            const auto& word=*emac_configuration_[n];
            if((word.value|word.unknown)&~0xFFFFu)return false;
            try {(void)configuration_mask(n*2u);}catch(const DeviceRefusal&){return false;}
        }
        if(bd_mode_ && *bd_mode_>1u)return false;
        if(emac_reset_at_ && (!reset_cycles_ || !*emac_reset_at_))return false;
        for(const auto& fifo:fifos_) {
            if(fifo.control && *fifo.control>1u)return false;
            if(fifo.control && !reset_cycles_)return false;
            if(fifo.control==1u && !fifo.complete_at)return false;
            if(fifo.complete_at && (!reset_cycles_ || fifo.control!=1u || fifo.empty))return false;
            if(fifo.control==0u && !fifo.empty)return false;
            if(!fifo.control && (fifo.complete_at || fifo.empty))return false;
        }
        return true;
    }
    // RUNTIME_HELPERS_END
};
} // namespace ps2native
