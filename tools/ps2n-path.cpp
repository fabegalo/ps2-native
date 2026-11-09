// Configure-time host path helper; uses the same I/O boundary as the runtime.
#include "ps2native/io.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include "dev_process.hpp"
#include <cctype>

namespace {
std::wstring decode_output(std::string_view bytes,unsigned codepage) {
    const auto flags=codepage==CP_UTF8?MB_ERR_INVALID_CHARS:0;
    const auto length=MultiByteToWideChar(codepage,flags,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    if(!length)return {};
    std::wstring result(length,L'\0');
    MultiByteToWideChar(codepage,flags,bytes.data(),static_cast<int>(bytes.size()),result.data(),length);return result;
}
void compiler_line(std::string_view bytes) {
    if(!bytes.empty() && bytes.back()=='\r')bytes.remove_suffix(1);
    std::wstring fallback;
    for(const auto codepage:{unsigned(CP_UTF8),GetOEMCP(),GetACP()}) {
        auto line=decode_output(bytes,codepage);if(line.empty() && !bytes.empty())continue;
        if(fallback.empty())fallback=line;
        std::size_t path=std::wstring::npos;
        for(std::size_t i=0;i+2<line.size();++i)
            if((line[i+1]==L':' && (line[i+2]==L'\\' || line[i+2]==L'/') &&
                ((line[i]>=L'A' && line[i]<=L'Z') || (line[i]>=L'a' && line[i]<=L'z'))) ||
               (line[i]==L'\\' && line[i+1]==L'\\')){path=i;break;}
        if(!path || path==std::wstring::npos)continue;
        const auto prefix=line.substr(0,path);
        if(std::count(prefix.begin(),prefix.end(),L':')!=2 || prefix.back()!=L' ' ||
           prefix.find_first_of(L"0123456789/\\()")!=std::wstring::npos)continue;
        const auto file=ps2native::native_io_path(std::filesystem::path(line.substr(path)));
        if(GetFileAttributesW(file.c_str())==INVALID_FILE_ATTRIBUTES)continue;
        std::cout<<"Note: including file: "<<ps2native::path_utf8(std::filesystem::path(line.substr(path)))<<'\n';return;
    }
    if(!bytes.empty() && fallback.empty())throw std::runtime_error("Cannot decode compiler diagnostics");
    std::cout<<ps2native::path_utf8(std::filesystem::path(fallback))<<'\n';
}
int compiler_main(int argc,wchar_t** argv) {
    std::vector<std::filesystem::path> arguments;for(int i=3;i<argc;++i)arguments.emplace_back(argv[i]);
    std::string pending;
    const auto output=[&](std::string_view bytes) {
        pending.append(bytes);std::size_t consumed=0,end;
        while((end=pending.find('\n',consumed))!=std::string::npos) {
            compiler_line(std::string_view(pending).substr(consumed,end-consumed));consumed=end+1;
        }
        pending.erase(0,consumed);
        if(pending.size()>1024u*1024u)throw std::runtime_error("Compiler diagnostic line exceeded 1 MiB");
        std::cout.flush();if(!std::cout)throw std::runtime_error("Cannot forward compiler diagnostics");
    };
    const auto result=ps2native::dev::process(argv[2],arguments,{},600,64u*1024u*1024u,{},output);
    if(!pending.empty())compiler_line(pending);
    return result.exit_code;
}
}
#endif

template<class Char> int path_main(int argc, Char** argv) {
    try {
#ifdef _WIN32
        if(argc>=3 && std::filesystem::path(argv[1])==L"msvc-v1")return compiler_main(argc,argv);
#endif
        if (argc != 4) throw std::runtime_error("Usage: ps2n-path alias|equivalent <path> <target>");
        const auto command = ps2native::path_utf8(std::filesystem::path(argv[1]));
        if (command == "alias") ps2native::ensure_directory_alias(argv[2], argv[3]);
        else if (command == "equivalent") {
            if (!ps2native::files_equivalent(argv[2], argv[3])) throw std::runtime_error("Paths refer to different files");
        } else throw std::runtime_error("Unknown path operation");
        return 0;
    } catch (const std::exception& error) { std::cerr << "ps2n-path: " << error.what() << '\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return path_main(argc, argv); }
#else
int main(int argc, char** argv) { return path_main(argc, argv); }
#endif
