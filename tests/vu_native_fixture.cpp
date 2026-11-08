#include "ps2native/vu_micro_ops.hpp"
#include "ps2native/vu_aot_runtime.hpp"

using namespace ps2native;
namespace {
// Exact synthetic images, including unknown masks and numeric mode, use the
// same registration/invalidation checks as ordinary offline VU artifacts.
#define VU_IMAGE_BEGIN(index,unit,mode) \
    constexpr unsigned unit_##index=unit; \
    const VuAotImage image_##index=[] {VuAotImage image; \
        image.words.fill({0,~0u});
#define VU_WORD_RANGE(first,last,value,mask) \
    for(unsigned at=first;at<last;++at)image.words[at]={value,mask};
#define VU_IMAGE_END(index) \
    return image;}(); \
    bool step_##index(VuControl& vu,Gif* gif,GsRegisters* gs) { \
        switch(vu.diagnostic_micro_pc(unit_##index)) {
#define VU_ENTRY(index,pc,low,upper,unit) \
    case pc:vu.execute_micro_native<low,upper,unit>(gif,gs);return true;
#define VU_REGISTER(index,unit,mode) \
    default:return false;}} \
    const std::string identity_##index=vu_aot_identity(image_##index,unit,mode); \
    const VuAotModule module_##index{identity_##index,&image_##index,mode,step_##index,unit}; \
    const VuAotRegistration registration_##index{module_##index};
#include "vu_native_words.inc"
#undef VU_IMAGE_BEGIN
#undef VU_WORD_RANGE
#undef VU_IMAGE_END
#undef VU_ENTRY
#undef VU_REGISTER
}
