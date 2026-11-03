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

std::string refusal(std::uint32_t physical) {
    // The BIOS memory setup also writes F400, F420, F450, F460, F490, F4A0 and
    // F4C0, writes F410 with bit 31 set and polls it, and reads F480 back. No
    // public document describes them (PCSX2 keeps F410 as an unknown register
    // reading 0).
    return "MCH (RDRAM controller) register " + hex(physical) +
           " is not modeled: its command/completion semantics are undocumented";
}
} // namespace

void MchRegisters::reset() {
    chain_.power_on();
    ricm_ = 0u;
    busy_until_ = 0u;
    write_data_.reset();
    read_data_.reset();
    setr_done_.reset();
}

std::uint64_t MchRegisters::read(std::uint32_t physical, std::size_t bytes, std::uint64_t now) {
    if (physical != kMchRicm && physical != kMchDrd) throw DeviceRefusal(refusal(physical));
    if (bytes != 4u) throw DeviceRefusal("MCH access of " + std::to_string(bytes) + " byte(s) is not modeled (32-bit only)");
    const bool busy = now < busy_until_;
    if (physical == kMchRicm) return ricm_ | (busy ? kMchRicmBusy : 0u);
    if (busy) throw DeviceRefusal("MCH_DRD read while a serial transaction is in progress");
    if (!read_data_) throw DeviceRefusal("MCH_DRD read without a completed SRD: its value is not verified");
    return *read_data_;
}

void MchRegisters::write(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now) {
    if (physical != kMchRicm && physical != kMchDrd) throw DeviceRefusal(refusal(physical));
    if (bytes != 4u) throw DeviceRefusal("MCH access of " + std::to_string(bytes) + " byte(s) is not modeled (32-bit only)");
    const auto word = static_cast<std::uint32_t>(value);
    if (now < busy_until_) throw DeviceRefusal("MCH write of " + hex(physical) + " while a serial transaction is in progress");
    if (physical == kMchDrd) {
        if ((word & 0xFFFF0000u) != 0u)
            throw DeviceRefusal("MCH_DRD write " + hex(word) + ": serial data is 16 bits (SD15..SD0)");
        write_data_ = static_cast<std::uint16_t>(word);
        read_data_.reset();
        return;
    }
    if ((word & kMchRicmBusy) == 0u)
        throw DeviceRefusal("MCH_RICM write " + hex(word) + " without bit 31: its effect is not verified");
    if ((word & ~(kMchRicmFields | kMchRicmBusy)) != 0u)
        throw DeviceRefusal("MCH_RICM write " + hex(word) + " sets bits outside the SRQ/SA fields");
    if ((word & kMchRicmSdevHigh) != 0u)
        throw DeviceRefusal("MCH_RICM write " + hex(word) + ": SDEV5 is beyond the 5-bit serial device id of the data sheet");
    RdramSerialRequest request;
    request.sop = static_cast<std::uint8_t>((word >> 6) & 0xFu);
    request.broadcast = (word & (1u << 5)) != 0u;
    request.sdev = static_cast<std::uint8_t>(word & 0x1Fu);
    request.sa = static_cast<std::uint16_t>((word >> 16) & 0xFFFu);
    if (request.sop == kRdramSwr) {
        if (!write_data_) throw DeviceRefusal("MCH SWR without serial data written to MCH_DRD");
        request.sd = *write_data_;
    }
    if (request.sop == kRdramClrr && setr_done_ &&
        now < *setr_done_ + mch_busy_cycles(kRdramSckSetrToClrr))
        throw DeviceRefusal("MCH CLRR less than 16 SCK cycles after SETR (Algorithm InitDev step 3)");
    const auto data = chain_.transact(request); // refuses before any change
    const auto sck = RdramChain::sck_cycles(request.sop);
    ricm_ = word & kMchRicmFields;
    busy_until_ = now + mch_busy_cycles(sck);
    read_data_ = data;
    if (request.sop == kRdramSetr) setr_done_ = busy_until_;
}
} // namespace ps2native
