#pragma once
// "image://icon/<name>/<RRGGBB>": line icons drawn for the launcher (original
// artwork, BSD-3-Clause like the rest of the project), rendered with Qt SVG at
// the requested size so they stay sharp at any scale and work with every
// Qt Quick scene graph backend.
#include <QQuickImageProvider>

namespace ps2launcher {
class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
} // namespace ps2launcher
