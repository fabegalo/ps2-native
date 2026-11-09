#include "experimental_session.hpp"
#include "host_audio.hpp"
#include "gamepad_retry.hpp"
#include "qt_support.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/frontend/session.hpp"
#include "ps2native/frontend/native_module.hpp"
#include "ps2native/frontend/content.hpp"
#include "ps2native/sha256.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryFile>
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
    case Qt::Key_Return:case Qt::Key_Enter:return 3;case Qt::Key_Z:return 14;case Qt::Key_X:return 13;case Qt::Key_Backspace:return 0;
    case Qt::Key_C:return 15;case Qt::Key_V:return 12;case Qt::Key_Q:return 10;case Qt::Key_E:return 11;
    case Qt::Key_1:return 8;case Qt::Key_3:return 9;case Qt::Key_Control:return 1;case Qt::Key_Shift:return 2;default:return -1;}
 }
int axisKey(int key) {
    switch(key){case Qt::Key_W:return 0;case Qt::Key_S:return 1;case Qt::Key_A:return 2;case Qt::Key_D:return 3;
    case Qt::Key_I:return 4;case Qt::Key_K:return 5;case Qt::Key_J:return 6;case Qt::Key_L:return 7;default:return -1;}
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
    std::deque<Iop::PadInput> input;
    std::uint16_t keys{0xFFFF},gamepad{0xFFFF};
    Iop::PadInput sampled{},gamepadInput{},lastQueued{};
    std::array<bool,8> axisKeys{};
    bool customGamepadMapping{};
    Iop::PadInput composeInput() const {
        if(!focused)return {};
        auto frame=gamepadInput;frame.buttons=keys&gamepad;frame.pressureValid=0xFFFu;
        const auto axis=[&](unsigned negative,unsigned positive,std::uint8_t fallback){
            return axisKeys[negative] || axisKeys[positive]?static_cast<std::uint8_t>(axisKeys[negative]==axisKeys[positive]?128u:axisKeys[negative]?0u:255u):fallback;
        };
        frame.ly=axis(0u,1u,frame.ly);frame.lx=axis(2u,3u,frame.lx);
        frame.ry=axis(4u,5u,frame.ry);frame.rx=axis(6u,7u,frame.rx);
        constexpr std::array<unsigned,12> bits{5u,7u,4u,6u,12u,13u,14u,15u,10u,11u,8u,9u};
        for(unsigned i=0;i<bits.size();++i) {
            if(!(keys&(1u<<bits[i])))frame.pressure[i]=255u;
            else if(!(gamepadInput.pressureValid&(1u<<i)))frame.pressure[i]=(gamepad&(1u<<bits[i]))?0u:255u;
        }
        return frame;
    }
    void enqueueInput() {
        const auto frame=composeInput();if(frame==lastQueued)return;
        lastQueued=frame;
        if(input.size()<256u)input.push_back(frame);
        else{input.clear();input.push_back({});lastQueued={};status=QStringLiteral("Fila de entrada excedeu 256 transições; entrada solta.");}
    }
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
    std::array<unsigned,16> mapping{0x20,0x40,0x80,0x10,1,8,2,4,0,0,0x100,0x200,0x8000,0x2000,0x1000,0x4000};
#ifdef _WIN32
    HMODULE xinput{};
    using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
    GetState getState{};
    using SetState=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);
    SetState setState{};
    std::mutex padOutputMutex;
    DWORD gamepadIndex{0xFFFFFFFFu};
    WORD rumbleLeft{},rumbleRight{};
    GamepadRetry gamepadRetry;
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
    if(p_->xinput)p_->setState=reinterpret_cast<Impl::SetState>(GetProcAddress(p_->xinput,"XInputSetState"));
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
        p_->originalBoot=request.experimental_manifest.empty() && request.native_cache.empty();
        p_->status=!request.native_cache.empty()?tr("Preparing verified native code…"):
            p_->originalBoot?tr("Validating BIOS and disc; booting from reset…"):QStringLiteral("Validando dependências e restaurando checkpoint…");p_->frame={};p_->newFrame=false;
        p_->input.clear();p_->pcm.clear();p_->pcmBytes=0;p_->keys=p_->gamepad=0xFFFF;p_->sampled={};p_->gamepadInput={};p_->lastQueued={};p_->axisKeys={};p_->customGamepadMapping=false;p_->save.clear();
        p_->frames=p_->painted=p_->cycles=p_->aot=p_->interpreted=p_->polls=p_->nonzero=p_->submitted=0;
        p_->lastFrames=p_->lastCycles=p_->underruns=0;p_->lastPoll=0;p_->lastButtons=0xFFFF;
        p_->iopAot=p_->iopInterpreted=p_->vu0Aot=p_->vu0Interpreted=p_->vu1Aot=p_->vu1Interpreted=p_->gpuPrimitives=p_->gpuBatches=0;
        p_->graphics=QStringLiteral("software GS");
        p_->hadAudio=p_->audioEmpty=false;p_->sampleTime=Clock::now();
        p_->mapping={0x20,0x40,0x80,0x10,1,8,2,4,0,0,0x100,0x200,0x8000,0x2000,0x1000,0x4000};
    }
    emit frameReady(QImage{});
    emit changed();
    const bool reviewed=p_->reviewed;
    const bool cacheDiagnostic=QCoreApplication::arguments().contains(QStringLiteral("--diagnostic-cache-uncached-hits"));
    p_->worker=std::thread([this,request,reviewed,cacheDiagnostic]{try{
        const bool original=request.experimental_manifest.empty();
        const auto config=original?QJsonObject{}:readManifest(request.experimental_manifest);
        const auto base=request.experimental_manifest.parent_path();
        const auto path=[&](const char* name){auto f=fspath(qstr(required(config,name)));return f.is_absolute()?f:base/f;};
        std::unique_ptr<frontend::LoadedNativeModule> loaded;
        std::unique_ptr<frontend::SessionSupervisor> supervisor;
        Memory* memory=nullptr;
        const auto createSupervisor=[&]{supervisor=std::make_unique<frontend::SessionSupervisor>();memory=&supervisor->memory();};
        std::shared_ptr<GsGpu> selectedGpu;
        CheckpointContext context;CheckpointInfo info;
        std::vector<AotBlock> blocks;AotModule merged{};const AotModule* module=nullptr;
        if(original){
        if(!request.native_cache.empty()) {
            if(!request.disc)throw std::runtime_error("No game disc selected");
            auto input=*request.disc;
            const auto probe=frontend::probe_disc(input);
            if(probe.status!=frontend::ProbeStatus::Valid)throw std::runtime_error("Disc refused: "+probe.error);
            if(probe.info.contains("cue_tracks")) {
                const auto sheet=frontend::parse_cue_sheet(input);
                if(!sheet.data_track())throw std::runtime_error("CUE has no data track");
                input=sheet.data_track()->file;
            }
            auto tool=QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ps2n-dev"));
#ifdef _WIN32
            tool+=QStringLiteral(".exe");
#endif
            if(!QDir().mkpath(qpath(request.native_cache)))throw std::runtime_error("Cannot create the native cache directory");
            QTemporaryFile cancel(QDir(qpath(request.native_cache)).filePath(QStringLiteral("prepare-XXXXXX.cancel")));
            if(!cancel.open())throw std::runtime_error("Cannot create native preparation cancellation ticket");
            QProcess preparer;preparer.setProcessChannelMode(QProcess::MergedChannels);
#ifdef _WIN32
            preparer.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
            QStringList prepareArgs{QStringLiteral("prepare-native"),qpath(input),qpath(request.native_cache),QStringLiteral("--disc"),
                QStringLiteral("--rom0=")+qpath(request.rom0),QStringLiteral("--rom0-sha256=")+QString::fromStdString(request.expected_rom0_sha256),
                QStringLiteral("--cancel-file=")+cancel.fileName()};
            if(!request.console_profile.empty())prepareArgs<<QStringLiteral("--console=")+QString::fromStdString(request.console_profile);
            if(!request.nvm.empty())prepareArgs<<QStringLiteral("--nvm=")+qpath(request.nvm)<<QStringLiteral("--nvm-sha256=")+QString::fromStdString(request.expected_nvm_sha256);
            if(cacheDiagnostic)prepareArgs<<QStringLiteral("--diagnostic-cache-uncached-hits");
            preparer.start(tool,prepareArgs);
            if(!preparer.waitForStarted(10000))throw std::runtime_error("Native preparation tool is unavailable");
            QByteArray output;QElapsedTimer timeout;timeout.start();bool cancelling=false,exceeded=false; qint64 cancelledAt=0;
            while(preparer.state()!=QProcess::NotRunning) {
                preparer.waitForFinished(100);output+=preparer.readAll();
                bool stopping;{std::lock_guard lock(p_->mutex);stopping=p_->stop;}
                exceeded=exceeded || timeout.elapsed()>750000 || output.size()>8*1024*1024;
                if((stopping || exceeded) && !cancelling) {
                    if(cancel.write("1")!=1 || !cancel.flush())throw std::runtime_error("Cannot signal native preparation cancellation");
                    cancelling=true;cancelledAt=timeout.elapsed();
                }
                if(output.size()>8*1024*1024)output=output.right(4096);
                if(cancelling && timeout.elapsed()-cancelledAt>30000) {
                    preparer.kill();preparer.waitForFinished(5000);
                    throw std::runtime_error("Native preparation did not stop within its cancellation deadline; incomplete outputs retained");
                }
            }
            output+=preparer.readAll();
            {std::lock_guard lock(p_->mutex);if(p_->stop){p_->state=SessionState::Stopped;p_->status=tr("Preparation cancelled.");return;}}
            if(exceeded)throw std::runtime_error("Native preparation exceeded its time/output limit");
            if(preparer.exitStatus()!=QProcess::NormalExit || preparer.exitCode()!=0)
                throw std::runtime_error("Native preparation failed:\n"+output.right(4096).toStdString());
            QString receipt;
            for(const auto& line:output.split('\n'))if(line.startsWith("native_receipt="))receipt=QString::fromUtf8(line.mid(15).trimmed());
            if(receipt.isEmpty())throw std::runtime_error("Native preparation did not publish a verified module receipt");
            const auto image=DiscImage(input).boot_elf();
            loaded=std::make_unique<frontend::LoadedNativeModule>(fspath(receipt),sha256_file(input),sha256_hex(image.bytes));
            module=&loaded->module();
            const auto receiptBytes=read_file(fspath(receipt));
            const auto preparation=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(receiptBytes.data()),static_cast<qsizetype>(receiptBytes.size()))).object();
            const auto unsupported=preparation.value(QStringLiteral("native_unsupported_count")).toInteger();
            if(request.prepare_only) {
                std::lock_guard lock(p_->mutex);p_->state=SessionState::Stopped;
                const auto discovery=preparation.value(QStringLiteral("discovery")).toObject();
                if(!discovery.value(QStringLiteral("reason")).toString().isEmpty()) {
                    p_->status=tr("Native artifacts prepared. Startup stopped: %1").arg(discovery.value(QStringLiteral("reason")).toString().left(300));return;
                }
                p_->status=unsupported ? tr("Partial preparation: %1 instructions have no native translation. Play stops if reached.").arg(unsupported) :
                    tr("Native code prepared. Select Play to start; game compatibility remains unverified.");return;
            }
            createSupervisor();supervisor->prepare_game(request,module);
            if(cacheDiagnostic)memory->diagnostic_data_cache(true);
        }else{
#ifdef PS2N_SESSION_BIOS_AOT
            module=&session_bios_aot_module();
#endif
            createSupervisor();supervisor->prepare_original(request,module);
        }
        }else{
        createSupervisor();auto& ee=supervisor->ee();
        const auto bios=load_bios_rom0(read_file(path("rom0")),BiosLoadOptions{required(config,"rom0_sha256")});
        const auto* hardware=find_console_hardware(required(config,"console"));if(!hardware)throw std::runtime_error("Unknown console profile");
        const auto nvm=load_nvm_image(read_file(path("nvm")),*hardware,NvmLoadOptions{required(config,"nvm_sha256")});
        const auto r=config.value("rom1_identity").toObject();
        Rom1Identity identity{required(r,"rom0_sha256"),required(r,"romver"),required(r,"model"),required(r,"mainboard"),required(r,"sha256"),required(r,"build"),required(r,"dvd_version"),required(r,"provenance")};
        const auto rom1=load_bios_rom1(read_file(path("rom1")),bios,*hardware,identity);
        supervisor->prepare_firmware(bios,nullptr,hardware,&nvm,nullptr,&rom1);
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
        auto& ee=supervisor->ee();
        auto& iop=const_cast<Iop&>(memory->iop());
        iop.set_pad_input_source([this](unsigned port,std::uint64_t cycle)->std::optional<Iop::PadInput>{
            if(port)return std::nullopt;
            std::lock_guard lock(p_->mutex);
            if(!p_->input.empty()){p_->sampled=p_->input.front();p_->input.pop_front();}
            ++p_->polls;p_->lastPoll=cycle;p_->lastButtons=p_->sampled.buttons;return p_->sampled;
        });
#ifdef _WIN32
        iop.set_pad_output_sink([this](unsigned port,std::uint8_t smallMotor,std::uint8_t largeMotor){
            if(port)return false;
            std::lock_guard outputLock(p_->padOutputMutex);
            Impl::SetState send{};DWORD index{};XINPUT_VIBRATION vibration{};
            {
                std::lock_guard lock(p_->mutex);send=p_->setState;index=p_->gamepadIndex;
                if(p_->focused){vibration.wLeftMotorSpeed=static_cast<WORD>(largeMotor*257u);vibration.wRightMotorSpeed=smallMotor?0xFFFFu:0u;}
                if(!send || index==0xFFFFFFFFu)return false;
                if(p_->rumbleLeft==vibration.wLeftMotorSpeed && p_->rumbleRight==vibration.wRightMotorSpeed)return true;
            }
            if(send(index,&vibration)!=ERROR_SUCCESS)return false;
            std::lock_guard lock(p_->mutex);
            if(index==p_->gamepadIndex){p_->rumbleLeft=vibration.wLeftMotorSpeed;p_->rumbleRight=vibration.wRightMotorSpeed;}
            return true;
        });
#endif
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
            if(!request.native_cache.empty())p_->status=tr("Native AOT with selective HLE. Missing translations and services stop explicitly; compatibility incomplete.");
            else {
#ifdef PS2N_NATIVE_ONLY
            p_->status=original?tr("Original BIOS boot with game disc — static AOT, software GS. Missing translations are reported explicitly."):QStringLiteral("Experimental — %1\n%2").arg(module?tr("Static AOT"):tr("Missing AOT"),qstr(required(config,"hypotheses")));
#else
            p_->status=original?tr("Original BIOS boot with game disc — offline AOT, shared console state. Execution stops when a required translation or device behavior is unavailable."):QStringLiteral("Experimental — %1\n%2").arg(module?QStringLiteral("AOT nativo"):QStringLiteral("AOT ausente — execução indisponível"),qstr(required(config,"hypotheses")));
#endif
            }
            if(!request.native_cache.empty() && cacheDiagnostic)
                p_->status+=QStringLiteral("\nDiagnostic cache-hit aliases: physical translation, unverified on hardware.");
            const auto map=config.value("gamepad_mapping").toArray();
            if(!map.isEmpty()){
                p_->customGamepadMapping=true;
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
            boundary=supervisor->run(module,options);
            {
                std::lock_guard lock(p_->mutex);p_->cycles=ee.cycles();p_->aot+=boundary.aot_instructions;p_->interpreted+=boundary.interpreted_instructions;
                p_->iopAot=iop.native_instructions();p_->iopInterpreted=iop.interpreted_instructions();
                p_->vu0Aot=memory->vu0_aot_stats().native_instructions;p_->vu0Interpreted=memory->vu0_aot_stats().interpreted_instructions;
                p_->vu1Aot=memory->vu1_aot_stats().native_instructions;p_->vu1Interpreted=memory->vu1_aot_stats().interpreted_instructions;
                if(selectedGpu){p_->gpuPrimitives=selectedGpu->stats().primitives;p_->gpuBatches=selectedGpu->stats().batches;}
                p_->nonzero+=nonzero;nonzero=0;
                if(!pcm.isEmpty() && p_->audioReady){p_->pcmBytes+=static_cast<std::size_t>(pcm.size());p_->pcm.push_back(std::move(pcm));pcm.clear();}else pcm.clear();
            }
            if(boundary.outcome==BootOutcome::BlockPoint && loaded && loaded->activate_overlays(*memory,boundary.pc,options))continue;
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
    const auto bit=keyBit(key),axis=axisKey(key);if(bit<0 && axis<0)return;
    std::lock_guard lock(p_->mutex);p_->focused=true;
    if(axis>=0)p_->axisKeys[static_cast<unsigned>(axis)]=down;
    if(bit>=0){if(down)p_->keys=static_cast<std::uint16_t>(p_->keys&~(1u<<bit));else p_->keys=static_cast<std::uint16_t>(p_->keys|(1u<<bit));}
    if(p_->state==SessionState::Running)p_->enqueueInput();
}
void ExperimentalSession::releaseKeys(){
#ifdef _WIN32
    std::lock_guard outputLock(p_->padOutputMutex);
    Impl::SetState send{};DWORD index{};
#endif
    {
        std::lock_guard lock(p_->mutex);p_->keys=p_->gamepad=0xFFFF;p_->axisKeys={};p_->gamepadInput={};p_->sampled={};p_->lastQueued={};
        p_->input.clear();p_->input.push_back({});p_->focused=false;
#ifdef _WIN32
        send=p_->setState;index=p_->gamepadIndex;p_->rumbleLeft=p_->rumbleRight=0u;
#endif
    }
#ifdef _WIN32
    if(send && index!=0xFFFFFFFFu){XINPUT_VIBRATION stopped{};(void)send(index,&stopped);}
#endif
}
void ExperimentalSession::focusInput(){std::lock_guard lock(p_->mutex);p_->focused=true;}
void ExperimentalSession::presented(){std::lock_guard lock(p_->mutex);++p_->painted;}
void ExperimentalSession::tick(){
    QImage frame;QByteArray packet;bool refresh=false;
    const bool audioBuffered=p_->audio.buffered();
#ifdef _WIN32
    // Device enumeration can block; never hold the execution/audio mutex here.
    XINPUT_STATE gamepad{};bool connected=false;DWORD activeIndex=0xFFFFFFFFu;
    const auto pollTime=GetTickCount64();
    if(p_->getState)for(DWORD i=0;i<4;++i) {
        if(p_->gamepadRetry.throttled(i,pollTime))continue;
        const auto result=p_->getState(i,&gamepad);
        const bool present=result==ERROR_SUCCESS;
        p_->gamepadRetry.observed(i,present,pollTime);
        if(present){connected=true;activeIndex=i;break;}
    }
    {
        std::lock_guard outputLock(p_->padOutputMutex);
        DWORD previous{};Impl::SetState send{};
        {
            std::lock_guard lock(p_->mutex);previous=p_->gamepadIndex;send=p_->setState;
            if(activeIndex!=previous){p_->gamepadIndex=activeIndex;p_->rumbleLeft=p_->rumbleRight=0u;}
        }
        if(activeIndex!=previous && previous!=0xFFFFFFFFu && send){XINPUT_VIBRATION stopped{};(void)send(previous,&stopped);}
    }
#endif
    {
        std::lock_guard lock(p_->mutex);
#ifdef _WIN32
        p_->gamepadStatus=connected?QStringLiteral("XInput conectado"):QStringLiteral("sem gamepad XInput");
        Iop::PadInput state{};
        if(connected && p_->focused) {
            for(unsigned i=0;i<16;++i)if(p_->mapping[i] && (gamepad.Gamepad.wButtons&p_->mapping[i]))state.buttons=static_cast<std::uint16_t>(state.buttons&~(1u<<i));
            state.lx=pad_axis_byte(gamepad.Gamepad.sThumbLX,XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            state.ly=pad_axis_byte(gamepad.Gamepad.sThumbLY,XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE,true);
            state.rx=pad_axis_byte(gamepad.Gamepad.sThumbRX,XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            state.ry=pad_axis_byte(gamepad.Gamepad.sThumbRY,XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE,true);
            if(!p_->customGamepadMapping) {
                state.pressureValid=(1u<<10u)|(1u<<11u);
                state.pressure[10]=gamepad.Gamepad.bLeftTrigger;state.pressure[11]=gamepad.Gamepad.bRightTrigger;
                if(gamepad.Gamepad.bLeftTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD)state.buttons=static_cast<std::uint16_t>(state.buttons&~(1u<<8u));
                if(gamepad.Gamepad.bRightTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD)state.buttons=static_cast<std::uint16_t>(state.buttons&~(1u<<9u));
            }
        }
        p_->gamepad=state.buttons;p_->gamepadInput=state;
        if(p_->state==SessionState::Running)p_->enqueueInput();
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
