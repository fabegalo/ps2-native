#include "ps2native/iop_aot.hpp"
#include "ps2native/iop_ops.hpp"
#include "ps2native/sha256.hpp"

extern "C" const ps2native::IopAotModule* ps2n_iop_aot_v1() {
    using namespace ps2native;
    static const std::array words{ObservedWord{0x2402002Au,0u}};
    static const auto source=sha256_hex(encode_iop_aot_image(0x1000u,words));
    static const std::array entries{IopAotEntry{0x1000u,0x2402002Au,[](Iop& cpu){cpu.execute_native<0x2402002Au>();}}};
    static const auto identity=iop_aot_identity(source,entries);
    static const IopAotModule module{source,identity,entries};
    return &module;
}
