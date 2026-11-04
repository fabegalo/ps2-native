#pragma once
#include <QTranslator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
namespace ps2launcher {
// The launcher's shared UI vocabulary; engine.retranslate() refreshes qsTr bindings.
class Translation final : public QTranslator {
public:
    Translation() {
        QFile file(QStringLiteral(":/i18n/pt_BR.json"));
        if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot open Portuguese translation catalog");
        QJsonParseError error;
        words_ = QJsonDocument::fromJson(file.readAll(), &error).object();
        if (error.error != QJsonParseError::NoError || words_.isEmpty()) qFatal("Invalid Portuguese translation catalog");
    }
    void setPortuguese(bool enabled) { portuguese_ = enabled; }
    bool isEmpty() const override { return false; }
    QString translate(const char*, const char* source, const char* = nullptr, int = -1) const override {
        if (!portuguese_ || !source) return {};
        const auto value = words_.value(QString::fromUtf8(source));
        return value.isString() ? value.toString() : QString{};
    }
private:
    QJsonObject words_;
    bool portuguese_{};
};
}
