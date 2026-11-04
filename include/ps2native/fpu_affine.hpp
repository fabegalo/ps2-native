#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <vector>
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
    // A bounded finite domain may cross sign/exponent classes. In that case
    // exponent is zero and origin proves copy/negation identity only.
    std::shared_ptr<const std::vector<std::uint32_t>> candidates{};
    // A union of conservative raw intervals, each within one sign/exponent
    // class. This bounds storage when exact finite sets grow too large.
    std::shared_ptr<const std::vector<std::array<std::uint32_t,2>>> ranges{};
};
}
