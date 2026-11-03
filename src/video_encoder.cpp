#include "ps2native/video_encoder.hpp"

#include <iomanip>
#include <sstream>

namespace ps2native {
namespace {
std::string hex(std::uint32_t value, int digits = 2) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return stream.str();
}

// 0x1A000000 block offsets.
constexpr std::uint32_t k1aCommand = 0x00u, k1aAddress = 0x02u, k1aConfig04 = 0x04u, k1aStatus = 0x06u,
                        k1aConfig08 = 0x08u, k1aEnable = 0x0Au, k1aData = 0x10u;
constexpr std::uint16_t k1aStart = 0x80u, k1aCountMask = 0x0Fu;
constexpr std::uint16_t kStatusDone = 1u << 1, kStatusError = 1u << 0;
// 0x1F801900 block offsets.
constexpr std::uint32_t kBData = 0x00u, kBAddress = 0x04u, kBAddressHigh = 0x05u, kBCommand = 0x06u,
                        kBConfig07 = 0x07u, kBControl = 0x08u, kBConfig0A = 0x0Au, kBConfig0B = 0x0Bu,
                        kBConfig0C = 0x0Cu, kBConfig0D = 0x0Du, kBReset = 0x18u;
constexpr std::uint8_t kBClear = 0x08u;
constexpr std::uint8_t kBSend = 0x01u, kBReceive = 0x03u, kBSendChecked = 0x11u;
constexpr std::size_t kBQueueBytes = 8u; // only up to 2 are ever queued by the firmware
} // namespace

void VideoEncoderBus::reset(const DiagnosticAssumptions* assumptions) {
    pointer_.reset();
    written_ = {};
    assumed_ = {};
    log_.clear();
    if (assumptions != nullptr)
        for (const auto& [index, value] : assumptions->video_encoder_registers) assumed_[index] = value;
}

void VideoEncoderBus::write(std::uint8_t address, std::span<const std::uint8_t> bytes) {
    if (address != kVideoEncoderWriteAddress)
        throw DeviceRefusal("serial bus write to address byte " + hex(address) +
                            ": no device other than the video encoder (0x42) is known on this bus");
    if (bytes.empty() || bytes.size() > 2u)
        throw DeviceRefusal("video encoder write of " + std::to_string(bytes.size()) +
                            " byte(s): only a register pointer and at most one value are modeled "
                            "(auto-increment is not established)");
    std::string text = "write 0x42 reg " + hex(bytes[0]);
    pointer_ = bytes[0];
    if (bytes.size() == 2u) {
        written_[bytes[0]] = bytes[1];
        text += " = " + hex(bytes[1]);
    }
    log_.push_back(text);
}

std::vector<std::uint8_t> VideoEncoderBus::read(std::uint8_t address, std::size_t count) {
    if (address != kVideoEncoderReadAddress)
        throw DeviceRefusal("serial bus read from address byte " + hex(address) +
                            ": no device other than the video encoder (0x43 read) is known on this bus");
    if (count != 1u)
        throw DeviceRefusal("video encoder read of " + std::to_string(count) + " byte(s): only single-byte "
                            "reads are modeled (auto-increment is not established)");
    if (!pointer_) throw DeviceRefusal("video encoder read before any register pointer was written");
    const auto index = *pointer_;
    if (!assumed_[index])
        throw DeviceRefusal("video encoder (serial address 0x42) register " + hex(index) +
                            " read: its value has not been measured on any console profile" +
                            (index == 0x7Eu ? " (chip identification: the kernel hangs on 0x1D, programs the "
                                              "encoder on 0x1C, skips it otherwise)"
                                            : std::string()) +
                            "; a diagnostic run may assume it with --assume-video-encoder=" + hex(index) +
                            ":VALUE");
    log_.push_back("read 0x43 reg " + hex(index) + " -> " + hex(*assumed_[index]) + " (diagnostic assumption)");
    return {*assumed_[index]};
}

void VideoEncoderMaster::reset() {
    latched_ = {};
    status_.reset();
    enabled_.reset();
    data_ = {};
    send_.clear();
    received_.clear();
}

std::string VideoEncoderMaster::name(std::uint32_t offset) const {
    const auto base = layout_ == Layout::Halfword1A ? kVideoEncoderMaster1A : kVideoEncoderMaster190x;
    return "video encoder bus master " + hex(base + offset, 8);
}

std::uint32_t VideoEncoderMaster::read(std::uint32_t offset, std::size_t bytes, VideoEncoderBus& bus) {
    (void)bus;
    const auto what = name(offset) + " read";
    if (layout_ == Layout::Halfword1A) {
        if (bytes != 2u) throw DeviceRefusal(what + ": only 16-bit accesses are modeled");
        if (offset == k1aStatus) {
            if (!status_) throw DeviceRefusal(what + " (status) before it was cleared: power-on value unknown");
            return *status_;
        }
        if (offset >= k1aData && offset % 2u == 0u) {
            const auto& byte = data_[(offset - k1aData) / 2u];
            if (!byte) throw DeviceRefusal(what + " (data): no byte written or received there");
            return *byte;
        }
        throw DeviceRefusal(what + ": reading this register back is not modeled");
    }
    if (bytes != 1u) throw DeviceRefusal(what + ": only 8-bit reads are modeled");
    if (offset == kBControl) {
        if (!status_) throw DeviceRefusal(what + " (status) before it was cleared: power-on value unknown");
        return *status_; // bit 0 busy: 0, transfers complete at once; bit 2 error: 0, the slave acknowledges
    }
    if (offset == kBData) {
        if (received_.empty()) throw DeviceRefusal(what + " (data port): no received byte is pending");
        const auto byte = received_.front();
        received_.erase(received_.begin());
        return byte;
    }
    throw DeviceRefusal(what + ": reading this register back is not modeled");
}

void VideoEncoderMaster::write(std::uint32_t offset, std::size_t bytes, std::uint32_t value, VideoEncoderBus& bus) {
    const auto what = name(offset) + " = " + hex(value, bytes == 1u ? 2 : 4);
    if (layout_ == Layout::Halfword1A) {
        if (bytes != 2u) throw DeviceRefusal(what + ": only 16-bit accesses are modeled");
        const auto v = static_cast<std::uint16_t>(value);
        switch (offset) {
            case k1aCommand:
                if (v > 0xFFu) throw DeviceRefusal(what + ": command bits above bit 7 are not established");
                start_1a(static_cast<std::uint8_t>(v), bus);
                return;
            case k1aAddress:
                if (v > 0xFFu) throw DeviceRefusal(what + ": address bits above bit 7 are not established");
                latched_[offset] = v;
                return;
            case k1aConfig04: case k1aConfig08: latched_[offset] = v; return;
            case k1aStatus:
                // Write 1 to clear (hypothesis): the firmware writes 3 before
                // each transfer and then waits for a set bit.
                if ((v & ~(kStatusDone | kStatusError)) != 0u)
                    throw DeviceRefusal(what + ": only the done/error bits are modeled");
                status_ = static_cast<std::uint16_t>(status_.value_or(0u) & ~v);
                return;
            case k1aEnable:
                if (v > 1u) throw DeviceRefusal(what + ": only bit 0 (enable) is modeled");
                enabled_ = v == 1u;
                if (!*enabled_) { // held: the transfer state is dropped (hypothesis)
                    status_.reset();
                    data_ = {};
                }
                return;
            default:
                if (offset >= k1aData && offset % 2u == 0u) {
                    if (v > 0xFFu) throw DeviceRefusal(what + ": data halfwords carry one byte");
                    data_[(offset - k1aData) / 2u] = static_cast<std::uint8_t>(v);
                    return;
                }
                throw DeviceRefusal(what + ": register not used by the firmware; its effect is unknown");
        }
    }
    const auto v = static_cast<std::uint8_t>(value);
    if (offset == kBReset) {
        // 8-bit 0 holds the master, 16-bit 1 releases it (firmware sequence).
        if (bytes > 2u || value > 1u) throw DeviceRefusal(what + ": only 0 and 1 are modeled");
        enabled_ = value == 1u;
        if (!*enabled_) {
            status_.reset();
            send_.clear();
            received_.clear();
        }
        return;
    }
    if (bytes != 1u) throw DeviceRefusal(what + ": only 8-bit accesses are modeled");
    switch (offset) {
        case kBData:
            if (send_.size() >= kBQueueBytes) throw DeviceRefusal(what + ": queue depth beyond 8 bytes is not established");
            send_.push_back(v);
            return;
        case kBAddress: case kBAddressHigh: case kBConfig07: case kBConfig0A: case kBConfig0B: case kBConfig0C:
        case kBConfig0D:
            latched_[offset] = v;
            return;
        case kBControl:
            if (v != kBClear) throw DeviceRefusal(what + ": only 0x08 (clear before a transfer) is modeled");
            send_.clear();
            received_.clear();
            status_ = 0u;
            return;
        case kBCommand: return start_190x(v, bus);
        default:
            throw DeviceRefusal(what + ": register not used by the firmware; its effect is unknown");
    }
}

void VideoEncoderMaster::start_1a(std::uint8_t command, VideoEncoderBus& bus) {
    const auto what = name(k1aCommand) + " = " + hex(command);
    if ((command & ~(k1aStart | k1aCountMask)) != 0u || (command & k1aStart) == 0u)
        throw DeviceRefusal(what + ": only start commands (bit 7, byte count in bits 0-3) are modeled");
    if (!enabled_ || !*enabled_) throw DeviceRefusal(what + ": the master is not enabled (0x1A00000A)");
    if (!latched_[k1aAddress]) throw DeviceRefusal(what + ": no address byte written");
    if (!status_) throw DeviceRefusal(what + ": status not cleared since the master was enabled");
    const auto address = static_cast<std::uint8_t>(*latched_[k1aAddress]);
    const auto count = static_cast<std::size_t>(command & k1aCountMask);
    if (count == 0u || count > data_.size()) throw DeviceRefusal(what + ": byte count outside 1-8");
    if ((address & 1u) == 0u) {
        std::vector<std::uint8_t> bytes;
        for (std::size_t i = 0; i < count; ++i) {
            if (!data_[i]) throw DeviceRefusal(what + ": data byte " + std::to_string(i) + " not written");
            bytes.push_back(*data_[i]);
        }
        bus.write(address, bytes);
    } else {
        const auto bytes = bus.read(address, count);
        for (std::size_t i = 0; i < count; ++i) data_[i] = bytes[i];
    }
    *status_ |= kStatusDone;
}

void VideoEncoderMaster::start_190x(std::uint8_t command, VideoEncoderBus& bus) {
    const auto what = name(kBCommand) + " = " + hex(command);
    if (!enabled_ || !*enabled_) throw DeviceRefusal(what + ": the master is held in reset (0x1F801918)");
    if (!status_) throw DeviceRefusal(what + ": not cleared (0x1F801908 = 8) since the master was released");
    if (!latched_[kBAddress]) throw DeviceRefusal(what + ": no address byte written");
    if (latched_[kBAddressHigh].value_or(1u) != 0u)
        throw DeviceRefusal(what + ": 0x1F801905 other than 0 is not modeled");
    const auto address = static_cast<std::uint8_t>(*latched_[kBAddress]);
    switch (command) {
        case kBSend: case kBSendChecked:
            bus.write(address, send_);
            send_.clear();
            return;
        case kBReceive:
            if (!send_.empty()) throw DeviceRefusal(what + ": receive with bytes still queued is not modeled");
            for (const auto byte : bus.read(static_cast<std::uint8_t>(address | 1u), 1u)) received_.push_back(byte);
            return;
        default:
            throw DeviceRefusal(what + ": only commands 0x01, 0x03 and 0x11 are modeled");
    }
}
} // namespace ps2native
