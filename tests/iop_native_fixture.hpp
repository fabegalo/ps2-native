#pragma once
// Shared fixed-word corpus for synthetic executables, including standalone device regressions.
#include "ps2native/iop_ops.hpp"
#include "ps2native/iop_aot.hpp"
#include "ps2native/sha256.hpp"
#ifdef _MSC_VER
#pragma optimize("",off)
#endif
namespace ps2native::test_iop_fixture {
inline const auto entries=[] {
    std::vector<IopAotEntry> result;
#define IOP_FIXTURE(word,first,last) \
    for(std::uint32_t pc=first;pc<=last;pc+=4) \
        result.push_back({pc,word,[](Iop& iop){iop.execute_native<word>();}});
#include "iop_native_words.inc"
#undef IOP_FIXTURE
    return result;
}();
inline const std::string_view source_text=
#define IOP_FIXTURE(word,first,last) #word ":" #first ":" #last "\n"
#include "iop_native_words.inc"
#undef IOP_FIXTURE
;
inline const auto source=sha256_hex({reinterpret_cast<const std::uint8_t*>(source_text.data()),source_text.size()});
inline const auto identity=iop_aot_identity(source,entries);
inline const IopAotModule module{source,identity,entries};
inline const IopAotRegistration registration{module};
}
#ifdef _MSC_VER
#pragma optimize("",on)
#endif
