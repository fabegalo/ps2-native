#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <array>
#include <filesystem>

namespace ps2native {
// SHA-256 (FIPS 180-4) as 64 lowercase hex digits: identifies user-supplied
// firmware and AOT inputs without distributing or embedding them.
[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> data);
// Incremental SHA-256 for multi-GiB discs, with constant memory use.
class Sha256 {
public:
    void update(std::span<const std::uint8_t> data);
    [[nodiscard]] std::string hex_digest() const;
private:
    std::array<std::uint32_t, 8> state_{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                                         0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    std::array<std::uint8_t,64> pending_{};
    std::uint64_t bytes_{};
    std::size_t used_{};
};
[[nodiscard]] std::string sha256_file(const std::filesystem::path& path);
} // namespace ps2native
