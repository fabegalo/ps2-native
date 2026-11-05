#pragma once
// Host transport for the existing SPU2 PCM, never a synthesizer or resampler.
#include <QByteArray>
#include <QString>
#include <memory>
namespace ps2launcher {
class HostAudio {
public:
    HostAudio();
    ~HostAudio();
    bool open();
    bool write(const QByteArray& pcm);
    bool buffered() const;
    void reset();
    void pause();
    void resume();
    QString description() const;
    QString error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
