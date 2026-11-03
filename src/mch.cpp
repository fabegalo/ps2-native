#include "ps2native/mch.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace ps2native {
namespace {
std::string hex(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

// Control registers the BIOS memory setup uses (see mch.hpp).
bool is_control_register(std::uint32_t physical) {
    switch (physical) {
        case kMchConfig: case kMchControl: case 0x1000F420u: case 0x1000F450u: case 0x1000F460u:
        case 0x1000F480u: case 0x1000F490u: case 0x1000F4A0u: case 0x1000F4C0u:
            return true;
        default:
            return false;
    }
}

std::size_t control_index(std::uint32_t physical) { return (physical - kMchBase) / 0x10u; }
} // namespace

void MchRegisters::settle(std::uint64_t now) {
    if (!pending_ || now < busy_until_) return;
    chain_ = std::move(*pending_);
    pending_.reset();
    read_data_ = pending_read_;
    pending_read_.reset();
}

void MchRegisters::reset() {
    chain_.power_on();
    pending_.reset();
    pending_read_.reset();
    ricm_ = 0u;
    busy_until_ = 0u;
    write_data_.reset();
    read_data_.reset();
    setr_done_.reset();
    control_ = {};
    // F400 reset value 0 (hypothesis): the BIOS reads it once, before writing
    // it, only to set bit 18 if clear and clear it again after a delay, and
    // then rewrites the whole register; no other bit of that value is used.
    control_[control_index(kMchConfig)] = 0u;
    control_busy_until_ = 0u;
}

std::uint64_t MchRegisters::read(std::uint32_t physical, std::size_t bytes, std::uint64_t now) {
    const bool control = is_control_register(physical);
    if (!control && physical != kMchRicm && physical != kMchDrd)
        throw DeviceRefusal("MCH (RDRAM controller) register " + hex(physical) +
                            " is not modeled: the BIOS memory setup does not use it");
    // The BIOS also reads the low half of F480 with LHU.
    if (bytes != 4u && !(control && bytes == 2u))
        throw DeviceRefusal("MCH access of " + std::to_string(bytes) + " byte(s) is not modeled (32-bit only)");
    settle(now);
    if (control) {
        const auto& latch = control_[control_index(physical)];
        if (!latch) throw DeviceRefusal("MCH " + hex(physical) + " read before any write: its reset value is unknown");
        const bool busy = physical == kMchControl && now < control_busy_until_;
        const auto word = *latch | (busy ? kMchRicmBusy : 0u);
        return bytes == 2u ? word & 0xFFFFu : word;
    }
    const bool busy = now < busy_until_;
    if (physical == kMchRicm) return ricm_ | (busy ? kMchRicmBusy : 0u);
    if (busy) throw DeviceRefusal("MCH_DRD read while a serial transaction is in progress");
    if (!read_data_) throw DeviceRefusal("MCH_DRD read without a completed SRD: its value is not verified");
    return *read_data_;
}

void MchRegisters::write(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now) {
    const bool control = is_control_register(physical);
    if (!control && physical != kMchRicm && physical != kMchDrd)
        throw DeviceRefusal("MCH (RDRAM controller) register " + hex(physical) +
                            " is not modeled: the BIOS memory setup does not use it");
    if (bytes != 4u) throw DeviceRefusal("MCH access of " + std::to_string(bytes) + " byte(s) is not modeled (32-bit only)");
    settle(now);
    const auto word = static_cast<std::uint32_t>(value);
    if (now < busy_until_) throw DeviceRefusal("MCH write of " + hex(physical) + " while a serial transaction is in progress");
    if (now < control_busy_until_)
        throw DeviceRefusal("MCH write of " + hex(physical) + " while the F410 operation is in progress");
    if (control) {
        if (physical == kMchControl && (word & kMchRicmBusy) != 0u) {
            if (word != kMchRicmBusy)
                throw DeviceRefusal("MCH F410 write " + hex(word) +
                                    ": bit 31 with other bits set is not verified (the BIOS writes bit 31 alone)");
            control_busy_until_ = now + mch_busy_cycles(kMchControlBusySck);
        }
        control_[control_index(physical)] = word & ~(physical == kMchControl ? kMchRicmBusy : 0u);
        return;
    }
    if (physical == kMchDrd) {
        if ((word & 0xFFFF0000u) != 0u)
            throw DeviceRefusal("MCH_DRD write " + hex(word) + ": serial data is 16 bits (SD15..SD0)");
        write_data_ = static_cast<std::uint16_t>(word);
        read_data_.reset();
        return;
    }
    if ((word & kMchRicmBusy) == 0u)
        throw DeviceRefusal("MCH_RICM write " + hex(word) + " without bit 31: its effect is not verified");
    if ((word & ~(kMchRicmFields | kMchRicmBusy | kMchRicmIgnored)) != 0u)
        throw DeviceRefusal("MCH_RICM write " + hex(word) + " sets bits outside the SRQ/SA fields");
    if ((word & kMchRicmSdevHigh) != 0u)
        throw DeviceRefusal("MCH_RICM write " + hex(word) + ": SDEV5 is beyond the 5-bit serial device id of the data sheet");
    RdramSerialRequest request;
    request.sop = static_cast<std::uint8_t>((word >> 6) & 0xFu);
    request.broadcast = (word & (1u << 5)) != 0u;
    request.sdev = static_cast<std::uint8_t>(word & 0x1Fu);
    request.sa = static_cast<std::uint16_t>((word >> 16) & 0xFFFu);
    auto next = chain_;
    std::optional<std::uint16_t> data;
    std::uint32_t sck = kRdramSckShort;
    if (request.sop == kMchSioPinInit) {
        if (!request.broadcast || request.sdev != 0u || request.sa != 0u)
            throw DeviceRefusal("MCH_RICM write " + hex(word) +
                                ": the SIO pin initialization is only verified in broadcast with SDEV and SA 0");
        next.sio_pin_initialization();
    } else {
        if (request.sop == kRdramSwr) {
            if (!write_data_) throw DeviceRefusal("MCH SWR without serial data written to MCH_DRD");
            request.sd = *write_data_;
        }
        if (request.sop == kRdramClrr && setr_done_ &&
            now < *setr_done_ + mch_busy_cycles(kRdramSckSetrToClrr))
            throw DeviceRefusal("MCH CLRR less than 16 SCK cycles after SETR (Algorithm InitDev step 3)");
        data = next.transact(request); // refuses before any change
        sck = RdramChain::sck_cycles(request.sop);
    }
    ricm_ = word & kMchRicmFields;
    busy_until_ = now + mch_busy_cycles(sck);
    pending_ = std::move(next);
    pending_read_ = data;
    read_data_.reset();
    if (request.sop == kRdramSetr) setr_done_ = busy_until_;
}
} // namespace ps2native
