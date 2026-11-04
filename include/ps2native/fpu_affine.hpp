#pragma once
#include <cstdint>
namespace ps2native {
// Exact relation in units of 2^(exponent-150), independent of the unknown
// base integer's value: (+/- origin) + offset. Only established by operations
// whose alignment/normalization proves this relation for every represented bit.
struct FpuAffine {
    std::uint64_t origin{};
    std::int64_t offset{};
    unsigned exponent{};
    bool negated{};
    std::uint32_t value{},unknown{}; // guards against replacement of raw data
    std::uint32_t minimum{},maximum{}; // tighter numeric interval than a bit hull
};
}
