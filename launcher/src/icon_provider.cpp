#include "icon_provider.hpp"

#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace ps2launcher {
namespace {
// 24x24 stroke geometry; stroke colour and width are applied by the renderer.
const QHash<QString, QString>& icons() {
    static const QHash<QString, QString> map{
        {QStringLiteral("home"), QStringLiteral(R"(<path d="M3.5 10.5 12 3.5l8.5 7"/><path d="M5.5 9v10.5a1 1 0 0 0 1 1H10v-6h4v6h3.5a1 1 0 0 0 1-1V9"/>)")},
        {QStringLiteral("library"), QStringLiteral(R"(<rect x="3.5" y="3.5" width="7" height="7" rx="1.6"/><rect x="13.5" y="3.5" width="7" height="7" rx="1.6"/><rect x="3.5" y="13.5" width="7" height="7" rx="1.6"/><rect x="13.5" y="13.5" width="7" height="7" rx="1.6"/>)")},
        {QStringLiteral("chip"), QStringLiteral(R"(<rect x="6" y="6" width="12" height="12" rx="2"/><rect x="9.5" y="9.5" width="5" height="5" rx="0.8"/><path d="M9 2.5V6M15 2.5V6M9 18v3.5M15 18v3.5M2.5 9H6M2.5 15H6M18 9h3.5M18 15h3.5"/>)")},
        {QStringLiteral("settings"), QStringLiteral(R"(<path d="M4 6.5h9M18 6.5h2M4 12h3M11 12h9M4 17.5h11M19 17.5h1"/><circle cx="15.5" cy="6.5" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="17.5" r="2"/>)")},
        {QStringLiteral("info"), QStringLiteral(R"(<circle cx="12" cy="12" r="8.5"/><path d="M12 11v5.5"/><path d="M12 7.6v.1"/>)")},
        {QStringLiteral("search"), QStringLiteral(R"(<circle cx="11" cy="11" r="6.5"/><path d="m20 20-4.3-4.3"/>)")},
        {QStringLiteral("plus"), QStringLiteral(R"(<path d="M12 5v14M5 12h14"/>)")},
        {QStringLiteral("folder"), QStringLiteral(R"(<path d="M3.5 7.5a2 2 0 0 1 2-2h3.8l2 2h7.2a2 2 0 0 1 2 2v7.5a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2z"/>)")},
        {QStringLiteral("disc"), QStringLiteral(R"(<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="2.4"/><path d="M16.2 7.8a6 6 0 0 1 1.7 3.2"/>)")},
        {QStringLiteral("play"), QStringLiteral(R"(<path d="M8 5.8v12.4a.8.8 0 0 0 1.2.7l9.6-6.2a.8.8 0 0 0 0-1.4L9.2 5.1A.8.8 0 0 0 8 5.8z"/>)")},
        {QStringLiteral("terminal"), QStringLiteral(R"(<rect x="3" y="4.5" width="18" height="15" rx="2"/><path d="m7 9.5 3 2.5-3 2.5M12.5 15h4.5"/>)")},
        {QStringLiteral("check"), QStringLiteral(R"(<path d="m5 12.5 4.5 4.5L19 7.5"/>)")},
        {QStringLiteral("check-circle"), QStringLiteral(R"(<circle cx="12" cy="12" r="8.5"/><path d="m8.2 12.3 2.6 2.6 5-5.2"/>)")},
        {QStringLiteral("alert"), QStringLiteral(R"(<path d="M10.3 4.4 2.9 17.5a2 2 0 0 0 1.7 3h14.8a2 2 0 0 0 1.7-3L13.7 4.4a2 2 0 0 0-3.4 0z"/><path d="M12 9.5v4.5M12 17.2v.1"/>)")},
        {QStringLiteral("x"), QStringLiteral(R"(<path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/>)")},
        {QStringLiteral("x-circle"), QStringLiteral(R"(<circle cx="12" cy="12" r="8.5"/><path d="m9 9 6 6M15 9l-6 6"/>)")},
        {QStringLiteral("refresh"), QStringLiteral(R"(<path d="M19.5 11A7.5 7.5 0 0 0 6.2 7.2L4.5 9"/><path d="M4.5 4.5V9H9"/><path d="M4.5 13a7.5 7.5 0 0 0 13.3 3.8l1.7-1.8"/><path d="M19.5 19.5V15H15"/>)")},
        {QStringLiteral("trash"), QStringLiteral(R"(<path d="M4.5 7h15M10 11v6M14 11v6"/><path d="M6.5 7l.8 12a1.5 1.5 0 0 0 1.5 1.4h6.4a1.5 1.5 0 0 0 1.5-1.4l.8-12"/><path d="M9.5 7V4.5h5V7"/>)")},
        {QStringLiteral("star"), QStringLiteral(R"(<path d="m12 3.8 2.5 5.1 5.6.8-4 3.9 1 5.6-5.1-2.7-5.1 2.7 1-5.6-4-3.9 5.6-.8z"/>)")},
        {QStringLiteral("memory"), QStringLiteral(R"(<path d="M7 3.5h7.5l3.5 3.5v12a1.5 1.5 0 0 1-1.5 1.5h-9.5A1.5 1.5 0 0 1 5.5 19V5A1.5 1.5 0 0 1 7 3.5z"/><path d="M9 8.5h3M9 16.5h6"/>)")},
        {QStringLiteral("gamepad"), QStringLiteral(R"(<path d="M7.5 8h9a4.5 4.5 0 0 1 4.4 3.6l.7 3.7a2.6 2.6 0 0 1-4.6 2.1L15.5 16h-7L7 17.4a2.6 2.6 0 0 1-4.6-2.1l.7-3.7A4.5 4.5 0 0 1 7.5 8z"/><path d="M8 10.8v3M6.5 12.3h3"/><path d="M15.5 11.5h.1M17.5 13.2h.1"/>)")},
        {QStringLiteral("speaker"), QStringLiteral(R"(<path d="M4 9.5h3.5L12 5.5v13l-4.5-4H4z"/><path d="M15.5 9.2a4 4 0 0 1 0 5.6M18 6.8a7.5 7.5 0 0 1 0 10.4"/>)")},
        {QStringLiteral("monitor"), QStringLiteral(R"(<rect x="3" y="4.5" width="18" height="12" rx="2"/><path d="M8.5 20h7M12 16.5V20"/>)")},
        {QStringLiteral("sparkles"), QStringLiteral(R"(<path d="M10 4.5 11.4 9l4.6 1.5-4.6 1.4L10 16.5 8.6 12 4 10.5 8.6 9z"/><path d="M17.5 14.5l.7 2 2 .7-2 .7-.7 2-.7-2-2-.7 2-.7z"/>)")},
        {QStringLiteral("external"), QStringLiteral(R"(<path d="M14 4h6v6M20 4l-8.5 8.5"/><path d="M18 14v4.5a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 4 18.5v-11A1.5 1.5 0 0 1 5.5 6H10"/>)")},
        {QStringLiteral("copy"), QStringLiteral(R"(<rect x="8.5" y="8.5" width="11.5" height="11.5" rx="2"/><path d="M15.5 8.5V5.5a1.5 1.5 0 0 0-1.5-1.5H5.5A1.5 1.5 0 0 0 4 5.5V14a1.5 1.5 0 0 0 1.5 1.5h3"/>)")},
        {QStringLiteral("stop"), QStringLiteral(R"(<rect x="6.5" y="6.5" width="11" height="11" rx="2"/>)")},
        {QStringLiteral("file"), QStringLiteral(R"(<path d="M13.5 3.5H7a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V9z"/><path d="M13.5 3.5V9H19"/>)")},
        {QStringLiteral("list"), QStringLiteral(R"(<path d="M9 6.5h11M9 12h11M9 17.5h11M4.5 6.5h.1M4.5 12h.1M4.5 17.5h.1"/>)")},
        {QStringLiteral("grid"), QStringLiteral(R"(<rect x="4" y="4" width="6.5" height="6.5" rx="1.2"/><rect x="13.5" y="4" width="6.5" height="6.5" rx="1.2"/><rect x="4" y="13.5" width="6.5" height="6.5" rx="1.2"/><rect x="13.5" y="13.5" width="6.5" height="6.5" rx="1.2"/>)")},
        {QStringLiteral("shield"), QStringLiteral(R"(<path d="M12 3.5 5 6.2v5.3c0 4.3 3 8 7 9 4-1 7-4.7 7-9V6.2z"/><path d="m9 12 2.2 2.2L15.2 10"/>)")},
        {QStringLiteral("chevron-right"), QStringLiteral(R"(<path d="m9.5 6 6 6-6 6"/>)")},
        {QStringLiteral("chevron-down"), QStringLiteral(R"(<path d="m6 9.5 6 6 6-6"/>)")},
        {QStringLiteral("lock"), QStringLiteral(R"(<rect x="5" y="10.5" width="14" height="10" rx="2"/><path d="M8.5 10.5V7.5a3.5 3.5 0 0 1 7 0v3"/>)")},
        {QStringLiteral("download"), QStringLiteral(R"(<path d="M12 4v11M7.5 10.5 12 15l4.5-4.5M5 19.5h14"/>)")},
        {QStringLiteral("key"), QStringLiteral(R"(<circle cx="8" cy="15.5" r="4"/><path d="m11 12.5 8.5-8.5M16.5 7l2.5 2.5M14.5 9l2 2"/>)")},
        {QStringLiteral("clock"), QStringLiteral(R"(<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>)")},
        {QStringLiteral("globe"), QStringLiteral(R"(<circle cx="12" cy="12" r="8.5"/><path d="M3.5 12h17M12 3.5c2.3 2.4 3.5 5.2 3.5 8.5s-1.2 6.1-3.5 8.5c-2.3-2.4-3.5-5.2-3.5-8.5S9.7 5.9 12 3.5z"/>)")},
        {QStringLiteral("layers"), QStringLiteral(R"(<path d="M12 3.5 3.5 8 12 12.5 20.5 8z"/><path d="m3.5 12 8.5 4.5 8.5-4.5M3.5 16l8.5 4.5 8.5-4.5"/>)")},
        {QStringLiteral("upload"), QStringLiteral(R"(<path d="M12 15V4M7.5 8.5 12 4l4.5 4.5M5 19.5h14"/>)")},
        {QStringLiteral("eye"), QStringLiteral(R"(<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="2.8"/>)")},
    };
    return map;
}
} // namespace

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const auto parts = id.split(QLatin1Char('/'));
    const QString name = parts.value(0);
    QString color = parts.value(1, QStringLiteral("ffffff"));
    if (!color.startsWith(QLatin1Char('#'))) color.prepend(QLatin1Char('#'));
    const double stroke = parts.size() > 2 ? parts.value(2).toDouble() : 1.75;
    const QSize target = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(48, 48);
    if (size) *size = target;
    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const auto body = icons().value(name, icons().value(QStringLiteral("info")));
    const QString svg = QStringLiteral(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="%1" stroke-width="%2" stroke-linecap="round" stroke-linejoin="round">%3</svg>)")
                            .arg(color, QString::number(stroke > 0 ? stroke : 1.75), body);
    QSvgRenderer renderer(svg.toUtf8());
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(target)));
    return image;
}
} // namespace ps2launcher
