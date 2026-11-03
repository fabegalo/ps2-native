#pragma once
// Conversions between Qt and the launcher core. Paths cross as UTF-8, which
// both sides treat losslessly (UTF-16 on Windows, bytes elsewhere).
#include "ps2native/frontend/library.hpp"
#include "ps2native/io.hpp"

#include <QDir>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ps2launcher {
inline QString qstr(const std::string& text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }
inline QString qstr(std::string_view text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }
inline QString qpath(const std::filesystem::path& path) {
    return path.empty() ? QString() : QDir::toNativeSeparators(qstr(ps2native::path_utf8(path)));
}
inline std::filesystem::path fspath(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    return ps2native::frontend::path_from_utf8(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}
inline std::string utf8(const QString& text) { return text.toStdString(); }
// file: URLs from dialogs and drops; plain paths are accepted too.
inline QString local_path(const QUrl& url) {
    if (url.isLocalFile()) return QDir::toNativeSeparators(url.toLocalFile());
    return url.scheme().isEmpty() ? QDir::toNativeSeparators(url.toString()) : QString();
}
inline QVariantMap qmap(const std::map<std::string, std::string>& values) {
    QVariantMap out;
    for (const auto& [key, value] : values) out.insert(qstr(key), qstr(value));
    return out;
}
inline QStringList qlist(const std::vector<std::string>& values) {
    QStringList out;
    out.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& value : values) out.push_back(qstr(value));
    return out;
}
QString human_size(std::uint64_t bytes);
} // namespace ps2launcher
