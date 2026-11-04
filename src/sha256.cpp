#include "ps2native/sha256.hpp"

#include <array>
#include <cstddef>
#include "ps2native/io.hpp"
#include <algorithm>
#include <stdexcept>

namespace ps2native {
namespace {
constexpr std::array<std::uint32_t, 64> kRound{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

void compress(std::array<std::uint32_t, 8>& h, const std::uint8_t* block) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i)
        w[i] = (static_cast<std::uint32_t>(block[4 * i]) << 24) | (static_cast<std::uint32_t>(block[4 * i + 1]) << 16) |
               (static_cast<std::uint32_t>(block[4 * i + 2]) << 8) | block[4 * i + 3];
    for (std::size_t i = 16; i < 64; ++i) {
        const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (std::size_t i = 0; i < 64; ++i) {
        const auto t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kRound[i] + w[i];
        const auto t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}
} // namespace

std::string sha256_hex(std::span<const std::uint8_t> data) {
    Sha256 hash; hash.update(data); return hash.hex_digest();
}
void Sha256::update(std::span<const std::uint8_t> data) {
    if (data.size() > UINT64_MAX / 8u - bytes_) throw std::runtime_error("SHA-256 input too large");
    bytes_ += data.size();
    while (!data.empty()) {
        if (!used_ && data.size() >= 64) { compress(state_,data.data()); data = data.subspan(64); continue; }
        const auto n = std::min(data.size(), pending_.size() - used_);
        std::copy_n(data.begin(), n, pending_.begin() + used_);
        used_ += n; data = data.subspan(n);
        if (used_ == 64) { compress(state_,pending_.data()); used_ = 0; }
    }
}
std::string Sha256::hex_digest() const {
    auto h = state_;
    std::array<std::uint8_t, 128> tail{};
    const auto rest = used_;
    for (std::size_t i = 0; i < rest; ++i) tail[i] = pending_[i];
    tail[rest] = 0x80u;
    const std::size_t blocks = rest + 9 <= 64 ? 1 : 2;
    const auto bits = bytes_ * 8u;
    for (std::size_t i = 0; i < 8; ++i)
        tail[blocks * 64 - 1 - i] = static_cast<std::uint8_t>(bits >> (8 * i));
    for (std::size_t i = 0; i < blocks; ++i) compress(h, tail.data() + 64 * i);
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const auto word : h)
        for (int shift = 28; shift >= 0; shift -= 4) out.push_back(kDigits[(word >> shift) & 15u]);
    return out;
}
std::string sha256_file(const std::filesystem::path& path) {
    InputFile file(path); Sha256 hash;
    std::vector<std::uint8_t> buffer(1024u * 1024u);
    for (std::uint64_t offset = 0; offset < file.size();) {
        const auto n = file.read_at(offset, buffer);
        if (!n) throw std::runtime_error("short read while hashing input");
        hash.update(std::span(buffer).first(n)); offset += n;
    }
    return hash.hex_digest();
}
} // namespace ps2native
