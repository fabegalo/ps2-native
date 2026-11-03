#include "ps2native/rdram.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace ps2native {
namespace {
std::string hex(std::uint32_t value, int digits) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return stream.str();
}

bool is_modeled_opcode(std::uint8_t sop) {
    return sop == kRdramSrd || sop == kRdramSwr || sop == kRdramSetr || sop == kRdramClrr || sop == kRdramSetf;
}
} // namespace

RdramChain::RdramChain(std::size_t devices) : devices_(devices) {
    if (devices == 0u || devices > 32u) throw DeviceRefusal("a Direct RDRAM channel holds 1 to 32 devices");
}

void RdramChain::power_on() {
    for (auto& device : devices_) device = {};
}

std::uint32_t RdramChain::sck_cycles(std::uint8_t sop) {
    if (sop == kRdramSrd || sop == kRdramSwr) return kRdramSckLong;
    if (sop == kRdramSetr || sop == kRdramClrr || sop == kRdramSetf) return kRdramSckShort;
    throw DeviceRefusal("RDRAM serial opcode " + hex(sop, 1) + " has no modeled transaction length");
}

std::optional<std::uint16_t> RdramChain::transact(const RdramSerialRequest& request) {
    const auto sop = request.sop;
    if (sop == kRdramTcen || sop == kRdramTcal)
        throw DeviceRefusal("RDRAM temperature calibration (TCEN/TCAL) is not modeled");
    if (!is_modeled_opcode(sop))
        throw DeviceRefusal("RDRAM serial opcode " + hex(sop, 1) +
                            " is reserved in the Direct RDRAM 64/72-Mbit data sheet; no evidence of its effect");
    if (request.sdev > kRdramInitSdevid) throw DeviceRefusal("RDRAM SDEV " + hex(request.sdev, 2) + " exceeds 5 bits");
    const bool access = sop == kRdramSrd || sop == kRdramSwr;
    if (access && request.sa != kRdramInit)
        throw DeviceRefusal("RDRAM control register " + hex(request.sa, 3) +
                            " is not modeled (only INIT 0x021 has a verified layout)");
    if (sop == kRdramSwr && (request.sd & ~kRdramInitFields) != 0u)
        throw DeviceRefusal("RDRAM INIT write " + hex(request.sd, 4) + " sets bits defined as 0 (5, 11-15)");

    // Which devices the request reaches (SIO repeated by every device before
    // them) and which of those it selects; validated before any state changes.
    std::vector<std::size_t> selected;
    for (std::size_t n = 0; n < devices_.size(); ++n) {
        const auto& device = devices_[n];
        const auto name = "RDRAM " + std::to_string(n);
        if (!device.reset_applied) {
            // Before the first SETR every register, SDEVID and SRP included,
            // is undefined. InitDev starts with a broadcast SETR that resets
            // all devices, so a broadcast SETR is taken to pass through them.
            if (sop != kRdramSetr)
                throw DeviceRefusal(name + " has not received SETR since power-on: its control registers are undefined");
            if (!request.broadcast)
                throw DeviceRefusal(name + ": a non-broadcast SETR before any SETR addresses an undefined SDEVID");
            selected.push_back(n);
            continue;
        }
        if (request.broadcast || (device.init & kRdramInitSdevid) == request.sdev) {
            if (access && device.reset_bit)
                throw DeviceRefusal(name + ": control register access between SETR and CLRR is not defined");
            selected.push_back(n);
        }
        if ((device.init & kRdramInitSrp) == 0u) break; // SIO1 = 0: the chain ends here
    }

    if (sop == kRdramSrd) {
        if (selected.empty())
            throw DeviceRefusal("RDRAM SRD to SDEV " + hex(request.sdev, 2) +
                                ": no device answered, and what the controller samples on an undriven SIO0 is not documented");
        if (selected.size() > 1u) throw DeviceRefusal("RDRAM SRD selects several devices driving SIO0 at once");
        const auto& device = devices_[selected.front()];
        if (device.init_defined != kRdramInitFields)
            throw DeviceRefusal("RDRAM " + std::to_string(selected.front()) +
                                " INIT read: PSX/NSR/PSR/LSR are undefined until INIT is written after SETR");
        return device.init;
    }
    for (const auto n : selected) {
        auto& device = devices_[n];
        switch (sop) {
            case kRdramSetr:
                device.reset_applied = true;
                device.reset_bit = true;
                device.init = kRdramInitReset;
                device.init_defined = kRdramInitReset;
                break;
            case kRdramClrr: device.reset_bit = false; break;
            case kRdramSetf: device.fast_clock = true; break;
            default:
                device.init = request.sd;
                device.init_defined = kRdramInitFields;
                break;
        }
    }
    return std::nullopt;
}
} // namespace ps2native
