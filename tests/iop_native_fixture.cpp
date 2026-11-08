#include "ps2native/iop_ops.hpp"
#include "ps2native/iop_aot.hpp"
#include "ps2native/sha256.hpp"

using namespace ps2native;
namespace {
// Only synthetic test targets link this immutable corpus. Production must
// supply content-owned translations through the normal preparation backend.
const auto entries=[] {
    std::vector<IopAotEntry> result;
#define IOP_FIXTURE(word,first,last) \
    for(std::uint32_t pc=first;pc<=last;pc+=4) \
        result.push_back({pc,word,[](Iop& iop){iop.execute_native<word>();}});
#include "iop_native_words.inc"
#undef IOP_FIXTURE
    return result;
}();
const std::string_view source_text=
#define IOP_FIXTURE(word,first,last) #word ":" #first ":" #last "\n"
#include "iop_native_words.inc"
#undef IOP_FIXTURE
;
const auto source=sha256_hex({reinterpret_cast<const std::uint8_t*>(source_text.data()),source_text.size()});
const auto identity=iop_aot_identity(source,entries);
const IopAotModule module{source,identity,entries};
const IopAotRegistration registration{module};
}
