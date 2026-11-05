#pragma once
#include "ps2native/frontend/runtime.hpp"
#include <QObject>
#include <QImage>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QUrl>
#include <memory>
namespace ps2launcher {
class ExperimentalSession final:public QObject,public ps2native::frontend::RuntimeSession {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString metrics READ metrics NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(QString manifest READ manifest WRITE setManifest NOTIFY manifestChanged)
public:
    explicit ExperimentalSession(QObject* parent=nullptr);
    ~ExperimentalSession() override;
    void start(const ps2native::frontend::SessionRequest&) override;
    Q_INVOKABLE void stop() override;
    ps2native::frontend::SessionState state() const override;
    std::string last_error() const override;
    QString status() const;
    QString metrics() const;
    bool active() const;
    bool paused() const;
    QString manifest() const;
    void setManifest(const QString&);
    Q_INVOKABLE void selectManifest(const QUrl&);
    Q_INVOKABLE void begin(bool reviewedRuntimeChange);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void save(const QUrl&);
    void key(int key,bool down);
    void releaseKeys();
    void focusInput();
    void presented();
signals:
    void changed();
    void manifestChanged();
    void frameReady(const QImage&);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    QTimer timer_;
    void tick();
};
class GameView:public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(ps2launcher::ExperimentalSession* session READ session WRITE setSession)
    Q_PROPERTY(bool nativePixels READ nativePixels WRITE setNativePixels)
public:
    explicit GameView(QQuickItem* parent=nullptr);
    ExperimentalSession* session()const{return session_;}
    void setSession(ExperimentalSession*);
    void paint(QPainter*) override;
    bool nativePixels() const { return nativePixels_; }
    void setNativePixels(bool value) { nativePixels_=value; update(); }
protected:
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void focusInEvent(QFocusEvent*) override;
private:
    ExperimentalSession* session_{};
    QImage frame_;
    bool pending_{};
    bool nativePixels_{};
};
}
