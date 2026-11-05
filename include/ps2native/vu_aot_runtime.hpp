#pragma once
#include "ps2native/device_timing.hpp"
#include <array>
#include <string>
#include <string_view>

namespace ps2native {
class VuControl;
class Gif;
class GsRegisters;
// Host artifacts only: never part of guest state or checkpoint payloads.
inline constexpr unsigned kVuAotAbi=1;
struct VuAotImage { std::array<ObservedWord,4096> words; };
[[nodiscard]] std::string vu1_aot_identity(const VuAotImage& image,bool diagnostic_power_two_carry=false);
struct VuAotModule {
    std::string_view identity;
    const VuAotImage* image;
    bool diagnostic_power_two_carry;
    bool (*step)(VuControl&,Gif*,GsRegisters*);
};
// Generated objects register immutable descriptors. No compiler or dynamic
// loader runs during gameplay; a missing translation is interpreted explicitly.
struct VuAotRegistration { explicit VuAotRegistration(const VuAotModule& module); };
struct VuAotStats {
    std::uint64_t native_instructions{},interpreted_instructions{},identity_checks{};
    std::uint64_t missing_program{},untranslated_pc{};
};
enum class VuAotFallback { None, Disabled, MissingProgram, UntranslatedPc };
} // namespace ps2native
