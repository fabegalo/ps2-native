// Exercises the real Qt worker and shared EE/IOP/GS/SPU2 runtime. Original
// synthetic firmware/data only; no physical-hardware or commercial claims.
#include "experimental_session.hpp"
#include "qt_support.hpp"
#include "bios_program.hpp"
#include "iso_image.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/sha256.hpp"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QSettings>
#include <iostream>

using namespace ps2native;
using namespace ps2launcher;
namespace {
void require(bool ok,const std::string& message) { if(!ok)throw std::runtime_error(message); }
template<class Predicate> void until(Predicate done,const char* what) {
    QElapsedTimer timer;timer.start();
    while(!done()) {
        QCoreApplication::processEvents();QThread::msleep(2);
        require(timer.elapsed()<20000,std::string("timeout: ")+what);
    }
}
void json(const std::filesystem::path& path,const QJsonObject& object) {
    const auto bytes=QJsonDocument(object).toJson();
    write_file_atomically(path,std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),static_cast<std::size_t>(bytes.size())));
}
struct Fixture {
    std::filesystem::path root;
    BiosImage bios;NvmImage nvm;Rom1Image rom1;
    const ConsoleHardware* hardware=find_console_hardware("SCPH-70004:GH-032-11");
    CheckpointContext context;
    QJsonObject config;
    void prepare(EEState& ee,Memory& memory) {
        prepare_ee_boot(bios,nullptr,ee,memory,hardware,&nvm,nullptr,&rom1);
        memory.mount_disc(std::make_shared<DiscImage>(root/"disc.iso"),Iop::DiscMedium::Ps2Cd,true);
    }
    explicit Fixture(const std::filesystem::path& directory):root(directory) {
        auto raw=m20::build_firmware().rom;
        const auto ver=std::search(raw.begin(),raw.end(),std::begin(m20::kRomver),std::end(m20::kRomver)-1);
        require(ver!=raw.end(),"synthetic ROMVER");std::copy(hardware->romver.begin(),hardware->romver.end(),ver);
        m20::Assembler a(kEeResetVector);
        const auto sh=[&](unsigned address,unsigned value){a.la(8,address);a.la(9,value);a.sh(9,0,8);};
        const auto sw=[&](unsigned address,unsigned value){a.la(8,address);a.la(9,value);a.sw(9,0,8);};
        sh(0xBF90019A,0);sh(0xBF90059A,0);
        const unsigned base=0xBF900400;
        sh(base+0x1A8,0);sh(base+0x1AA,0x2800);
        sh(base+0x1AC,0x0404);for(unsigned i=0;i<7;++i)sh(base+0x1AC,0x7777);
        sh(base+0x1AC,0x0304);for(unsigned i=0;i<7;++i)sh(base+0x1AC,0x9999);
        sh(base+0x19A,0x8010);for(unsigned i=0;i<40;++i)a.nop();sh(base+0x19A,0x8000);
        for(unsigned reg=0x180;reg<0x198;reg+=2)sh(base+reg,(reg==0x188 || reg==0x190)?1:0);
        sh(base+0x198,0xC00);sh(0xBF900788,0x3FFF);sh(0xBF90078A,0x3FFF);
        sh(base,0x3FFF);sh(base+2,0x3FFF);sh(base+4,0x1000);sh(base+6,0xF);sh(base+8,0x1FC0);
        sh(base+0x1C0,0);sh(base+0x1C2,0x2800);sh(base+0x1A0,1);
        sw(0xBF801070,0);sw(0xBF801078,0);
        a.label("poll");sw(0xBF808268,0x3BC);sw(0xBF808200,(5u<<18)|(5u<<8)|0x40);
        a.la(8,0xBF808260);
        for(unsigned i=0;i<5;++i){a.la(9,i==0?1:i==1?0x42:0);a.sb(9,0,8);}
        sw(0xBF808204,0);sw(0xBF808268,0x3B1);
        for(unsigned i=0;i<80;++i)a.nop();
        a.la(8,0xBF808264);a.la(10,0xA0000300);
        for(unsigned i=0;i<5;++i){a.lbu(9,0,8);a.nop();a.sb(9,i,10);}
        sw(0xBF808280,1);a.beq(0,0,"poll");a.nop();
        const auto code=m20::le_words(a.finish());require(code.size()<m20::kResetSize,"synthetic code fits RESET");
        std::copy(code.begin(),code.end(),raw.begin());bios=load_bios_rom0(raw);write_file_atomically(root/"rom0.bin",raw);
        std::vector<std::uint8_t> nv(kNvmBytes);
        const auto& layout=nvm_layout_info(hardware->mechacon->layout);
        std::copy(hardware->model.begin(),hardware->model.end(),nv.begin()+layout.model_name);
        std::copy(hardware->mechacon->model_id.begin(),hardware->mechacon->model_id.end(),nv.begin()+layout.ilink_id+1);
        std::copy(hardware->mechacon->con_model_id.begin(),hardware->mechacon->con_model_id.end(),nv.begin()+layout.console_id);
        nvm=load_nvm_image(nv,*hardware);write_file_atomically(root/"nvm.bin",nv);
        std::vector<std::uint8_t> companion(256);
        const char* names[]={"RESET","ROMDIR","EXTINFO","DVDVERE"};const unsigned sizes[]={16,80,12,6};
        for(unsigned i=0;i<4;++i){const auto at=16+i*16;std::copy_n(names[i],std::char_traits<char>::length(names[i]),companion.begin()+at);
            companion[at+10]=i==1?12:0;for(unsigned j=0;j<4;++j)companion[at+12+j]=static_cast<std::uint8_t>(sizes[i]>>(8*j));}
        companion[98]=8;companion[99]=3;
        const std::string build="fixture",version="0.01E\n";
        std::copy(build.begin(),build.end(),companion.begin()+100);std::copy(version.begin(),version.end(),companion.begin()+112);
        Rom1Identity identity{bios.sha256,bios.version.romver,std::string(hardware->model),std::string(hardware->mainboard),sha256_hex(companion),"fixture","0.01E","original synthetic test pairing"};
        rom1=load_bios_rom1(companion,bios,*hardware,identity);write_file_atomically(root/"rom1.bin",companion);
        const auto disc=iso_test::build_iso({});write_file_atomically(root/"disc.iso",disc);
        context.scenario_sha256=sha256_hex(raw);context.expected_disc_sha256=sha256_hex(disc);
        auto memory=std::make_unique<Memory>();EEState ee;
        DiagnosticAssumptions assumptions;assumptions.spu2_quiescent_reset=true;assumptions.spu2_pio_word_cycles=1;
        prepare_ee_boot(bios,nullptr,ee,*memory,hardware,&nvm,&assumptions,&rom1);
        memory->mount_disc(std::make_shared<DiscImage>(root/"disc.iso"),Iop::DiscMedium::Ps2Cd,true);
        ee.pc=0x80010000;memory->write32(ee.pc,0x1000FFFF);memory->write32(ee.pc+4,0);
        auto& iop=const_cast<Iop&>(memory->iop());iop.diagnostic_sio2_digital_pad(0,0xFFFF,32);
        auto& gs=const_cast<GsRegisters&>(memory->gs());
        gs.write(kGsSmode1,8,kSmode1Sint|(2ull<<kSmode1CmodShift),0,{});
        gs.write(kGsSyncv,8,2u|(2ull<<42),0,{});
        gs.write(kGsPmode,8,0xFF25,0,{});gs.write(kGsSmode2,8,3,0,{});
        gs.write(kGsDispfb1,8,1u<<9,0,{});gs.write(kGsDisplay1,8,(3ull<<32)|(7ull<<44),0,{});
        gs.general_write(0x50,1ull<<48);gs.general_write(0x51,0);gs.general_write(0x52,4u|(4ull<<32));gs.general_write(0x53,0);
        for(unsigned i=0;i<8;++i)gs.general_write(0x54,0x8040201080402010ull);
        gs.write(kGsSmode1,8,2ull<<kSmode1CmodShift,0,{});
        BootReport boundary;boundary.outcome=BootOutcome::BudgetExhausted;boundary.pc=ee.pc;
        save_checkpoint(root/"start.ps2ncp",ee,*memory,boundary,context);
        config={{"format",1},{"rom0","rom0.bin"},{"rom0_sha256",qstr(bios.sha256)},
            {"console","SCPH-70004:GH-032-11"},{"nvm","nvm.bin"},{"nvm_sha256",qstr(nvm.sha256)},
            {"rom1","rom1.bin"},{"rom1_identity",QJsonObject{{"rom0_sha256",qstr(bios.sha256)},{"romver",qstr(bios.version.romver)},
                {"model",qstr(identity.model)},{"mainboard",qstr(identity.mainboard)},{"sha256",qstr(identity.sha256)},
                {"build","fixture"},{"dvd_version","0.01E"},{"provenance","original synthetic test pairing"}}},
            {"disc","disc.iso"},{"disc_sha256",qstr(context.expected_disc_sha256)},{"disc_medium","ps2-cd"},
            {"checkpoint","start.ps2ncp"},{"checkpoint_sha256",qstr(sha256_file(root/"start.ps2ncp"))},
            {"scenario_sha256",qstr(context.scenario_sha256)},{"hypotheses","Synthetic GS timing, digital pad and quiescent SPU2; no Sony code"}};
        json(root/"session.json",config);
    }
};
}
int main(int argc,char** argv)try {
    QGuiApplication app(argc,argv);QTemporaryDir temporary;
    app.setOrganizationName("ps2n-session-test");app.setApplicationName("session-test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    require(temporary.isValid(),"temporary test directory");Fixture fixture(fspath(temporary.path()));
    ExperimentalSession session;QImage observed;
    QObject::connect(&session,&ExperimentalSession::frameReady,&session,[&](const QImage& f){observed=f;});
    session.setManifest(qpath(fixture.root/"session.json"));session.begin(false);
    until([&]{return session.state()==frontend::SessionState::Running || !session.active();},"start");
    require(session.active(),session.last_error());
    until([&]{return !observed.isNull() || !session.active();},"GS presentation");
    require(observed.size()==QSize(4,4) && observed.pixelColor(0,0)==QColor(0x10,0x20,0x40),"real GS RGBA reaches Qt");
    session.key(Qt::Key_Z,true);
    // Allow real polls to consume the held button, then acknowledge pause.
    QElapsedTimer inputTime;inputTime.start();
    until([&]{return inputTime.elapsed()>100;},"input polls");session.pause();
    until([&]{return session.paused() || !session.active();},"pause");require(session.paused(),session.last_error());
    const auto saved=fixture.root/"paused.ps2ncp";session.save(QUrl::fromLocalFile(qpath(saved)));
    until([&]{return session.status().contains("seguro salvo") || session.status().contains("Falha");},"safe save");
    require(session.status().contains("seguro salvo"),utf8(session.status()));
    auto resumed=std::make_unique<Memory>();EEState state;fixture.prepare(state,*resumed);
    auto live=fixture.context;live.scenario_sha256=inspect_checkpoint(saved).scenario_sha256;
    (void)load_checkpoint(saved,state,*resumed,live);
    require(resumed->iop().ee_read(0x304,1)==0xBF,"keyboard Cross reaches real guest SIO2 reply RAM");
    const auto frozen=checkpoint_state_sha256(state,*resumed);
    session.save(QUrl::fromLocalFile(qpath(fixture.root/"still-paused.ps2ncp")));
    until([&]{return session.status().contains("still-paused");},"second save");
    require(inspect_checkpoint(fixture.root/"still-paused.ps2ncp").payload_sha256==inspect_checkpoint(saved).payload_sha256,"pause/save never advances guest state");
    // Reference dispatcher, including DMA/IRQ/PCM, versus 100k worker slices.
    std::vector<Iop::Spu2Sample> pcm[2];std::vector<std::string> frames[2];std::string states[2];
    for(unsigned run=0;run<2;++run) {
        (void)load_checkpoint(saved,state,*resumed,live);
        const_cast<Iop&>(resumed->iop()).set_spu2_sample_sink([&](unsigned c,const auto& s){if(c==1)pcm[run].push_back(s);});
        BootOptions options;options.tail_instructions=0;options.max_trace_events=1;
        options.copy_device_histories=run==0;
        options.max_instructions=run?100000:300000;
        options.on_frame=[&](const Framebuffer& f){frames[run].push_back(sha256_hex(f.rgba));};
        for(unsigned n=0;n<(run?3u:1u);++n)require(run_ee_boot(state,*resumed,nullptr,options).outcome==BootOutcome::BudgetExhausted,"reference execution");
        states[run]=checkpoint_state_sha256(state,*resumed);
    }
    require(states[0]==states[1] && states[0]!=frozen,"worker slicing preserves complete state, cycles, DMA and IRQ");
    require(pcm[0].size()==pcm[1].size() && std::equal(pcm[0].begin(),pcm[0].end(),pcm[1].begin(),[](const auto& a,const auto& b){return a.cycle==b.cycle && a.left==b.left && a.right==b.right;}) && !pcm[0].empty() && std::any_of(pcm[0].begin(),pcm[0].end(),[](const auto& s){return s.left||s.right;}),"identical live synthesized PCM across slice boundaries");
    require(frames[0]==frames[1] && !frames[0].empty(),"identical real VBLANK RGBA across slice boundaries");
    session.resume();until([&]{return !session.paused();},"resume");session.stop();
    until([&]{return !session.active();},"stop");require(session.last_error().empty(),session.last_error());
    auto bad=fixture.config;bad["scenario_sha256"]=QString(64,'0');json(fixture.root/"bad.json",bad);
    session.setManifest(qpath(fixture.root/"bad.json"));session.begin(false);until([&]{return !session.active();},"identity refusal");
    require(session.last_error().find("scenario identity")!=std::string::npos,"identity mismatch fails visibly");
    session.setManifest(qpath(fixture.root/"session.json"));session.begin(false);session.stop();
    until([&]{return !session.active();},"stop during validation");
    std::cout<<"Experimental Qt runtime lifecycle, input, GS, PCM and complete-state equivalence passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
