#include "ps2native/gs_gpu.hpp"
#include "ps2native/device_timing.hpp"
#include "ps2native/gs_vram.hpp"
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ps2native {
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t elapsed(Clock::time_point before) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-before).count());
}
void check(VkResult result, const char* operation) {
    if(result != VK_SUCCESS)throw DeviceRefusal(std::string("GS Vulkan ")+operation+" failed: VkResult="+std::to_string(result));
}
constexpr std::uint32_t shader[] = {
#include "gs_flat_spv.inc"
};

class VulkanGs final : public GsGpu {
public:
    ~VulkanGs() override { cleanup(); }
    void initialize();
    GsGpuResult execute(std::span<std::uint32_t>,std::span<std::uint32_t>,std::span<const GsGpuCommand>,
        std::span<const GsGpuRange>,std::span<const GsGpuRange>) override;
    const GsGpuStats& stats() const noexcept override { return stats_; }
    const std::string& device_name() const noexcept override { return name_; }
private:
#ifdef _WIN32
    HMODULE loader_{};
#else
    void* loader_{};
#endif
    PFN_vkGetInstanceProcAddr get_instance_proc_{};
    PFN_vkGetDeviceProcAddr get_device_proc_{};
#define GS_VK_INSTANCE_FUNCTIONS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceFeatures) X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceMemoryProperties) X(EnumerateDeviceExtensionProperties) X(CreateDevice)
#define GS_VK_DEVICE_FUNCTIONS(X) \
    X(DestroyDevice) X(GetDeviceQueue) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) \
    X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdBindPipeline) X(CmdBindDescriptorSets) \
    X(CmdPushConstants) X(CmdDispatch) X(CmdPipelineBarrier) X(CreateFence) X(DestroyFence) \
    X(ResetFences) X(WaitForFences) X(QueueSubmit) X(CreateQueryPool) X(DestroyQueryPool) \
    X(CmdResetQueryPool) X(CmdWriteTimestamp) X(GetQueryPoolResults)
#define GS_VK_DECLARE(name) PFN_vk##name name##_{};
    GS_VK_INSTANCE_FUNCTIONS(GS_VK_DECLARE)
    GS_VK_DEVICE_FUNCTIONS(GS_VK_DECLARE)
#undef GS_VK_DECLARE
    VkInstance instance_{};
    VkPhysicalDevice physical_{};
    VkDevice device_{};
    VkQueue queue_{};
    std::uint32_t family_{};
    VkPhysicalDeviceMemoryProperties memory_{};
    struct Buffer {VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};};
    Buffer words_,known_,result_,primitives_;
    VkDescriptorSetLayout descriptors_{};
    VkDescriptorPool descriptor_pool_{};
    VkDescriptorSet descriptor_set_{};
    VkPipelineLayout layout_{};
    VkShaderModule shader_{};
    VkPipeline pipeline_{};
    VkCommandPool pool_{};
    VkCommandBuffer command_{};
    VkFence fence_{};
    VkQueryPool queries_{};
    float timestamp_period_{};
    std::uint32_t timestamp_bits_{};
    bool in_flight_{};
    std::exception_ptr failure_;
    GsGpuStats stats_{};
    std::string name_;
    std::mutex mutex_;
    void create_buffer(Buffer&,VkDeviceSize);
    void cleanup() noexcept;
};

void VulkanGs::initialize() {
#ifdef _WIN32
    loader_=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(loader_)get_instance_proc_=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader_,"vkGetInstanceProcAddr"));
#else
    const char* names[]={
#ifdef __APPLE__
        "libvulkan.1.dylib","libvulkan.dylib","libMoltenVK.dylib",
#else
        "libvulkan.so.1","libvulkan.so",
#endif
    };
    for(auto name:names){loader_=dlopen(name,RTLD_NOW|RTLD_LOCAL);if(loader_)break;}
    if(loader_)get_instance_proc_=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader_,"vkGetInstanceProcAddr"));
#endif
    if(!get_instance_proc_)throw DeviceRefusal("GS Vulkan loader is unavailable");
    auto create_instance=reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc_(nullptr,"vkCreateInstance"));
    auto enumerate_extensions=reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(get_instance_proc_(nullptr,"vkEnumerateInstanceExtensionProperties"));
    if(!create_instance||!enumerate_extensions)throw DeviceRefusal("GS Vulkan loader has incomplete entry points");
    std::uint32_t extension_count{};
    check(enumerate_extensions(nullptr,&extension_count,nullptr),"enumerate instance extensions");
    std::vector<VkExtensionProperties> extensions(extension_count);
    check(enumerate_extensions(nullptr,&extension_count,extensions.data()),"read instance extensions");
    const bool portability=std::any_of(extensions.begin(),extensions.end(),[](const auto& x){return std::strcmp(x.extensionName,"VK_KHR_portability_enumeration")==0;});
    const char* portability_extension="VK_KHR_portability_enumeration";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName="ps2-native experimental GS";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo=&app;
    if(portability){instance_info.flags=0x00000001u;instance_info.enabledExtensionCount=1;instance_info.ppEnabledExtensionNames=&portability_extension;}
    check(create_instance(&instance_info,nullptr,&instance_),"create instance");
#define GS_VK_LOAD_INSTANCE(name) name##_=reinterpret_cast<PFN_vk##name>(get_instance_proc_(instance_,"vk" #name)); if(!name##_)throw DeviceRefusal("GS Vulkan missing vk" #name);
    GS_VK_INSTANCE_FUNCTIONS(GS_VK_LOAD_INSTANCE)
#undef GS_VK_LOAD_INSTANCE
    get_device_proc_=reinterpret_cast<PFN_vkGetDeviceProcAddr>(get_instance_proc_(instance_,"vkGetDeviceProcAddr"));
    if(!get_device_proc_)throw DeviceRefusal("GS Vulkan missing vkGetDeviceProcAddr");
    std::uint32_t device_count{};
    check(EnumeratePhysicalDevices_(instance_,&device_count,nullptr),"enumerate devices");
    std::vector<VkPhysicalDevice> devices(device_count);
    check(EnumeratePhysicalDevices_(instance_,&device_count,devices.data()),"read devices");
    int best=-1;
    VkPhysicalDeviceProperties chosen{};
    for(auto candidate:devices) {
        VkPhysicalDeviceFeatures features{};GetPhysicalDeviceFeatures_(candidate,&features);
        VkPhysicalDeviceProperties properties{};GetPhysicalDeviceProperties_(candidate,&properties);
        if(!features.shaderInt64||properties.apiVersion<VK_API_VERSION_1_1||
           properties.limits.maxPushConstantsSize<sizeof(std::uint32_t)||
           properties.limits.maxStorageBufferRange<256u*sizeof(GsGpuCommand))continue;
        std::uint32_t count{};GetPhysicalDeviceQueueFamilyProperties_(candidate,&count,nullptr);
        std::vector<VkQueueFamilyProperties> families(count);GetPhysicalDeviceQueueFamilyProperties_(candidate,&count,families.data());
        for(std::uint32_t n=0;n<count;++n)if(families[n].queueCount&&(families[n].queueFlags&VK_QUEUE_COMPUTE_BIT)) {
            const int score=properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?4:properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?3:properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU?2:1;
            if(score>best){best=score;physical_=candidate;family_=n;chosen=properties;timestamp_bits_=families[n].timestampValidBits;}
            break;
        }
    }
    if(!physical_)throw DeviceRefusal("GS Vulkan requires a Vulkan 1.1 compute device with shaderInt64; CPU renderer remains available");
    name_=chosen.deviceName;timestamp_period_=chosen.limits.timestampPeriod;
    GetPhysicalDeviceMemoryProperties_(physical_,&memory_);
    float priority=1.f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex=family_;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
    VkPhysicalDeviceFeatures features{};features.shaderInt64=VK_TRUE;
    extension_count=0;check(EnumerateDeviceExtensionProperties_(physical_,nullptr,&extension_count,nullptr),"enumerate device extensions");
    extensions.resize(extension_count);check(EnumerateDeviceExtensionProperties_(physical_,nullptr,&extension_count,extensions.data()),"read device extensions");
    const bool subset=std::any_of(extensions.begin(),extensions.end(),[](const auto& x){return std::strcmp(x.extensionName,"VK_KHR_portability_subset")==0;});
    const char* subset_extension="VK_KHR_portability_subset";
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;device_info.pEnabledFeatures=&features;
    if(subset){device_info.enabledExtensionCount=1;device_info.ppEnabledExtensionNames=&subset_extension;}
    check(CreateDevice_(physical_,&device_info,nullptr,&device_),"create device");
#define GS_VK_LOAD_DEVICE(name) name##_=reinterpret_cast<PFN_vk##name>(get_device_proc_(device_,"vk" #name)); if(!name##_)throw DeviceRefusal("GS Vulkan missing vk" #name);
    GS_VK_DEVICE_FUNCTIONS(GS_VK_LOAD_DEVICE)
#undef GS_VK_LOAD_DEVICE
    GetDeviceQueue_(device_,family_,0,&queue_);
    create_buffer(words_,GsVram::kBytes);create_buffer(known_,GsVram::kBytes);create_buffer(result_,12u);
    create_buffer(primitives_,256u*sizeof(GsGpuCommand));
    std::array<VkDescriptorSetLayoutBinding,4> bindings{};
    for(unsigned n=0;n<bindings.size();++n){bindings[n].binding=n;bindings[n].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[n].descriptorCount=1;bindings[n].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
    VkDescriptorSetLayoutCreateInfo descriptors_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptors_info.bindingCount=static_cast<std::uint32_t>(bindings.size());descriptors_info.pBindings=bindings.data();
    check(CreateDescriptorSetLayout_(device_,&descriptors_info,nullptr,&descriptors_),"create descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(std::uint32_t)};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount=1;layout_info.pSetLayouts=&descriptors_;layout_info.pushConstantRangeCount=1;layout_info.pPushConstantRanges=&push;
    check(CreatePipelineLayout_(device_,&layout_info,nullptr,&layout_),"create pipeline layout");
    VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shader_info.codeSize=sizeof(shader);shader_info.pCode=shader;
    check(CreateShaderModule_(device_,&shader_info,nullptr,&shader_),"create shader");
    VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_info.stage.module=shader_;pipeline_info.stage.pName="main";pipeline_info.layout=layout_;
    check(CreateComputePipelines_(device_,VK_NULL_HANDLE,1,&pipeline_info,nullptr,&pipeline_),"create compute pipeline");
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4};
    VkDescriptorPoolCreateInfo descriptor_pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};descriptor_pool_info.maxSets=1;descriptor_pool_info.poolSizeCount=1;descriptor_pool_info.pPoolSizes=&pool_size;
    check(CreateDescriptorPool_(device_,&descriptor_pool_info,nullptr,&descriptor_pool_),"create descriptor pool");
    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};set_info.descriptorPool=descriptor_pool_;set_info.descriptorSetCount=1;set_info.pSetLayouts=&descriptors_;
    check(AllocateDescriptorSets_(device_,&set_info,&descriptor_set_),"allocate descriptor set");
    std::array<VkDescriptorBufferInfo,4> buffer_info{{{words_.buffer,0,GsVram::kBytes},{known_.buffer,0,GsVram::kBytes},
        {result_.buffer,0,12u},{primitives_.buffer,0,256u*sizeof(GsGpuCommand)}}};
    std::array<VkWriteDescriptorSet,4> writes{};
    for(unsigned n=0;n<writes.size();++n){writes[n].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[n].dstSet=descriptor_set_;writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[n].pBufferInfo=&buffer_info[n];}
    UpdateDescriptorSets_(device_,static_cast<std::uint32_t>(writes.size()),writes.data(),0,nullptr);
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pool_info.queueFamilyIndex=family_;
    check(CreateCommandPool_(device_,&pool_info,nullptr,&pool_),"create command pool");
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command_info.commandPool=pool_;command_info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_info.commandBufferCount=1;
    check(AllocateCommandBuffers_(device_,&command_info,&command_),"allocate command buffer");
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(CreateFence_(device_,&fence_info,nullptr,&fence_),"create fence");
    if(timestamp_bits_) {
        VkQueryPoolCreateInfo query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};query_info.queryType=VK_QUERY_TYPE_TIMESTAMP;query_info.queryCount=2;
        check(CreateQueryPool_(device_,&query_info,nullptr,&queries_),"create timestamp queries");stats_.gpu_timestamps=true;
    }
}

void VulkanGs::create_buffer(Buffer& buffer,VkDeviceSize size) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(CreateBuffer_(device_,&info,nullptr,&buffer.buffer),"create VRAM buffer");
    VkMemoryRequirements requirements{};GetBufferMemoryRequirements_(device_,buffer.buffer,&requirements);
    std::uint32_t type=memory_.memoryTypeCount;
    for(std::uint32_t n=0;n<memory_.memoryTypeCount;++n)if((requirements.memoryTypeBits&(1u<<n))&&
        (memory_.memoryTypes[n].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        // Materialization reads every byte on the CPU. Uncached coherent
        // memory makes those reads very expensive on discrete GPUs. Keep
        // the same coherence contract and prefer a CPU-cached type, without
        // requiring HOST_CACHED on devices that do not expose one.
        if(type==memory_.memoryTypeCount)type=n;
        if(memory_.memoryTypes[n].propertyFlags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT){type=n;break;}
    }
    if(type==memory_.memoryTypeCount)throw DeviceRefusal("GS Vulkan needs host-visible coherent storage memory");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
    check(AllocateMemory_(device_,&allocation,nullptr,&buffer.memory),"allocate VRAM buffer");
    stats_.storage_bytes+=requirements.size;
    check(BindBufferMemory_(device_,buffer.buffer,buffer.memory,0),"bind VRAM buffer");
    check(MapMemory_(device_,buffer.memory,0,size,0,&buffer.mapped),"map VRAM buffer");
}

GsGpuResult VulkanGs::execute(std::span<std::uint32_t> words,std::span<std::uint32_t> known,
    std::span<const GsGpuCommand> commands,std::span<const GsGpuRange> uploads,
    std::span<const GsGpuRange> readbacks) {
    std::lock_guard lock(mutex_);
    if(failure_)std::rethrow_exception(failure_);
    if(words.size_bytes()!=GsVram::kBytes||known.size_bytes()!=GsVram::kBytes)throw DeviceRefusal("GS Vulkan requires the complete existing 4 MiB VRAM and knowledge mask");
    for(const auto ranges:{uploads,readbacks})for(const auto& range:ranges)
        if(!range.word_count||range.first_word>=words.size()||range.word_count>words.size()-range.first_word)
            throw DeviceRefusal("GS Vulkan VRAM synchronization range is out of bounds");
    if(commands.empty()&&uploads.empty()&&readbacks.empty())return {};
    if(commands.size()>256u)throw DeviceRefusal("GS Vulkan batch exceeds the bounded 256 primitive limit");
    for(const auto& p:commands) {
        if(p.triangle!=0u && p.triangle!=1u && p.triangle!=3u && p.triangle!=7u)
            throw DeviceRefusal("GS Vulkan received invalid triangle flags");
        // The GS caller additionally proves non-aliasing framebuffer/depth
        // pages and constant triangle attributes. Defend the dispatch ABI.
        if(p.left<0||p.top<0||p.right>2048||p.bottom>2048||p.left>=p.right||p.top>=p.bottom||
           p.psm>1u||(p.zpsm>1u&&p.zpsm!=10u&&(p.depth_write||((p.test&0x10000u)&&((p.test>>17u)&3u)>=2u)))||
           p.bw>4032u||!p.bw||p.bw%64u||p.right>static_cast<int>(p.bw))throw DeviceRefusal("GS Vulkan received an unsupported or aliased primitive packet");
        if(p.zpsm==10u&&p.z>0xffffu&&(p.depth_write||((p.test&0x10000u)&&((p.test>>17u)&3u)>=2u)))
            throw DeviceRefusal("GS Vulkan received an out-of-range Z16S primitive depth");
        if((p.texture_flags&1u) && p.triangle &&
           (p.u0<0 || p.u0>16383 || p.v0<0 || p.v0>16383 ||
            p.u1<0 || p.u1>16383 || p.v1<0 || p.v1>16383 ||
            p.u2<0 || p.u2>16383 || p.v2<0 || p.v2>16383 ||
            std::int64_t{p.x1-p.x0}*(p.y2-p.y0)-std::int64_t{p.y1-p.y0}*(p.x2-p.x0)<=0))
            throw std::runtime_error("GS compute received invalid textured triangle coordinates");
        if(p.texture_flags>63u || ((p.texture_flags&1u) &&
           (p.tbp>=GsVram::kBytes||p.tbp%256u||p.tbw>4032u||p.tbw%64u||
            (p.tpsm!=0u&&p.tpsm!=1u&&p.tpsm!=19u&&p.tpsm!=20u)||p.tw>10u||p.th>10u||
            p.clamp_u>3u||p.clamp_v>3u||p.minu>1023u||p.maxu>1023u||p.minv>1023u||p.maxv>1023u||
            (p.clamp_u==2u&&p.minu>p.maxu)||(p.clamp_v==2u&&p.minv>p.maxv)||(!p.triangle&&(p.x0==p.x1||p.y0==p.y1)))))
            throw DeviceRefusal("GS Vulkan received an unsupported texture primitive packet");
    }
    try {
        auto before=Clock::now();
        for(const auto& range:uploads) {
            const auto bytes=static_cast<std::size_t>(range.word_count)*sizeof(std::uint32_t);
            std::memcpy(static_cast<std::uint32_t*>(words_.mapped)+range.first_word,words.data()+range.first_word,bytes);
            std::memcpy(static_cast<std::uint32_t*>(known_.mapped)+range.first_word,known.data()+range.first_word,bytes);
            stats_.upload_bytes+=2ull*bytes;
        }
        if(!uploads.empty())++stats_.upload_syncs;
        if(!commands.empty()) {
            std::memset(result_.mapped,0,12u);stats_.result_upload_bytes+=12u;
            std::memcpy(primitives_.mapped,commands.data(),commands.size_bytes());
            stats_.command_upload_bytes+=commands.size_bytes();
        }
        stats_.upload_ns+=elapsed(before);
        std::array<std::uint32_t,3> result{};
        if(!commands.empty()) {
            before=Clock::now();
            check(ResetCommandBuffer_(command_,0),"reset commands");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(BeginCommandBuffer_(command_,&begin),"begin commands");
            if(queries_){CmdResetQueryPool_(command_,queries_,0,2);CmdWriteTimestamp_(command_,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries_,0);}
            VkMemoryBarrier host_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host_barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;host_barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            CmdPipelineBarrier_(command_,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&host_barrier,0,nullptr,0,nullptr);
            CmdBindPipeline_(command_,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_);
            CmdBindDescriptorSets_(command_,VK_PIPELINE_BIND_POINT_COMPUTE,layout_,0,1,&descriptor_set_,0,nullptr);
            for(std::uint32_t index=0;index<commands.size();++index) {
                const auto& p=commands[index];
                CmdPushConstants_(command_,layout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(index),&index);
                CmdDispatch_(command_,(static_cast<std::uint32_t>(p.right-p.left)+7u)/8u,(static_cast<std::uint32_t>(p.bottom-p.top)+7u)/8u,1);
                VkMemoryBarrier order{VK_STRUCTURE_TYPE_MEMORY_BARRIER};order.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;order.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                CmdPipelineBarrier_(command_,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&order,0,nullptr,0,nullptr);
            }
            VkMemoryBarrier host_read{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host_read.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host_read.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            CmdPipelineBarrier_(command_,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host_read,0,nullptr,0,nullptr);
            if(queries_)CmdWriteTimestamp_(command_,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries_,1);
            check(EndCommandBuffer_(command_),"end commands");check(ResetFences_(device_,1,&fence_),"reset completion fence");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command_;
            check(QueueSubmit_(queue_,1,&submit,fence_),"submit primitives");in_flight_=true;
            stats_.submit_ns+=elapsed(before);++stats_.submissions;
            before=Clock::now();
            ++stats_.waits;
            const auto wait=WaitForFences_(device_,1,&fence_,VK_TRUE,10'000'000'000ull);
            stats_.wait_ns+=elapsed(before);check(wait,"wait for primitives (10 second deadline)");in_flight_=false;
            before=Clock::now();std::memcpy(result.data(),result_.mapped,12u);
            stats_.readback_ns+=elapsed(before);stats_.result_readback_bytes+=12u;
            if(result[0])throw DeviceRefusal("GS Vulkan shader rejected VRAM/texture proof (error mask="+
                std::to_string(result[0])+": 1 unknown VRAM, 2 UV precision, 4 unloaded CLUT, 8 bilinear precision); batch not committed");
            if(queries_) {
                std::array<std::uint64_t,2> timestamps{};
                check(GetQueryPoolResults_(device_,queries_,0,2,sizeof(timestamps),timestamps.data(),sizeof(std::uint64_t),VK_QUERY_RESULT_64_BIT),"read GPU timestamps");
                const auto mask=timestamp_bits_>=64u?UINT64_MAX:((std::uint64_t{1}<<timestamp_bits_)-1u);
                stats_.gpu_ns+=static_cast<std::uint64_t>(static_cast<double>((timestamps[1]-timestamps[0])&mask)*timestamp_period_);
            }
            ++stats_.batches;stats_.primitives+=commands.size();
        }
        before=Clock::now();
        for(const auto& range:readbacks) {
            const auto bytes=static_cast<std::size_t>(range.word_count)*sizeof(std::uint32_t);
            std::memcpy(words.data()+range.first_word,static_cast<const std::uint32_t*>(words_.mapped)+range.first_word,bytes);
            std::memcpy(known.data()+range.first_word,static_cast<const std::uint32_t*>(known_.mapped)+range.first_word,bytes);
            stats_.readback_bytes+=2ull*bytes;
        }
        if(!readbacks.empty())++stats_.readback_syncs;
        stats_.readback_ns+=elapsed(before);
        return {result[1],result[2]};
    } catch(...) {failure_=std::current_exception();throw;}
}

void VulkanGs::cleanup() noexcept {
    // No unbounded wait in destruction. A failed driver fence cannot safely
    // destroy objects still in use; preserve that failed device until process
    // exit. Normal boundaries have already completed their bounded fence.
    if(in_flight_)return;
    if(device_&&DestroyDevice_) {
        if(queries_&&DestroyQueryPool_)DestroyQueryPool_(device_,queries_,nullptr);
        if(fence_&&DestroyFence_)DestroyFence_(device_,fence_,nullptr);
        if(pool_&&DestroyCommandPool_)DestroyCommandPool_(device_,pool_,nullptr);
        if(descriptor_pool_&&DestroyDescriptorPool_)DestroyDescriptorPool_(device_,descriptor_pool_,nullptr);
        if(pipeline_&&DestroyPipeline_)DestroyPipeline_(device_,pipeline_,nullptr);
        if(shader_&&DestroyShaderModule_)DestroyShaderModule_(device_,shader_,nullptr);
        if(layout_&&DestroyPipelineLayout_)DestroyPipelineLayout_(device_,layout_,nullptr);
        if(descriptors_&&DestroyDescriptorSetLayout_)DestroyDescriptorSetLayout_(device_,descriptors_,nullptr);
        for(auto* b:{&primitives_,&result_,&known_,&words_}) {
            if(b->mapped&&UnmapMemory_)UnmapMemory_(device_,b->memory);
            if(b->buffer&&DestroyBuffer_)DestroyBuffer_(device_,b->buffer,nullptr);
            if(b->memory&&FreeMemory_)FreeMemory_(device_,b->memory,nullptr);
        }
        DestroyDevice_(device_,nullptr);
    }
    if(instance_&&DestroyInstance_)DestroyInstance_(instance_,nullptr);
#ifdef _WIN32
    if(loader_)FreeLibrary(loader_);
#else
    if(loader_)dlclose(loader_);
#endif
}
} // namespace
std::shared_ptr<GsGpu> create_gs_vulkan() {
    auto backend=std::make_shared<VulkanGs>();backend->initialize();return backend;
}
} // namespace ps2native
