#include "ps2native/frontend/native_module.hpp"
#include "sdk/iop_module_interface.hpp"
#include "ps2native/native_module_abi.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/boot.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace ps2native::frontend {
namespace {
std::uint32_t read_execution_alias(const nlohmann::json& info) {
    if(!info.contains("execution_alias"))return 0;
    const auto& field=info.at("execution_alias");
    if(!field.is_number_integer())throw std::runtime_error("Invalid native execution alias");
    const auto value=field.get<std::uint64_t>();
    if(value && value!=0x80000000u && value!=0xA0000000u)throw std::runtime_error("Invalid native execution alias");
    return static_cast<std::uint32_t>(value);
}
struct CodeImage {
#ifdef _WIN32
    HMODULE handle{};
    ~CodeImage(){if(handle)FreeLibrary(handle);}
    void* symbol(const char* name) const {return reinterpret_cast<void*>(GetProcAddress(handle,name));}
#else
    void* handle{};
    ~CodeImage(){if(handle)dlclose(handle);}
    void* symbol(const char* name) const {return dlsym(handle,name);}
#endif
};
nlohmann::json read_receipt(const std::filesystem::path& path) {
    const auto size=regular_file_size(path);
    if(!size || size>65536u)throw std::runtime_error("Native preparation receipt missing or too large");
    const auto bytes=read_file(path);return nlohmann::json::parse(bytes.begin(),bytes.end());
}
std::shared_ptr<CodeImage> load_code(const nlohmann::json& info,const std::filesystem::path& receipt) {
    const auto name=std::filesystem::u8path(info.at("binary").get<std::string>());
    if(name.empty() || name.is_absolute() || name.has_parent_path())throw std::runtime_error("Invalid native module filename");
    const auto binary=std::filesystem::absolute(receipt.parent_path()/name);
    const auto hash=info.at("binary_sha256").get<std::string>();
    if(sha256_file(binary)!=hash)throw std::runtime_error("Native module binary SHA-256 mismatch");
    auto image=std::make_shared<CodeImage>();
#ifdef _WIN32
    image->handle=LoadLibraryExW(native_io_path(binary).c_str(),nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!image->handle)throw std::runtime_error("Cannot load native module: Windows error "+std::to_string(GetLastError()));
#else
    image->handle=dlopen(native_io_path(binary).c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!image->handle)throw std::runtime_error(std::string("Cannot load native module: ")+dlerror());
#endif
    if(sha256_file(binary)!=hash)throw std::runtime_error("Native module changed while loading");
    return image;
}
}
struct NativeIopCode::Impl {
    std::shared_ptr<CodeImage> image;
    std::unique_ptr<IopAotRegistration> registration;
};
NativeIopCode::NativeIopCode(const std::filesystem::path& receipt,const std::string& hash):p_(std::make_unique<Impl>()) {
    const auto info=read_receipt(receipt);
    if(std::string_view(native_runtime_abi())!=PS2N_HOST_NATIVE_ABI ||
       info.at("schema")!="ps2n-native-iop-module-v1" || info.at("runtime_abi")!=native_runtime_abi() || info.at("image_sha256")!=hash)
        throw std::runtime_error("Native IOP module is stale or incompatible; prepare the content again");
    p_->image=load_code(info,receipt);
    using Entry=const NativeIopModuleDescriptor*(*)();
    const auto entry=reinterpret_cast<Entry>(p_->image->symbol("ps2n_native_iop_module_v1"));
    if(!entry)throw std::runtime_error("Native IOP module entry point is missing");
    const auto* descriptor=entry();
    if(!descriptor || descriptor->version!=1 || descriptor->size!=sizeof(NativeIopModuleDescriptor) ||
       !descriptor->runtime_abi || std::string_view(descriptor->runtime_abi)!=native_runtime_abi() ||
       !descriptor->module || descriptor->module->source_sha256!=hash || descriptor->module->entries.empty())
        throw std::runtime_error("Native IOP module ABI or image mismatch");
    p_->registration=std::make_unique<IopAotRegistration>(*descriptor->module,p_->image);
}
NativeIopCode::~NativeIopCode()=default;
struct LoadedNativeModule::Impl {
    std::shared_ptr<CodeImage> handle;
    std::vector<std::unique_ptr<NativeIopCode>> iopModules;
    const AotModule* module{};
    struct Overlay {
        ElfImage image;
        std::string hash;
        std::filesystem::path receipt;
        std::unique_ptr<LoadedNativeModule> loaded;
        bool active{};
        std::uint32_t alias{};
    };
    std::vector<Overlay> overlays;
};
LoadedNativeModule::LoadedNativeModule(const std::filesystem::path& receipt,const std::string& input_hash,
                                     const std::string& elf_hash,std::uint32_t execution_alias):p_(std::make_unique<Impl>()) {
    if(execution_alias && execution_alias!=0x80000000u && execution_alias!=0xA0000000u)
        throw std::runtime_error("Invalid native execution alias");
    if(std::string_view(native_runtime_abi())!=PS2N_HOST_NATIVE_ABI)
        throw std::runtime_error("Host executable and shared runtime ABI differ; rebuild the launcher and tools");
    const auto info=read_receipt(receipt);
    if(info.at("schema")!="ps2n-native-module-v1" || info.at("runtime_abi")!=native_runtime_abi() ||
       info.at("input_sha256")!=input_hash || info.at("elf_sha256")!=elf_hash ||
       read_execution_alias(info)!=execution_alias)
        throw std::runtime_error("Native module is stale or incompatible; prepare the content again");
    p_->handle=load_code(info,receipt);
    using Entry=const NativeModuleDescriptor* (*)();
    const auto entry=reinterpret_cast<Entry>(p_->handle->symbol("ps2n_native_module_v1"));
    if(!entry)throw std::runtime_error("Native module entry point is missing");
    const auto* descriptor=entry();
    if(!descriptor || descriptor->version!=1 || descriptor->size!=sizeof(NativeModuleDescriptor) ||
       !descriptor->runtime_abi || std::string_view(descriptor->runtime_abi)!=native_runtime_abi() || !descriptor->module)
        throw std::runtime_error("Native module ABI mismatch");
    const auto& module=*descriptor->module;
    if(!module.source_kind || std::string_view(module.source_kind)!="elf" || !module.source_sha256 ||
       elf_hash!=module.source_sha256 || !module.blocks || !module.block_count || module.block_count>16000000 ||
       module.indirect_entry_count>module.block_count || (module.indirect_entry_count && !module.indirect_entries))
        throw std::runtime_error("Native module provenance or dispatch table is invalid");
    for(std::size_t i=0;i<module.block_count;++i)
        if(!module.blocks[i].execute || (module.blocks[i].address&3u) ||
           (module.blocks[i].address&0xE0000000u)!=execution_alias ||
           (module.blocks[i].address&0x1FFFFFFFu)>=Memory::kMainRamBytes ||
           (i && module.blocks[i-1].address>=module.blocks[i].address))throw std::runtime_error("Invalid native block table");
    for(std::size_t i=0;i<module.indirect_entry_count;++i)
        if(!find_aot_block(module,module.indirect_entries[i]) ||
           (i && module.indirect_entries[i-1]>=module.indirect_entries[i]))throw std::runtime_error("Invalid native indirect entries");
    p_->module=&module;
    if(info.contains("overlays")) {
        const auto& overlays=info.at("overlays");
        if(!overlays.is_array() || overlays.size()>8)throw std::runtime_error("Invalid native overlay list");
        for(const auto& overlay:overlays) {
            const auto hash=overlay.at("image_sha256").get<std::string>();
            const auto alias=read_execution_alias(overlay);
            if(hash.size()!=64 || hash.find_first_not_of("0123456789abcdef")!=std::string::npos)
                throw std::runtime_error("Invalid native overlay hash");
            if(std::any_of(p_->overlays.begin(),p_->overlays.end(),[&](const auto& item){return item.hash==hash && item.alias==alias;}))
                throw std::runtime_error("Duplicate native overlay image");
            const auto root=receipt.parent_path().parent_path();
            const auto file=root/"regions"/(hash+".elf");
            if(regular_file_size(file)>0x10100u || sha256_file(file)!=hash)
                throw std::runtime_error("Native overlay image SHA-256 mismatch");
            const auto snapshot=read_file(file);
            if(snapshot.size()>0x10100u || sha256_hex(snapshot)!=hash)
                throw std::runtime_error("Native overlay image SHA-256 mismatch");
            auto image=ee_execution_alias(parse_ee_elf(snapshot),alias);
            const auto identity=hash+native_runtime_abi()+(alias?":"+std::to_string(alias):"");
            const auto key=sha256_hex({reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()});
            p_->overlays.push_back({std::move(image),hash,root/key/"native-prepare.json",{},false,alias});
        }
    }
    if(info.contains("iop_modules")) {
        const auto& modules=info.at("iop_modules");
        if(!modules.is_array() || modules.size()>64u)throw std::runtime_error("Invalid native IOP module list");
        std::vector<std::string> seen;
        const auto root=receipt.parent_path().parent_path();
        for(const auto& item:modules) {
            const auto hash=item.at("image_sha256").get<std::string>();
            if(hash.size()!=64u || hash.find_first_not_of("0123456789abcdef")!=std::string::npos ||
                std::find(seen.begin(),seen.end(),hash)!=seen.end())throw std::runtime_error("Invalid or duplicate native IOP image");
            const auto capture=root/"iop-regions"/(hash+".iop");
            if(regular_file_size(capture)>2u*1024u*1024u+16u || sha256_file(capture)!=hash)
                throw std::runtime_error("Native IOP image SHA-256 mismatch");
            p_->iopModules.push_back(std::make_unique<NativeIopCode>(root/native_iop_cache_key(hash,native_runtime_abi())/"native-iop-prepare.json",hash));
            seen.push_back(hash);
        }
    }
}
LoadedNativeModule::~LoadedNativeModule()=default;
const AotModule& LoadedNativeModule::module() const noexcept {return *p_->module;}
bool LoadedNativeModule::activate_overlays(const Memory& memory,std::uint32_t pc,BootOptions& options) {
    for(auto& overlay:p_->overlays) {
        if(overlay.active)continue;
        const auto& segments=overlay.image.segments;
        if(std::none_of(segments.begin(),segments.end(),[&](const auto& s){
            return pc>=s.vaddr && std::uint64_t{pc}-s.vaddr+4u<=s.file_size;
        }))continue;
        bool matches=true;
        for(const auto& s:segments) {
            if(!(s.flags&1u) || s.file_size!=s.memory_size || (s.vaddr&3u) || (s.file_size&3u))
                throw std::runtime_error("Invalid native overlay regions");
            for(unsigned i=0;i<s.file_size;i+=4) {
                std::uint32_t word=0;
                for(unsigned b=0;b<4;++b)word|=std::uint32_t{overlay.image.bytes[s.file_offset+i+b]}<<(b*8u);
                if(memory.fetch32(s.vaddr+i)!=word){matches=false;break;}
            }
            if(!matches)break;
        }
        if(!matches)continue;
        if(!overlay.loaded)overlay.loaded=std::make_unique<LoadedNativeModule>(overlay.receipt,overlay.hash,overlay.hash,overlay.alias);
        if(!overlay.loaded->p_->overlays.empty())throw std::runtime_error("Nested native overlay lists are invalid");
        if(!find_aot_block(overlay.loaded->module(),pc))continue;
        auto verified=VerifiedAotOverlay::verify(overlay.image,overlay.loaded->module(),memory);
        for(auto& old:p_->overlays)if(old.active) {
            const bool overlaps=std::any_of(old.image.segments.begin(),old.image.segments.end(),[&](const auto& a){
                return std::any_of(segments.begin(),segments.end(),[&](const auto& b){
                    return std::uint64_t{a.vaddr}<std::uint64_t{b.vaddr}+b.memory_size &&
                           std::uint64_t{b.vaddr}<std::uint64_t{a.vaddr}+a.memory_size;
                });
            });
            if(overlaps) {
                std::erase_if(options.aot_overlays,[&](const auto& item){return &item.module_for(memory)==&old.loaded->module();});
                old.active=false;
            }
        }
        options.aot_overlays.push_back(std::move(verified));overlay.active=true;return true;
    }
    return false;
}
}
