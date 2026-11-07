#include "direct_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/aot_runtime.hpp"
#include "ps2native/aot.hpp"
#include <iostream>
using namespace ps2native;
using boot_checks::require;

namespace {
std::uint32_t expected;
void native_cache(ExecutionContext& ctx) {
 if(!aot_enter_checked(ctx,direct_test::text,expected,false))return;
 const auto frame=ee::begin(ctx.state,direct_test::text);
 execute_cache_instruction(ctx,expected);(void)ee::retire(ctx,frame);
}
struct Result {std::string state;BootReport report;};
Result run(bool native,unsigned op,bool functional,bool user,bool unknown_tag) {
 expected=0xBC000000u|(8u<<21u)|(op<<16u);
 const auto image=parse_ee_elf(direct_test::elf({{{direct_test::text,5,m20::le_words({expected,0u})}},0,{}, {}}));
 const auto hash=sha256_hex(image.bytes);
 const AotBlock block{direct_test::text,native_cache};
 const AotModule module{&block,1,nullptr,0,"elf",hash.c_str()};
 EEState state;Memory memory;prepare_direct_elf(image,native?&module:nullptr,state,memory);
 memory.diagnostic_data_cache();state.set_cp0(Cop0::Config,0x10443u);memory.set_cache_config(0x10443u);
 memory.store32_observed(0xA0004000u,{0x12340000u,0x000000FFu});
 memory.store16_observed(0x80004002u,{0xA040u,0x000Fu});
 if(!functional)memory.reset_for_boot(m20::build_firmware().rom); // Separate coherent-memory contract.
 if(!functional)memory.write32(direct_test::text,expected);
 state.set_reg32(8,op==0x18||op==0x1A||op==0x1C ? 0x80004000u:0u);
 state.set_cp0(Cop0::TagLo,0);state.taglo_unknown=unknown_tag?0x10u:0;
 state.set_cp0(Cop0::Status,user?0x10u:0u);
 BootOptions options;options.max_instructions=1;options.trace=TraceLevel::Full;options.max_trace_events=128;
 const auto report=run_ee_boot(state,memory,native?&module:nullptr,options);
 if(!user && functional && (op==0x14u || op==0x18u || op==0x1Cu)) {
  const auto published=memory.diagnostic_ram(0x4000u,4u);
  require(published.value==0xA0400000u && published.unknown==0x000F00FFu,
          "writeback publishes independently expected bytes and bit knowledge");
 }
 return {checkpoint_state_sha256(state,memory),report};
}
}
int main() try {
 for(unsigned op:{4u,7u,12u,16u,17u,18u,19u,20u,21u,22u,24u,26u,28u})
 for(bool functional:{false,true})for(bool user:{false,true})for(bool unknown:{false,true}) {
  const auto reference=run(false,op,functional,user,unknown),native=run(true,op,functional,user,unknown);
  require(reference.state==native.state,"native CACHE preserves complete guest state");
  require(reference.report.reason==native.report.reason && reference.report.retired==native.report.retired,
          "native CACHE preserves refusals, privilege and retirement");
  require(boot_checks::normalized(reference.report.tail)==boot_checks::normalized(native.report.tail) &&
          boot_checks::normalized(reference.report.trace)==boot_checks::normalized(native.report.trace),
          "native CACHE observes the same instruction attempts and fault boundary");
  const auto events=[](const BootReport& report){std::vector<std::string> result;for(const auto& line:report.trace)if(line.starts_with("  S "))result.push_back(line);return result;};
  require(events(reference.report)==events(native.report),"native CACHE preserves system events");
 }
 const auto image=parse_ee_elf(direct_test::elf({{{direct_test::text,5,m20::le_words({0xBD140000u,0u})}},0,{}, {}}));
 AotOptions options;options.interpreter_boundaries=true;
 const auto generated=generate_aot_cpp(image,options);
 require(generated.find("aot_enter_checked(ctx,")!=std::string::npos &&
         generated.find("execute_cache_instruction(ctx,")!=std::string::npos &&
         generated.find("system instruction Cache")==std::string::npos,
         "CACHE code generation uses shared semantics instead of interpreter boundaries");
 std::cout<<"104 CACHE differential pairs passed\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
