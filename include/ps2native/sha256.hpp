#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace ps2native {
// SHA-256 (FIPS 180-4) as 64 lowercase hex digits: identifies user-supplied
// firmware and AOT inputs without distributing or embedding them.
[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> data);
} // namespace ps2native
