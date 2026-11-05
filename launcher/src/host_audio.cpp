#include "host_audio.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <array>
#elif defined(PS2N_QT_AUDIO)
#include <QAudioSink>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QIODevice>
#include <QMediaDevices>
#endif
namespace ps2launcher {
struct HostAudio::Impl {
    QString error,description;
#ifdef _WIN32
    HWAVEOUT output{};
    struct Buffer { WAVEHDR header{}; QByteArray bytes; bool prepared{}; };
    std::array<Buffer,8> buffers;
#elif defined(PS2N_QT_AUDIO)
    std::unique_ptr<QAudioSink> output;
    QIODevice* device{};
#endif
};
HostAudio::HostAudio():impl_(std::make_unique<Impl>()){}
HostAudio::~HostAudio(){reset();}
void HostAudio::pause(){
#ifdef _WIN32
    if(impl_->output)waveOutPause(impl_->output);
#elif defined(PS2N_QT_AUDIO)
    if(impl_->output)impl_->output->suspend();
#endif
}
void HostAudio::resume(){
#ifdef _WIN32
    if(impl_->output)waveOutRestart(impl_->output);
#elif defined(PS2N_QT_AUDIO)
    if(impl_->output)impl_->output->resume();
#endif
}
void HostAudio::reset(){
#ifdef _WIN32
    if(impl_->output){
        waveOutReset(impl_->output);
        for(auto& b:impl_->buffers)if(b.prepared){waveOutUnprepareHeader(impl_->output,&b.header,sizeof(b.header));b.prepared=false;}
        waveOutClose(impl_->output);impl_->output=nullptr;
    }
#elif defined(PS2N_QT_AUDIO)
    if(impl_->output)impl_->output->reset();
    impl_->output.reset();impl_->device=nullptr;
#endif
}
bool HostAudio::open(){
    reset();impl_->error.clear();
#ifdef _WIN32
    WAVEFORMATEX format{WAVE_FORMAT_PCM,2,48000,192000,4,16,0};
    const auto result=waveOutOpen(&impl_->output,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);
    if(result!=MMSYSERR_NOERROR){impl_->error=QStringLiteral("waveOutOpen: %1").arg(result);return false;}
    impl_->description=QStringLiteral("Windows default output, PCM 48000 Hz stereo / 16 bit");return true;
#elif defined(PS2N_QT_AUDIO)
    QAudioFormat format;format.setSampleRate(48000);format.setChannelCount(2);format.setSampleFormat(QAudioFormat::Int16);
    const auto device=QMediaDevices::defaultAudioOutput();
    if(device.isNull() || !device.isFormatSupported(format)){impl_->error=QStringLiteral("Host does not support 48000 Hz stereo Int16 PCM");return false;}
    impl_->output=std::make_unique<QAudioSink>(device,format);impl_->output->setBufferSize(19200);
    impl_->device=impl_->output->start();
    if(!impl_->device){impl_->error=QStringLiteral("QAudioSink could not start");return false;}
    impl_->description=device.description();return true;
#else
    impl_->error=QStringLiteral("Host audio unavailable: build with Qt Multimedia");return false;
#endif
}
bool HostAudio::write(const QByteArray& pcm){
#ifdef _WIN32
    if(!impl_->output)return false;
    for(auto& b:impl_->buffers){
        if(b.prepared && !(b.header.dwFlags&WHDR_DONE))continue;
        if(b.prepared){waveOutUnprepareHeader(impl_->output,&b.header,sizeof(b.header));b.prepared=false;}
        b.bytes=pcm;b.header={};b.header.lpData=b.bytes.data();b.header.dwBufferLength=static_cast<DWORD>(pcm.size());
        auto result=waveOutPrepareHeader(impl_->output,&b.header,sizeof(b.header));
        if(result==MMSYSERR_NOERROR){b.prepared=true;result=waveOutWrite(impl_->output,&b.header,sizeof(b.header));}
        if(result!=MMSYSERR_NOERROR){impl_->error=QStringLiteral("waveOut PCM submit: %1").arg(result);return false;}
        return true;
    }
    return false;
#elif defined(PS2N_QT_AUDIO)
    if(!impl_->output || impl_->output->bytesFree()<pcm.size())return false;
    if(impl_->output->error()!=QAudio::NoError && impl_->output->error()!=QAudio::UnderrunError){impl_->error=QStringLiteral("QAudioSink device error");return false;}
    const auto written=impl_->device->write(pcm);
    if(written!=pcm.size()){impl_->error=QStringLiteral("QAudioSink short PCM write");return false;}return true;
#else
    Q_UNUSED(pcm);return false;
#endif
}
bool HostAudio::buffered()const{
#ifdef _WIN32
    for(const auto& b:impl_->buffers)if(b.prepared && !(b.header.dwFlags&WHDR_DONE))return true;
    return false;
#elif defined(PS2N_QT_AUDIO)
    return impl_->output && impl_->output->state()==QAudio::ActiveState;
#else
    return false;
#endif
}
QString HostAudio::description()const{return impl_->description;}
QString HostAudio::error()const{return impl_->error;}
}
