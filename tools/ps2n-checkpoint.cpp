#include "ps2native/checkpoint.hpp"
#include "ps2native/io.hpp"
#include <iostream>
template<class C> int checkpoint_main(int argc,C** argv)try {
    if(argc!=3 || ps2native::path_utf8(std::filesystem::path(argv[1]))!="inspect") {
        std::cout<<"Usage: ps2n-checkpoint inspect <complete-checkpoint>\n";return 2;
    }
    const auto info=ps2native::inspect_checkpoint(std::filesystem::path(argv[2]));
    std::cout<<"Complete checkpoint: pc=0x"<<std::hex<<info.pc<<std::dec<<" retired="<<info.retired<<" cycles="<<info.cycles
        <<"\npayload_bytes="<<info.payload_bytes<<"\npayload_sha256="<<info.payload_sha256<<"\nscenario="<<info.scenario_sha256
        <<"\nschema="<<info.schema_sha256<<"\nruntime="<<info.runtime_sha256<<"\nbindings="<<info.bindings_sha256
        <<"\nhost_metadata="<<info.host_metadata<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"ps2n-checkpoint: "<<e.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return checkpoint_main(argc,argv);}
#else
int main(int argc,char** argv){return checkpoint_main(argc,argv);}
#endif
