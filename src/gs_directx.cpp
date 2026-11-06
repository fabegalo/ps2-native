// Original BSD-3-Clause D3D12 execution backend for the shared GS device.
#include "ps2native/gs_gpu.hpp"
#include "ps2native/gs_vram.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "gs_flat_dxil.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ps2native {
namespace {
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
std::uint64_t elapsed(Clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
}
void checked(HRESULT hr,const char* operation) {
    if(SUCCEEDED(hr))return;
    std::ostringstream reason;reason<<"GS Direct3D 12 "<<operation<<" failed (HRESULT 0x"<<std::hex<<static_cast<unsigned long>(hr)<<')';
    throw std::runtime_error(reason.str());
}
struct Event {
    HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    Event(){if(!value)throw std::runtime_error("GS Direct3D 12 fence event creation failed");}
    ~Event(){CloseHandle(value);}
};
struct SystemModule {
    HMODULE handle{};
    explicit SystemModule(const wchar_t* name):handle(LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if(!handle)throw std::runtime_error("GS Direct3D 12 system runtime is unavailable");
    }
    ~SystemModule(){FreeLibrary(handle);}
    SystemModule(const SystemModule&)=delete;
    SystemModule& operator=(const SystemModule&)=delete;
};
class DirectXGs final : public GsGpu {
    // Declared first so the system modules outlive every COM resource.
    SystemModule d3d12_{L"d3d12.dll"},dxgi_{L"dxgi.dll"};
    decltype(&D3D12CreateDevice) create_device_{};
    decltype(&D3D12SerializeRootSignature) serialize_root_{};
    using CreateFactory=HRESULT(WINAPI*)(REFIID,void**);
    CreateFactory create_factory_{};
    static constexpr UINT64 kVramBytes=4u*1024u*1024u;
    static constexpr UINT64 kResultOffset=2u*kVramBytes;
    static constexpr UINT64 kBufferBytes=kResultOffset+256u;
    ComPtr<IDXGIFactory1> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12Device5> lifecycle_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> pipeline_;
    ComPtr<ID3D12Resource> device_memory_,upload_,readback_,query_readback_,primitive_upload_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12QueryHeap> queries_;
    Event event_;
    UINT64 fence_value_{},timestamp_frequency_{};
    std::exception_ptr failure_;
    bool in_flight_{};
    std::mutex mutex_;
    GsGpuStats stats_{};
    std::string name_;
    bool diagnostics_{},dred_configured_{},driver_known_{};
    UINT vendor_id_{},device_id_{};
    LARGE_INTEGER driver_version_{};

    ComPtr<ID3D12Resource> buffer(UINT64 bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,bool uav=false) {
        D3D12_HEAP_PROPERTIES properties{};properties.Type=heap;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;
        desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(uav)desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> result;
        checked(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)),"buffer allocation");
        stats_.storage_bytes+=device_->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
        return result;
    }
    void transition(D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=device_memory_.Get();barrier.Transition.StateBefore=before;
        barrier.Transition.StateAfter=after;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands_->ResourceBarrier(1,&barrier);
    }
    void append_dred(std::ostream& message) const {
        message<<"; vendor=0x"<<std::hex<<vendor_id_<<"; device=0x"<<device_id_;
        if(driver_known_)message<<"; driver=0x"<<static_cast<UINT64>(driver_version_.QuadPart);
        message<<"; DRED configured="<<(dred_configured_?"yes":"no");
        ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        const auto queried=device_.As(&dred);
        if(FAILED(queried)){message<<"; DRED interface HRESULT=0x"<<static_cast<unsigned long>(queried);return;}
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
        const auto breadcrumbs_status=dred->GetAutoBreadcrumbsOutput(&breadcrumbs);
        message<<"; DRED breadcrumbs HRESULT=0x"<<static_cast<unsigned long>(breadcrumbs_status);
        if(SUCCEEDED(breadcrumbs_status)) {
            const auto* node=breadcrumbs.pHeadAutoBreadcrumbNode;
            unsigned index=0;
            for(;node && index<4u;node=node->pNext,++index) {
                message<<"; breadcrumb["<<std::dec<<index<<"] count="<<node->BreadcrumbCount;
                if(!node->pLastBreadcrumbValue){message<<" last=unavailable";continue;}
                const auto last=*node->pLastBreadcrumbValue;
                message<<" last="<<last;
                if(last>node->BreadcrumbCount){message<<" (invalid counter)";continue;}
                // DRED retains a 65536-operation history ring. Check both
                // counter bounds and pointer availability before indexing.
                if(last && node->BreadcrumbCount-last<65536u && node->pCommandHistory)
                    message<<" last_completed_op="<<static_cast<unsigned>(node->pCommandHistory[(last-1u)%65536u]);
                if(last<node->BreadcrumbCount && node->BreadcrumbCount-last<=65536u && node->pCommandHistory)
                    message<<" next_op="<<static_cast<unsigned>(node->pCommandHistory[last%65536u]);
            }
            if(node)message<<"; additional breadcrumb nodes omitted";
        }
        D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
        const auto fault_status=dred->GetPageFaultAllocationOutput(&fault);
        message<<"; DRED page fault HRESULT=0x"<<std::hex<<static_cast<unsigned long>(fault_status);
        if(SUCCEEDED(fault_status))message<<" GPU_VA=0x"<<fault.PageFaultVA;
    }
    [[noreturn]] void fail_batch(const char* cause) {
        // Preserve the driver's status before cleanup can replace it. Adapter
        // identity is diagnostic evidence, never a per-driver skip rule.
        const auto removed=device_ ? device_->GetDeviceRemovedReason() : S_OK;
        std::ostringstream optional_details;
        if(diagnostics_) {
            // Diagnostics are bounded and best-effort. Read them before a
            // deliberate RemoveDevice changes the original fault evidence.
            try{append_dred(optional_details);}catch(...){}
        }
        const auto terminated=in_flight_;
        if(in_flight_){lifecycle_->RemoveDevice();in_flight_=false;}
        std::ostringstream message;
        message<<cause<<"; adapter=\""<<name_<<"\"; GetDeviceRemovedReason=0x"
            <<std::hex<<static_cast<unsigned long>(removed);
        if(diagnostics_)message<<optional_details.str();
        if(terminated)message<<"; incomplete device execution terminated before resource release";
        failure_=std::make_exception_ptr(std::runtime_error(message.str()));
        std::rethrow_exception(failure_);
    }
    void preflight() {
        // Verify real integer execution before advertising availability. This
        // isolated host scratch storage contains no guest GS state or cycles.
        std::vector<std::uint32_t> words(kVramBytes/4u),known(words.size());
        GsGpuCommand sprite{};
        sprite.right=8;sprite.bottom=8;sprite.bw=64;
        sprite.color=0x80402010u;sprite.fba_known=1;
        auto triangle=sprite;triangle.triangle=1;triangle.x1=128;triangle.y2=128;
        triangle.color=0x80804020u;
        const std::array primitives{sprite,triangle};
        GsGpuResult result;
        try {
            // The owner is still null. The common standalone overload proves
            // the full upload/materialization contract without binding a GS.
            result=GsGpu::execute(words,known,primitives);
        } catch(const std::exception& error) {
            throw std::runtime_error(std::string{"GS Direct3D 12 initialization preflight failed: "}+error.what());
        }
        if(result.revisions!=100u || result.pixel_writes!=100u)
            fail_batch("GS Direct3D 12 initialization preflight counters differ from the GS proof");
        std::vector<std::uint32_t> expected(words.size()),expected_known(words.size());
        for(unsigned y=0;y<8u;++y)for(unsigned x=0;x<8u;++x) {
            const auto at=GsVram::address(0,64,0,x,y)/4u;
            expected[at]=x+y<8u ? triangle.color : sprite.color;
            expected_known[at]=0xFFFFFFFFu;
        }
        if(words!=expected || known!=expected_known)
            fail_batch("GS Direct3D 12 initialization preflight pixels or known-bit masks differ from the GS proof");
        // Guest counters begin after the proof. Retain allocation/capability
        // diagnostics; residency and all synchronization policies are intact.
        const auto timestamps=stats_.gpu_timestamps;
        const auto storage=stats_.storage_bytes;
        stats_={};stats_.gpu_timestamps=timestamps;stats_.storage_bytes=storage;
    }
public:
    DirectXGs() {
        create_device_=reinterpret_cast<decltype(create_device_)>(GetProcAddress(d3d12_.handle,"D3D12CreateDevice"));
        serialize_root_=reinterpret_cast<decltype(serialize_root_)>(GetProcAddress(d3d12_.handle,"D3D12SerializeRootSignature"));
        create_factory_=reinterpret_cast<CreateFactory>(GetProcAddress(dxgi_.handle,"CreateDXGIFactory1"));
        if(!create_device_ || !serialize_root_ || !create_factory_)
            throw std::runtime_error("GS Direct3D 12 system entry points are unavailable");
        if(const auto* setting=std::getenv("PS2N_GS_GPU_DIAGNOSTICS"))diagnostics_=std::strcmp(setting,"1")==0;
        if(diagnostics_) {
            // DRED settings must precede device creation. Neither DRED nor
            // the debug layer is required for normal execution/availability.
            const auto get_debug=reinterpret_cast<decltype(&D3D12GetDebugInterface)>(
                GetProcAddress(d3d12_.handle,"D3D12GetDebugInterface"));
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
            if(get_debug && SUCCEEDED(get_debug(IID_PPV_ARGS(&settings)))) {
                settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred_configured_=true;
            }
        }
        checked(create_factory_(IID_PPV_ARGS(&factory_)),"DXGI discovery");
        for(UINT index=0;;++index) {
            ComPtr<IDXGIAdapter1> adapter;
            const auto hr=factory_->EnumAdapters1(index,&adapter);
            if(hr==DXGI_ERROR_NOT_FOUND)break;
            checked(hr,"adapter enumeration");
            DXGI_ADAPTER_DESC1 desc{};checked(adapter->GetDesc1(&desc),"adapter identity");
            if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
            ComPtr<ID3D12Device> candidate;
            if(FAILED(create_device_(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&candidate))))continue;
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 options{};
            if(FAILED(candidate->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,&options,sizeof(options))) || !options.Int64ShaderOps)continue;
            D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_0};
            if(FAILED(candidate->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&shader_model,sizeof(shader_model))) || shader_model.HighestShaderModel<D3D_SHADER_MODEL_6_0)continue;
            device_=std::move(candidate);
            if(diagnostics_) {
                vendor_id_=desc.VendorId;device_id_=desc.DeviceId;
                driver_known_=SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver_version_));
            }
            const auto bytes=WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,nullptr,0,nullptr,nullptr);
            if(bytes<=1)throw std::runtime_error("GS Direct3D 12 adapter identity is unavailable");
            std::string adapter_name(static_cast<std::size_t>(bytes),0);
            WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,adapter_name.data(),bytes,nullptr,nullptr);
            adapter_name.pop_back();name_="Direct3D 12: "+adapter_name;break;
        }
        if(!device_)throw std::runtime_error("GS Direct3D 12 needs a hardware adapter with Shader Model 6.0 and Int64ShaderOps");
        // A failed/deadline-expired submission must be terminated before its
        // buffers can be released. Older runtimes without this lifecycle API
        // remain on the CPU renderer rather than allowing unbounded waits.
        checked(device_.As(&lifecycle_),"bounded device lifecycle discovery");
        D3D12_COMMAND_QUEUE_DESC queue_desc{};queue_desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        checked(device_->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue_)),"queue creation");
        checked(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator_)),"allocator creation");
        checked(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator_.Get(),nullptr,IID_PPV_ARGS(&commands_)),"command list creation");
        checked(commands_->Close(),"initial command list close");
        D3D12_ROOT_PARAMETER parameters[5]{};
        parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues=1;
        parameters[0].Constants.ShaderRegister=0;
        for(UINT i=1;i<4;++i) {
            parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
            parameters[i].Descriptor.ShaderRegister=i-1;
        }
        parameters[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[4].Descriptor.ShaderRegister=0;
        D3D12_ROOT_SIGNATURE_DESC root_desc{};root_desc.NumParameters=5;root_desc.pParameters=parameters;
        ComPtr<ID3DBlob> serialized,error;
        checked(serialize_root_(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error),"root signature serialization");
        checked(device_->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root_)),"root signature creation");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};pipeline_desc.pRootSignature=root_.Get();
        pipeline_desc.CS={ps2n_gs_flat_dxil,sizeof(ps2n_gs_flat_dxil)};
        checked(device_->CreateComputePipelineState(&pipeline_desc,IID_PPV_ARGS(&pipeline_)),"integer raster pipeline creation");
        device_memory_=buffer(kBufferBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,true);
        upload_=buffer(kBufferBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        readback_=buffer(kBufferBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        primitive_upload_=buffer(256u*sizeof(GsGpuCommand),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        checked(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)),"fence creation");
        D3D12_QUERY_HEAP_DESC query_desc{};query_desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query_desc.Count=2;
        if(SUCCEEDED(queue_->GetTimestampFrequency(&timestamp_frequency_)) && timestamp_frequency_ &&
           SUCCEEDED(device_->CreateQueryHeap(&query_desc,IID_PPV_ARGS(&queries_)))) {
            query_readback_=buffer(16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            stats_.gpu_timestamps=true;
        }
        preflight();
    }
    GsGpuResult execute(std::span<std::uint32_t> words,std::span<std::uint32_t> known,
                        std::span<const GsGpuCommand> primitives,std::span<const GsGpuRange> uploads,
                        std::span<const GsGpuRange> readbacks) override {
        const std::lock_guard lock(mutex_);
        if(failure_)std::rethrow_exception(failure_);
        if(words.size_bytes()!=kVramBytes || known.size_bytes()!=kVramBytes || primitives.size()>256u)
            throw std::runtime_error("GS Direct3D 12 invalid VRAM or batch size");
        for(const auto ranges:{uploads,readbacks})for(const auto& range:ranges)
            if(!range.word_count||range.first_word>=words.size()||range.word_count>words.size()-range.first_word)
                throw std::runtime_error("GS Direct3D 12 VRAM synchronization range is out of bounds");
        for(const auto& p:primitives) {
            if(p.left<0||p.top<0||p.right>2048||p.bottom>2048||p.left>=p.right||p.top>=p.bottom||
               p.psm>1u||(p.zpsm>1u&&p.zpsm!=10u&&(p.depth_write||((p.test&0x10000u)&&((p.test>>17u)&3u)>=2u)))||
               p.bw>4032u||!p.bw||p.bw%64u||p.right>static_cast<int>(p.bw))
                throw std::runtime_error("GS Direct3D 12 received an unsupported or aliased primitive packet");
            if(p.zpsm==10u&&p.z>0xffffu&&(p.depth_write||((p.test&0x10000u)&&((p.test>>17u)&3u)>=2u)))
                throw std::runtime_error("GS Direct3D 12 received an out-of-range Z16S primitive depth");
            if(p.texture_flags>63u || ((p.texture_flags&1u) &&
               (p.triangle||p.tbp>=kVramBytes||p.tbp%256u||p.tbw>4032u||p.tbw%64u||
                (p.tpsm!=0u&&p.tpsm!=1u&&p.tpsm!=19u&&p.tpsm!=20u)||p.tw>10u||p.th>10u||
                p.clamp_u>3u||p.clamp_v>3u||p.minu>1023u||p.maxu>1023u||p.minv>1023u||p.maxv>1023u||
                (p.clamp_u==2u&&p.minu>p.maxu)||(p.clamp_v==2u&&p.minv>p.maxv)||p.x0==p.x1||p.y0==p.y1)))
                throw std::runtime_error("GS Direct3D 12 received an unsupported texture primitive packet");
        }
        if(primitives.empty()&&uploads.empty()&&readbacks.empty())return {};
        try {
            auto start=Clock::now();
            void* mapped{};const D3D12_RANGE no_read{0,0};
            if(!primitives.empty()) {
                checked(primitive_upload_->Map(0,&no_read,&mapped),"primitive upload mapping");
                std::memcpy(mapped,primitives.data(),primitives.size_bytes());
                const D3D12_RANGE written{0,primitives.size_bytes()};primitive_upload_->Unmap(0,&written);
                stats_.command_upload_bytes+=primitives.size_bytes();
            }
            if(!uploads.empty()||!primitives.empty()) {
                checked(upload_->Map(0,&no_read,&mapped),"upload mapping");
                auto* bytes=static_cast<std::uint8_t*>(mapped);
                SIZE_T first=static_cast<SIZE_T>(kBufferBytes),last{};
                for(const auto& range:uploads) {
                    const auto offset=static_cast<SIZE_T>(range.first_word)*sizeof(std::uint32_t);
                    const auto size=static_cast<SIZE_T>(range.word_count)*sizeof(std::uint32_t);
                    std::memcpy(bytes+offset,words.data()+range.first_word,size);
                    std::memcpy(bytes+kVramBytes+offset,known.data()+range.first_word,size);
                    first=std::min(first,offset);last=std::max(last,static_cast<SIZE_T>(kVramBytes)+offset+size);
                    stats_.upload_bytes+=2ull*size;
                }
                if(!uploads.empty())++stats_.upload_syncs;
                if(!primitives.empty()) {
                    std::memset(bytes+kResultOffset,0,12u);stats_.result_upload_bytes+=12u;
                    first=std::min(first,static_cast<SIZE_T>(kResultOffset));last=static_cast<SIZE_T>(kResultOffset)+12u;
                }
                const D3D12_RANGE written{first,last};upload_->Unmap(0,&written);
            }
            stats_.upload_ns+=elapsed(start);
            start=Clock::now();
            checked(allocator_->Reset(),"allocator reset");checked(commands_->Reset(allocator_.Get(),pipeline_.Get()),"command list reset");
            for(const auto& range:uploads) {
                const auto offset=static_cast<UINT64>(range.first_word)*sizeof(std::uint32_t);
                const auto size=static_cast<UINT64>(range.word_count)*sizeof(std::uint32_t);
                commands_->CopyBufferRegion(device_memory_.Get(),offset,upload_.Get(),offset,size);
                commands_->CopyBufferRegion(device_memory_.Get(),kVramBytes+offset,upload_.Get(),kVramBytes+offset,size);
            }
            if(!primitives.empty()) {
                commands_->CopyBufferRegion(device_memory_.Get(),kResultOffset,upload_.Get(),kResultOffset,12u);
                transition(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                commands_->SetComputeRootSignature(root_.Get());
                const auto address=device_memory_->GetGPUVirtualAddress();
                commands_->SetComputeRootUnorderedAccessView(1,address);
                commands_->SetComputeRootUnorderedAccessView(2,address+kVramBytes);
                commands_->SetComputeRootUnorderedAccessView(3,address+kResultOffset);
                commands_->SetComputeRootShaderResourceView(4,primitive_upload_->GetGPUVirtualAddress());
                if(queries_)commands_->EndQuery(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
                for(UINT index=0;index<primitives.size();++index) {
                    const auto& primitive=primitives[index];
                    commands_->SetComputeRoot32BitConstants(0,1,&index,0);
                    commands_->Dispatch(static_cast<UINT>((primitive.right-primitive.left+7)/8),
                        static_cast<UINT>((primitive.bottom-primitive.top+7)/8),1);
                    // A dispatch is one original primitive. Make every write and
                    // known-bit join visible before the next primitive reads it.
                    D3D12_RESOURCE_BARRIER order{};order.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    order.UAV.pResource=device_memory_.Get();commands_->ResourceBarrier(1,&order);
                }
                if(queries_) {
                    commands_->EndQuery(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
                    commands_->ResolveQueryData(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,query_readback_.Get(),0);
                }
            }
            if(!readbacks.empty()||!primitives.empty()) {
                transition(primitives.empty()?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
                for(const auto& range:readbacks) {
                    const auto offset=static_cast<UINT64>(range.first_word)*sizeof(std::uint32_t);
                    const auto size=static_cast<UINT64>(range.word_count)*sizeof(std::uint32_t);
                    commands_->CopyBufferRegion(readback_.Get(),offset,device_memory_.Get(),offset,size);
                    commands_->CopyBufferRegion(readback_.Get(),kVramBytes+offset,device_memory_.Get(),kVramBytes+offset,size);
                }
                if(!primitives.empty())commands_->CopyBufferRegion(readback_.Get(),kResultOffset,device_memory_.Get(),kResultOffset,12u);
                transition(D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            }
            checked(commands_->Close(),"batch close");
            ID3D12CommandList* lists[]{commands_.Get()};in_flight_=true;queue_->ExecuteCommandLists(1,lists);
            ++stats_.submissions;
            const auto signal=++fence_value_;checked(queue_->Signal(fence_.Get(),signal),"batch signal");
            stats_.submit_ns+=elapsed(start);start=Clock::now();
            if(fence_->GetCompletedValue()<signal) {
                checked(fence_->SetEventOnCompletion(signal,event_.value),"completion event");
                ++stats_.waits;
                if(WaitForSingleObject(event_.value,30000)!=WAIT_OBJECT_0)
                    throw std::runtime_error("GS Direct3D 12 rasterization exceeded the 30-second fence deadline");
            }
            in_flight_=false;
            checked(device_->GetDeviceRemovedReason(),"device execution");stats_.wait_ns+=elapsed(start);
            if(queries_&&!primitives.empty()) {
                const D3D12_RANGE query_range{0,16};checked(query_readback_->Map(0,&query_range,&mapped),"timestamp mapping");
                UINT64 timestamps[2]{};std::memcpy(timestamps,mapped,sizeof(timestamps));query_readback_->Unmap(0,&no_read);
                const auto ticks=timestamps[1]-timestamps[0];
                stats_.gpu_ns+=(ticks/timestamp_frequency_)*1000000000ull+
                    ((ticks%timestamp_frequency_)*1000000000ull)/timestamp_frequency_;
            }
            std::uint32_t result[3]{};
            if(!readbacks.empty()||!primitives.empty()) {
                start=Clock::now();
                SIZE_T first=static_cast<SIZE_T>(kBufferBytes),last{};
                for(const auto& range:readbacks) {
                    const auto offset=static_cast<SIZE_T>(range.first_word)*sizeof(std::uint32_t);
                    const auto size=static_cast<SIZE_T>(range.word_count)*sizeof(std::uint32_t);
                    first=std::min(first,offset);last=std::max(last,static_cast<SIZE_T>(kVramBytes)+offset+size);
                }
                if(!primitives.empty()) {
                    first=std::min(first,static_cast<SIZE_T>(kResultOffset));last=static_cast<SIZE_T>(kResultOffset)+12u;
                }
                const D3D12_RANGE readable{first,last};
                checked(readback_->Map(0,&readable,&mapped),"readback mapping");
                const auto* bytes=static_cast<const std::uint8_t*>(mapped);
                if(!primitives.empty()){std::memcpy(result,bytes+kResultOffset,sizeof(result));stats_.result_readback_bytes+=12u;}
                if(result[0]) {
                    readback_->Unmap(0,&no_read);
                    throw std::runtime_error("GS Direct3D 12 shader rejected VRAM/texture proof (error mask="+
                        std::to_string(result[0])+": 1 unknown VRAM, 2 UV precision, 4 unloaded CLUT, 8 bilinear precision); batch was not published");
                }
                for(const auto& range:readbacks) {
                    const auto offset=static_cast<SIZE_T>(range.first_word)*sizeof(std::uint32_t);
                    const auto size=static_cast<SIZE_T>(range.word_count)*sizeof(std::uint32_t);
                    std::memcpy(words.data()+range.first_word,bytes+offset,size);
                    std::memcpy(known.data()+range.first_word,bytes+kVramBytes+offset,size);
                    stats_.readback_bytes+=2ull*size;
                }
                if(!readbacks.empty())++stats_.readback_syncs;
                readback_->Unmap(0,&no_read);stats_.readback_ns+=elapsed(start);
            }
            if(!primitives.empty()){++stats_.batches;stats_.primitives+=primitives.size();}
            return {result[1],result[2]};
        } catch(const std::exception& error) {fail_batch(error.what());}
        catch(...) {fail_batch("GS Direct3D 12 encountered an unknown host execution error");}
    }
    const GsGpuStats& stats() const noexcept override{return stats_;}
    const std::string& device_name() const noexcept override{return name_;}
};
}
std::shared_ptr<GsGpu> create_gs_directx(){return std::make_shared<DirectXGs>();}
} // namespace ps2native
