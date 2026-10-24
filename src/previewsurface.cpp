// SPDX-License-Identifier: AGPL-3.0-only

#include "previewsurface.h"

#include <QMimeType>
#include <QPainter>
#include <QStyle>
#include <algorithm>

PreviewSurface::PreviewSurface(const QMimeType &mime, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("previewSurface"));
    setProperty("mimeType", mime.name());
    icon_ = QIcon::fromTheme(
        mime.iconName(),
        QIcon::fromTheme(mime.genericIconName(), style()->standardIcon(QStyle::SP_FileIcon)));
    auto colors = palette();
    colors.setColor(QPalette::Window, QColor(16, 17, 20));
    colors.setColor(QPalette::WindowText, QColor(230, 232, 236));
    setPalette(colors);
}

void PreviewSurface::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    const int size = std::max(0, std::min({256, width() * 2 / 3, height() / 3}));
    if (!size)
    {
        return;
    }
    QPixmap watermark(QSize(size, size) * devicePixelRatioF());
    watermark.setDevicePixelRatio(devicePixelRatioF());
    watermark.fill(Qt::transparent);
    QPainter iconPainter(&watermark);
    const auto glyph = icon_.pixmap(QSize(size, size), devicePixelRatioF());
    iconPainter.drawPixmap(QRect(0, 0, size, size), glyph, glyph.rect());
    iconPainter.end();
    // Keep the type emblem above centered controls or messages so it stays visible.
    painter.setOpacity(0.22);
    painter.drawPixmap((width() - size) / 2, height() / 4 - size / 2, watermark);
}
