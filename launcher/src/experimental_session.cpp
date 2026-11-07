#include "experimental_session.hpp"
#include "host_audio.hpp"
#include "qt_support.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/frontend/content.hpp"
#include "ps2native/sha256.hpp"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QPainter>
#include <QSettings>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#endif
#ifdef PS2N_SESSION_AOT
namespace ps2native { const AotModule& session_aot_module(); }
#endif
#ifdef PS2N_SESSION_AUX_AOT
namespace ps2native { const AotModule& session_aux_aot_module(); }
#endif
#ifdef PS2N_SESSION_BIOS_AOT
namespace ps2native { const AotModule& session_bios_aot_module(); }
#endif
#ifdef PS2N_SESSION_KERNEL_AOT
namespace ps2native { const AotModule& session_kernel_aot_module(); }
#endif
namespace ps2launcher {
using namespace ps2native;
using frontend::SessionState;
namespace {
using Clock=std::chrono::steady_clock;
std::string required(const QJsonObject& o,const char* key){
    const auto value=o.value(QLatin1String(key)).toString();
    if(value.isEmpty())throw std::runtime_error(std::string("Missing experimental manifest field: ")+key);
    return utf8(value);
}
QJsonObject readManifest(const std::filesystem::path& path){
    const auto bytes=read_file(path);QJsonParseError error;
    const auto doc=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes.data()),static_cast<qsizetype>(bytes.size())),&error);
    if(error.error!=QJsonParseError::NoError || !doc.isObject())throw std::runtime_error("Experimental manifest is not a JSON object");
    const auto o=doc.object();if(o.value("format").toInt()!=1)throw std::runtime_error("Unsupported experimental manifest format");
    required(o,"hypotheses");return o;
}
int keyBit(int key){
    switch(key){case Qt::Key_Up:return 4;case Qt::Key_Right:return 5;case Qt::Key_Down:return 6;case Qt::Key_Left:return 7;
    case Qt::Key_Return:case Qt::Key_Enter:return 3;case Qt::Key_Z:return 14;case Qt::Key_X:return 13;case Qt::Key_Backspace:return 0;default:return -1;}
}
}
struct ExperimentalSession::Impl {
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::thread worker;
    SessionState state{SessionState::Idle};
    bool stop{},pause{},reviewed{},audioReady{},originalBoot{};
    QString manifest,status{QStringLiteral("Selecione um manifesto local de checkpoint experimental.")},error,metrics;
    std::filesystem::path save;
    QImage frame;
    bool newFrame{};
    std::deque<std::uint16_t> input;
    std::uint16_t keys{0xFFFF},sampled{0xFFFF},gamepad{0xFFFF};
    std::deque<QByteArray> pcm;
    std::size_t pcmBytes{};
    HostAudio audio;
    std::uint64_t frames{},painted{},cycles{},aot{},interpreted{},polls{},nonzero{},submitted{},lastFrames{},lastCycles{},underruns{};
    std::uint64_t lastPoll{},lastButtons{0xFFFF};
    std::uint64_t iopAot{},iopInterpreted{},vu0Aot{},vu0Interpreted{},vu1Aot{},vu1Interpreted{},gpuPrimitives{},gpuBatches{};
    QString graphics;
    bool hadAudio{},focused{},audioEmpty{};
    Clock::time_point sampleTime{Clock::now()};
    QString audioStatus,gamepadStatus;
    std::array<unsigned,16> mapping{0x20,0,0,0x10,1,8,2,4,0,0,0x100,0x200,0x8000,0x2000,0x1000,0x4000};
#ifdef _WIN32
    HMODULE xinput{};
    using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
    GetState getState{};
    ~Impl(){if(xinput)FreeLibrary(xinput);}
#endif
};
ExperimentalSession::ExperimentalSession(QObject* parent):QObject(parent),p_(std::make_unique<Impl>()){
    p_->manifest=QSettings().value("experimental/manifest").toString();
    connect(&timer_,&QTimer::timeout,this,&ExperimentalSession::tick);timer_.start(10);
#ifdef _WIN32
    wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
    p_->xinput=LoadLibraryW((std::wstring(system)+L"\\xinput1_4.dll").c_str());
    if(p_->xinput)p_->getState=reinterpret_cast<Impl::GetState>(GetProcAddress(p_->xinput,"XInputGetState"));
#endif
}
ExperimentalSession::~ExperimentalSession(){stop();if(p_->worker.joinable())p_->worker.join();}
SessionState ExperimentalSession::state()const{std::lock_guard lock(p_->mutex);return p_->state;}
std::string ExperimentalSession::last_error()const{std::lock_guard lock(p_->mutex);return utf8(p_->error);}
QString ExperimentalSession::status()const{std::lock_guard lock(p_->mutex);return p_->status;}
QString ExperimentalSession::metrics()const{std::lock_guard lock(p_->mutex);return p_->metrics;}
bool ExperimentalSession::active()const{auto s=state();return s==SessionState::Starting || s==SessionState::Running || s==SessionState::Paused || s==SessionState::Stopping;}
bool ExperimentalSession::paused()const{return state()==SessionState::Paused;}
bool ExperimentalSession::originalBoot()const{std::lock_guard lock(p_->mutex);return p_->originalBoot;}
QString ExperimentalSession::manifest()const{return p_->manifest;}
void ExperimentalSession::setManifest(const QString& value){if(active())return;setSessionManifest(value);QSettings().setValue("experimental/manifest",value);}
void ExperimentalSession::setSessionManifest(const QString& value){if(active())return;p_->manifest=value;{std::lock_guard lock(p_->mutex);p_->originalBoot=false;}emit manifestChanged();emit changed();}
void ExperimentalSession::selectManifest(const QUrl& url){setManifest(local_path(url));}
void ExperimentalSession::begin(bool reviewed){p_->reviewed=reviewed;frontend::SessionRequest r;r.experimental_manifest=fspath(p_->manifest);start(r);}
void ExperimentalSession::start(const frontend::SessionRequest& request){
    if(active())return;
    if(p_->worker.joinable())p_->worker.join();
    p_->audioReady=p_->audio.open();p_->audioStatus=p_->audioReady?p_->audio.description():p_->audio.error();
    {
        std::lock_guard lock(p_->mutex);p_->state=SessionState::Starting;p_->stop=p_->pause=false;p_->error.clear();
        p_->originalBoot=request.experimental_manifest.empty();
        p_->status=p_->originalBoot?tr("Validating BIOS and disc; booting from reset…"):QStringLiteral("Validando dependências e restaurando checkpoint…");p_->frame={};p_->newFrame=false;
        p_->input.clear();p_->pcm.clear();p_->pcmBytes=0;p_->keys=p_->sampled=p_->gamepad=0xFFFF;p_->save.clear();
        p_->frames=p_->painted=p_->cycles=p_->aot=p_->interpreted=p_->polls=p_->nonzero=p_->submitted=0;
        p_->lastFrames=p_->lastCycles=p_->underruns=0;p_->lastPoll=0;p_->lastButtons=0xFFFF;
        p_->iopAot=p_->iopInterpreted=p_->vu0Aot=p_->vu0Interpreted=p_->vu1Aot=p_->vu1Interpreted=p_->gpuPrimitives=p_->gpuBatches=0;
        p_->graphics=QStringLiteral("software GS");
        p_->hadAudio=p_->audioEmpty=false;p_->sampleTime=Clock::now();
        p_->mapping={0x20,0,0,0x10,1,8,2,4,0,0,0x100,0x200,0x8000,0x2000,0x1000,0x4000};
    }
    emit frameReady(QImage{});
    emit changed();
    const bool reviewed=p_->reviewed;
    p_->worker=std::thread([this,request,reviewed]{try{
        const bool original=request.experimental_manifest.empty();
        const auto config=original?QJsonObject{}:readManifest(request.experimental_manifest);
        const auto base=request.experimental_manifest.parent_path();
        const auto path=[&](const char* name){auto f=fspath(qstr(required(config,name)));return f.is_absolute()?f:base/f;};
        auto memory=std::make_unique<Memory>();EEState ee;
        std::shared_ptr<GsGpu> selectedGpu;
        // IOP keeps a pointer to the validated NVM for the entire session.
        std::optional<NvmImage> sessionNvm;
        CheckpointContext context;CheckpointInfo info;
        std::vector<AotBlock> blocks;AotModule merged{};const AotModule* module=nullptr;
        if(original){
            if(request.renderer!=frontend::RendererMode::GsFaithful)throw std::runtime_error("Native renderer is unavailable for original boot");
            if(request.expected_rom0_sha256.empty())throw std::runtime_error("Original boot requires the identified ROM0 SHA-256");
            const auto bios=load_bios_rom0(read_file(request.rom0),BiosLoadOptions{request.expected_rom0_sha256});
            const auto* hardware=request.console_profile.empty()?nullptr:find_console_hardware(request.console_profile);
            if(!request.console_profile.empty() && !hardware)throw std::runtime_error("Unknown console profile");
            if(!request.nvm.empty()){
                if(!hardware || request.expected_nvm_sha256.empty())throw std::runtime_error("NVM requires an identified console profile and SHA-256");
                sessionNvm=load_nvm_image(read_file(request.nvm),*hardware,NvmLoadOptions{request.expected_nvm_sha256});
            }
            if(!request.disc)throw std::runtime_error("No game disc selected");
            const auto probe=frontend::probe_disc(*request.disc);
            if(probe.status!=frontend::ProbeStatus::Valid || !probe.info.contains("platform") || probe.info.at("platform")!="PlayStation 2")
                throw std::runtime_error("Disc refused: "+probe.error);
            if(request.expected_disc_boot.empty() || probe.info.at("boot")!=request.expected_disc_boot)
                throw std::runtime_error("Disc BOOT2 differs from the identified library entry; reidentify the disc");
            auto discPath=*request.disc;
            if(probe.info.contains("cue_tracks")){
                const auto sheet=frontend::parse_cue_sheet(discPath);
                if(!sheet.data_track())throw std::runtime_error("CUE has no data track");
                discPath=sheet.data_track()->file;
            }
            auto disc=std::make_shared<DiscImage>(discPath);
            (void)disc->boot_path();
#ifdef PS2N_SESSION_BIOS_AOT
            module=&session_bios_aot_module();
#endif
            prepare_ee_boot(bios,module,ee,*memory,hardware,sessionNvm?&*sessionNvm:nullptr);
            const auto medium=probe.info.find("media");
            memory->mount_disc(std::move(disc),medium!=probe.info.end() && medium->second=="DVD"?Iop::DiscMedium::Ps2Dvd:Iop::DiscMedium::Ps2Cd,true);
        }else{
        const auto bios=load_bios_rom0(read_file(path("rom0")),BiosLoadOptions{required(config,"rom0_sha256")});
        const auto* hardware=find_console_hardware(required(config,"console"));if(!hardware)throw std::runtime_error("Unknown console profile");
        sessionNvm=load_nvm_image(read_file(path("nvm")),*hardware,NvmLoadOptions{required(config,"nvm_sha256")});
        const auto& nvm=*sessionNvm;
        const auto r=config.value("rom1_identity").toObject();
        Rom1Identity identity{required(r,"rom0_sha256"),required(r,"romver"),required(r,"model"),required(r,"mainboard"),required(r,"sha256"),required(r,"build"),required(r,"dvd_version"),required(r,"provenance")};
        const auto rom1=load_bios_rom1(read_file(path("rom1")),bios,*hardware,identity);
        prepare_ee_boot(bios,nullptr,ee,*memory,hardware,&nvm,nullptr,&rom1);
        if(config.contains("rtc_epoch_utc")) {
            bool valid=false;const auto epoch=config.value("rtc_epoch_utc").toString().toULongLong(&valid);
            if(!valid)throw std::runtime_error("rtc_epoch_utc must be a decimal UTC epoch string");
            memory->configure_rtc(epoch);
        }
        const auto medium=required(config,"disc_medium");
        if(medium!="ps2-cd" && medium!="ps2-dvd")throw std::runtime_error("disc_medium must be ps2-cd or ps2-dvd");
        memory->mount_disc(std::make_shared<DiscImage>(path("disc")),medium=="ps2-cd"?Iop::DiscMedium::Ps2Cd:Iop::DiscMedium::Ps2Dvd,true);
        const auto checkpointPath=path("checkpoint");
        if(sha256_file(checkpointPath)!=required(config,"checkpoint_sha256"))throw std::runtime_error("Checkpoint file SHA-256 differs from reviewed manifest");
        context.scenario_sha256=required(config,"scenario_sha256");context.expected_disc_sha256=required(config,"disc_sha256");context.allow_runtime_change=reviewed;
        info=load_checkpoint(checkpointPath,ee,*memory,context);
        const auto graphics=config.value("gs_backend").toString(QStringLiteral("cpu"));
        if(graphics!=QStringLiteral("cpu") && graphics!=QStringLiteral("vulkan") && graphics!=QStringLiteral("directx"))
            throw std::runtime_error("Unknown gs_backend in experimental manifest");
        if(graphics!=QStringLiteral("cpu")) {
            std::string reason;selectedGpu=create_gs_gpu(reason,graphics==QStringLiteral("vulkan") ? GsGpuApi::Vulkan : GsGpuApi::DirectX);
            if(!selectedGpu)throw std::runtime_error("Experimental GPU GS unavailable: "+reason);
            memory->set_gs_gpu_backend(selectedGpu);
        }
        const auto liveIdentity=QByteArray("ps2n-live-sio2-poll-queue-v1:")+QByteArray::fromStdString(info.scenario_sha256);
        context.scenario_sha256=sha256_hex(std::span(reinterpret_cast<const std::uint8_t*>(liveIdentity.constData()),static_cast<std::size_t>(liveIdentity.size())));
        // Loading restores the existing diagnostic assumptions, without adding
        // any refinement, hardware defaults or title-specific behavior here.
        const auto addModule=[&](const AotModule& a,const char* elfKey){
            const auto elf=parse_ee_elf(read_file(path(elfKey)));
            if(std::string_view(a.source_kind)!="elf" || sha256_hex(elf.bytes)!=a.source_sha256)throw std::runtime_error("Compiled AOT/ELF identity differs");
            // Verify block entries against restored memory, allowing mutable
            // data in executable ELF segments. aot_enter checks every executed
            // instruction, including instructions after each block entry.
            for(std::size_t i=0;i<a.block_count;++i){
                const auto pc=a.blocks[i].address;
                const auto sourcePc=pc>=0x80000000u && pc<0xC0000000u ? pc&0x1FFFFFFFu : pc;
                const auto segment=std::find_if(elf.segments.begin(),elf.segments.end(),[&](const auto& s){return (s.flags&1u) && sourcePc>=s.vaddr && sourcePc-s.vaddr+4u<=s.file_size;});
                if(segment==elf.segments.end())throw std::runtime_error("AOT block outside verified executable ELF");
                for(unsigned j=0;j<4;++j){const auto byte=memory->diagnostic_ram(sourcePc+j,1);
                    if(byte.unknown || byte.value!=elf.bytes[segment->file_offset+sourcePc-segment->vaddr+j])throw std::runtime_error("AOT block bytes differ from checkpoint RAM");}
            }
            blocks.insert(blocks.end(),a.blocks,a.blocks+a.block_count);
        };
        Q_UNUSED(addModule);
#ifdef PS2N_SESSION_AOT
        addModule(session_aot_module(),"aot_elf");
#endif
#ifdef PS2N_SESSION_AUX_AOT
        addModule(session_aux_aot_module(),"aux_aot_elf");
#endif
#ifdef PS2N_SESSION_KERNEL_AOT
        addModule(session_kernel_aot_module(),"kernel_aot_elf");
#endif
        if(!blocks.empty()){
            std::sort(blocks.begin(),blocks.end(),[](const auto& a,const auto& b){return a.address<b.address;});
            for(std::size_t i=1;i<blocks.size();++i)if(blocks[i-1].address==blocks[i].address)throw std::runtime_error("Compiled session AOT modules overlap");
            merged.blocks=blocks.data();merged.block_count=blocks.size();module=&merged;
        }
        }
        auto& iop=const_cast<Iop&>(memory->iop());
        iop.set_pad_poll_source([this](unsigned port,std::uint64_t cycle){
            if(port)return std::uint16_t{0xFFFF};
            std::lock_guard lock(p_->mutex);
            if(!p_->input.empty()){p_->sampled=p_->input.front();p_->input.pop_front();}
            ++p_->polls;p_->lastPoll=cycle;p_->lastButtons=p_->sampled;return p_->sampled;
        });
        QByteArray pcm;std::uint64_t nonzero=0;
        iop.set_spu2_sample_sink([&](unsigned core,const Iop::Spu2Sample& sample){
            if(core!=1)return;
            for(const auto value:{sample.left,sample.right}){const auto u=static_cast<std::uint16_t>(value);pcm.append(static_cast<char>(u));pcm.append(static_cast<char>(u>>8u));}
            nonzero+=sample.left!=0 || sample.right!=0;
        });
        BootOptions options;options.max_instructions=100000;options.tail_instructions=0;options.max_trace_events=1;
        options.copy_device_histories=false;
        options.on_frame=[this](const Framebuffer& f){
            QImage image(f.rgba.data(),static_cast<int>(f.width),static_cast<int>(f.height),static_cast<int>(f.stride),QImage::Format_RGBA8888);
            std::lock_guard lock(p_->mutex);p_->frame=image.copy();p_->newFrame=true;++p_->frames;
        };
        BootReport boundary;boundary.outcome=BootOutcome::BudgetExhausted;boundary.pc=ee.pc;
        {
            std::lock_guard lock(p_->mutex);p_->cycles=ee.cycles();p_->lastCycles=ee.cycles();p_->sampleTime=Clock::now();p_->state=SessionState::Running;
            if(selectedGpu)p_->graphics=qstr(selectedGpu->device_name());
#ifdef PS2N_NATIVE_ONLY
            p_->status=original?tr("Original BIOS boot with game disc — static AOT, software GS. Missing translations are reported explicitly."):QStringLiteral("Experimental — %1\n%2").arg(module?tr("Static AOT"):tr("Missing AOT"),qstr(required(config,"hypotheses")));
#else
            p_->status=original?tr("Original BIOS boot with game disc — reference interpreter, software GS. Compatibility is incomplete; execution stops at the first unsupported behavior."):QStringLiteral("Experimental — %1\n%2").arg(module?QStringLiteral("AOT + interpretador explícito"):QStringLiteral("somente interpretador"),qstr(required(config,"hypotheses")));
#endif
            const auto map=config.value("gamepad_mapping").toArray();
            if(!map.isEmpty()){
                if(map.size()!=16)throw std::runtime_error("gamepad_mapping needs 16 XInput masks (one per PS2 bit)");
                for(int i=0;i<16;++i){const auto value=map[i].toInt(-1);if(value<0 || value>65535)throw std::runtime_error("Invalid XInput mask");p_->mapping[i]=static_cast<unsigned>(value);}
            }
        }
        while(true){
            std::filesystem::path saving;
            {
                std::unique_lock lock(p_->mutex);
                if(p_->stop)break;
                if(p_->pause){
                    p_->state=SessionState::Paused;
                    if(p_->save.empty()){p_->cv.wait(lock,[&]{return p_->stop || !p_->pause || !p_->save.empty();});continue;}
                    saving=std::move(p_->save);p_->save.clear();
                }else if(p_->audioReady && p_->pcmBytes>48000){
                    p_->cv.wait_for(lock,std::chrono::milliseconds(10));continue;
                }else p_->state=SessionState::Running;
            }
            if(!saving.empty()){
                try{
                    if(original)throw std::runtime_error("Checkpoint export requires an experimental manifest");
                    if(std::filesystem::exists(saving))throw std::runtime_error("Manual save refuses to overwrite an existing checkpoint");
                    auto sidecar=saving;sidecar+=".json";
                    if(std::filesystem::exists(sidecar))throw std::runtime_error("Manual save manifest already exists");
                    context.host_metadata="{\"input\":\"live-sio2\",\"source_scenario\":\""+info.scenario_sha256+"\"}";
                    save_checkpoint(saving,ee,*memory,boundary,context);
                    auto savedConfig=config;
                    for(const auto key:{"rom0","rom1","nvm","disc","aot_elf","aux_aot_elf","kernel_aot_elf"})if(config.contains(QLatin1String(key)))savedConfig[QLatin1String(key)]=qpath(path(key));
                    savedConfig["checkpoint"]=qpath(std::filesystem::absolute(saving));
                    savedConfig["checkpoint_sha256"]=qstr(sha256_file(saving));savedConfig["scenario_sha256"]=qstr(context.scenario_sha256);
                    const auto json=QJsonDocument(savedConfig).toJson();
                    write_file_atomically(sidecar,std::span(reinterpret_cast<const std::uint8_t*>(json.constData()),static_cast<std::size_t>(json.size())));
                    std::lock_guard lock(p_->mutex);p_->status=QStringLiteral("Pausado. Checkpoint seguro salvo: %1\nPara retomar, abra o manifesto %2").arg(qpath(saving),qpath(sidecar));
                }catch(const std::exception& error){std::lock_guard lock(p_->mutex);p_->status=QStringLiteral("Falha no salvamento: %1").arg(qstr(std::string(error.what())));}
                continue;
            }
            boundary=run_ee_boot(ee,*memory,module,options);
            {
                std::lock_guard lock(p_->mutex);p_->cycles=ee.cycles();p_->aot+=boundary.aot_instructions;p_->interpreted+=boundary.interpreted_instructions;
                p_->iopAot=iop.native_instructions();p_->iopInterpreted=iop.interpreted_instructions();
                p_->vu0Aot=memory->vu0_aot_stats().native_instructions;p_->vu0Interpreted=memory->vu0_aot_stats().interpreted_instructions;
                p_->vu1Aot=memory->vu1_aot_stats().native_instructions;p_->vu1Interpreted=memory->vu1_aot_stats().interpreted_instructions;
                if(selectedGpu){p_->gpuPrimitives=selectedGpu->stats().primitives;p_->gpuBatches=selectedGpu->stats().batches;}
                p_->nonzero+=nonzero;nonzero=0;
                if(!pcm.isEmpty() && p_->audioReady){p_->pcmBytes+=static_cast<std::size_t>(pcm.size());p_->pcm.push_back(std::move(pcm));pcm.clear();}else pcm.clear();
            }
            if(boundary.outcome==BootOutcome::BlockPoint || iop_execution_refused(*memory)){
                throw std::runtime_error("EE 0x"+QString::number(boundary.pc,16).toStdString()+" ["+boundary.category+"] "+boundary.reason+
                    (boundary.iop_stop?"; IOP 0x"+QString::number(boundary.iop_pc,16).toStdString()+": "+*boundary.iop_stop:std::string{})+
                    (boundary.frame_unavailable?"; scanout: "+*boundary.frame_unavailable:std::string{}));
            }
            if(boundary.frame_unavailable){std::lock_guard lock(p_->mutex);p_->status=qstr(*boundary.frame_unavailable);}
        }
        std::lock_guard lock(p_->mutex);p_->state=SessionState::Stopped;p_->status=QStringLiteral("Sessão encerrada com segurança.");
    }catch(const std::exception& error){std::lock_guard lock(p_->mutex);p_->state=SessionState::Failed;p_->error=qstr(std::string(error.what()));p_->status=p_->error;}});
}
void ExperimentalSession::stop(){
    {std::lock_guard lock(p_->mutex);p_->stop=true;p_->pause=false;if(p_->state==SessionState::Running || p_->state==SessionState::Paused || p_->state==SessionState::Starting)p_->state=SessionState::Stopping;}
    p_->cv.notify_all();p_->audio.reset();emit changed();
}
void ExperimentalSession::pause(){{std::lock_guard lock(p_->mutex);if(p_->state==SessionState::Running || p_->state==SessionState::Starting)p_->pause=true;}p_->audio.pause();p_->cv.notify_all();}
void ExperimentalSession::resume(){releaseKeys();{std::lock_guard lock(p_->mutex);p_->pause=false;}p_->audio.resume();p_->cv.notify_all();}
void ExperimentalSession::save(const QUrl& url){std::lock_guard lock(p_->mutex);if(p_->state==SessionState::Paused){p_->save=fspath(local_path(url));p_->cv.notify_all();}}
void ExperimentalSession::key(int key,bool down){
    const auto bit=keyBit(key);if(bit<0)return;
    std::lock_guard lock(p_->mutex);p_->focused=true;const auto before=p_->keys;
    if(down)p_->keys=static_cast<std::uint16_t>(p_->keys&~(1u<<bit));else p_->keys=static_cast<std::uint16_t>(p_->keys|(1u<<bit));
    if(before!=p_->keys && p_->state==SessionState::Running){
        if(p_->input.size()<256)p_->input.push_back(p_->keys&p_->gamepad);
        else{p_->input.clear();p_->input.push_back(0xFFFF);p_->status=QStringLiteral("Fila de entrada excedeu 256 transições; botões soltos.");}
    }
}
void ExperimentalSession::releaseKeys(){std::lock_guard lock(p_->mutex);p_->keys=p_->gamepad=0xFFFF;p_->input.clear();p_->input.push_back(0xFFFF);p_->focused=false;}
void ExperimentalSession::focusInput(){std::lock_guard lock(p_->mutex);p_->focused=true;}
void ExperimentalSession::presented(){std::lock_guard lock(p_->mutex);++p_->painted;}
void ExperimentalSession::tick(){
    QImage frame;QByteArray packet;bool refresh=false;
    const bool audioBuffered=p_->audio.buffered();
    {
        std::lock_guard lock(p_->mutex);
#ifdef _WIN32
        XINPUT_STATE gamepad{};bool connected=false;
        if(p_->getState)for(DWORD i=0;i<4;++i)if(p_->getState(i,&gamepad)==ERROR_SUCCESS){connected=true;break;}
        p_->gamepadStatus=connected?QStringLiteral("XInput conectado (mapeamento no manifesto)"):QStringLiteral("sem gamepad XInput");
        std::uint16_t buttons=0xFFFF;
        if(connected && p_->focused)for(unsigned i=0;i<16;++i)if(p_->mapping[i] && (gamepad.Gamepad.wButtons&p_->mapping[i]))buttons=static_cast<std::uint16_t>(buttons&~(1u<<i));
        if(buttons!=p_->gamepad){p_->gamepad=buttons;if(p_->state==SessionState::Running && p_->input.size()<256)p_->input.push_back(buttons&p_->keys);}
#else
        p_->gamepadStatus=QStringLiteral("gamepad indisponível neste backend; teclado ativo");
#endif
        if(p_->newFrame){frame=p_->frame;p_->newFrame=false;}
        if(p_->state==SessionState::Running && p_->audioReady && p_->pcmBytes>=3840){
            while(packet.size()<3840 && !p_->pcm.empty()){
                auto& front=p_->pcm.front();const auto count=std::min(qsizetype(3840-packet.size()),front.size());
                packet.append(front.constData(),count);front.remove(0,count);p_->pcmBytes-=static_cast<std::size_t>(count);
                if(front.isEmpty())p_->pcm.pop_front();
            }
        }
        const auto now=Clock::now();const auto seconds=std::chrono::duration<double>(now-p_->sampleTime).count();
        const bool empty=p_->hadAudio && p_->state==SessionState::Running && !audioBuffered;
        if(empty && !p_->audioEmpty)++p_->underruns;
        p_->audioEmpty=empty;
        if(seconds>=1){
            const bool running=p_->state==SessionState::Running;
            const double fps=(p_->painted-p_->lastFrames)/seconds;
            const double speed=running?100.0*static_cast<double>(p_->cycles-p_->lastCycles)/(294912000.0*seconds):0;
            const auto total=p_->aot+p_->interpreted;
            p_->metrics=QStringLiteral("%1 FPS apresentados | %2% velocidade EE (clock modelado) | AOT %3% (%4 / %5)\nSIO2: %6 polls; último IOP %7, botões 0x%8 | PCM com sinal: %9; enviado: %10; faltas de buffer: %11\n%12 | %13%14")
                .arg(fps,0,'f',2).arg(speed,0,'f',2).arg(total?100.0*static_cast<double>(p_->aot)/static_cast<double>(total):0,0,'f',2)
                .arg(p_->aot).arg(total).arg(p_->polls).arg(p_->lastPoll).arg(p_->lastButtons,4,16,QChar('0')).arg(p_->nonzero).arg(p_->submitted).arg(p_->underruns)
                .arg(p_->audioStatus,p_->gamepadStatus,p_->state==SessionState::Paused?QStringLiteral(" | PAUSADO"):QString{});
            p_->metrics+=QStringLiteral("\nIOP AOT %1 / interpretadas %2 | VU0 AOT %3 / interpretadas %4 | VU1 AOT %5 / interpretadas %6\nGS: %7 | primitivas GPU: %8; lotes: %9")
                .arg(p_->iopAot).arg(p_->iopInterpreted).arg(p_->vu0Aot).arg(p_->vu0Interpreted).arg(p_->vu1Aot).arg(p_->vu1Interpreted)
                .arg(p_->graphics).arg(p_->gpuPrimitives).arg(p_->gpuBatches);
            p_->lastFrames=p_->painted;p_->lastCycles=p_->cycles;p_->sampleTime=now;refresh=true;
        }
    }
    if(!active())p_->audio.reset();
    if(!packet.isEmpty()){
        const bool accepted=p_->audio.write(packet);
        std::lock_guard lock(p_->mutex);
        if(accepted){p_->submitted+=static_cast<std::uint64_t>(packet.size()/4);p_->hadAudio=true;}
        else{p_->pcmBytes+=static_cast<std::size_t>(packet.size());p_->pcm.push_front(std::move(packet));if(!p_->audio.error().isEmpty()){p_->audioStatus=p_->audio.error();p_->audioReady=false;p_->pcm.clear();p_->pcmBytes=0;}}
    }
    if(!frame.isNull())emit frameReady(frame);
    p_->cv.notify_all();
    if(refresh)emit changed();
}
GameView::GameView(QQuickItem* parent):QQuickPaintedItem(parent){setFlag(ItemIsFocusScope);setAcceptedMouseButtons(Qt::LeftButton);setAntialiasing(false);}
void GameView::setSession(ExperimentalSession* s){
    if(session_)disconnect(session_,nullptr,this,nullptr);session_=s;
    if(s)connect(s,&ExperimentalSession::frameReady,this,[this](const QImage& image){frame_=image;pending_=true;update();});
}
void GameView::paint(QPainter* painter){
    painter->fillRect(boundingRect(),Qt::black);
    if(frame_.isNull())return;
    // A GS field contains non-square samples (e.g. 640x224 for 480i).
    // Scale that field to the selected TV aspect without weaving fields,
    // temporal interpolation or changing any RGBA bytes supplied by the GS.
    const auto source=nativePixels_ ? QSizeF(frame_.size()) : QSizeF(4,3);
    const auto size=source.scaled(boundingRect().size(),Qt::KeepAspectRatio);
    const QRectF target((width()-size.width())/2,(height()-size.height())/2,size.width(),size.height());
    painter->setRenderHint(QPainter::SmoothPixmapTransform,false);painter->drawImage(target,frame_);
    if(pending_ && session_){session_->presented();pending_=false;}
}
void GameView::keyPressEvent(QKeyEvent* e){if(session_ && !e->isAutoRepeat())session_->key(e->key(),true);e->accept();}
void GameView::keyReleaseEvent(QKeyEvent* e){if(session_ && !e->isAutoRepeat())session_->key(e->key(),false);e->accept();}
void GameView::focusOutEvent(QFocusEvent* e){if(session_)session_->releaseKeys();QQuickPaintedItem::focusOutEvent(e);}
void GameView::focusInEvent(QFocusEvent* e){if(session_)session_->focusInput();QQuickPaintedItem::focusInEvent(e);}
}
