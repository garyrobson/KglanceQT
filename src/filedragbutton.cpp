// SPDX-License-Identifier: AGPL-3.0-only

#include "filedragbutton.h"

#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>

FileDragButton::FileDragButton(QWidget *parent) : QToolButton(parent)
{
    setObjectName(QStringLiteral("dragCurrentFile"));
    setText(tr("Drag file"));
    setAccessibleName(tr("Drag current file"));
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setAutoRaise(true);
    setFile({});
}

void FileDragButton::setFile(const QUrl &url, const QIcon &icon)
{
    pressed_ = false;
    setDown(false);
    url_ = url.isLocalFile() && QDir::isAbsolutePath(url.toLocalFile()) ? url : QUrl{};
    setEnabled(!url_.isEmpty());
    setCursor(isEnabled() ? Qt::OpenHandCursor : Qt::ArrowCursor);
    setIcon(icon.isNull() ? QIcon::fromTheme(QStringLiteral("document-export")) : icon);
    setToolTip(isEnabled() ? tr("Drag %1 into another application").arg(url_.fileName())
                           : tr("Drag the current file into another application"));
}

void FileDragButton::mousePressEvent(QMouseEvent *event)
{
    pressed_ = isEnabled() && event->button() == Qt::LeftButton;
    pressPosition_ = event->position().toPoint();
    QToolButton::mousePressEvent(event);
}

void FileDragButton::mouseMoveEvent(QMouseEvent *event)
{
    if (!pressed_ || !(event->buttons() & Qt::LeftButton) || url_.isEmpty() ||
        (event->position().toPoint() - pressPosition_).manhattanLength() <
            QApplication::startDragDistance())
    {
        QToolButton::mouseMoveEvent(event);
        return;
    }
    pressed_ = false;
    setDown(false);
    QPointer<QDrag> drag = new QDrag(this);
    auto *data = new QMimeData;
    data->setUrls({url_});
    drag->setMimeData(data);
    const auto pixmap = icon().pixmap(48, 48);
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
    // Offering Copy only prevents a drop target from requesting source deletion.
    drag->exec(Qt::CopyAction, Qt::CopyAction);
    if (drag)
        drag->deleteLater();
}

void FileDragButton::mouseReleaseEvent(QMouseEvent *event)
{
    pressed_ = false;
    QToolButton::mouseReleaseEvent(event);
}
