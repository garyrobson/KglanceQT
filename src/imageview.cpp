// SPDX-License-Identifier: AGPL-3.0-only

#include "imageview.h"

#include <QGraphicsPixmapItem>
#include <QTimer>
#include <QWheelEvent>

ImageView::ImageView(const QImage &image, QWidget *parent) : QGraphicsView(parent), scene_(this)
{
    setScene(&scene_);
    scene_.addPixmap(QPixmap::fromImage(image));
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setFrameShape(QFrame::NoFrame);
}

void ImageView::fit()
{
    fitted_ = true;
    fitInView(scene_.itemsBoundingRect(), Qt::KeepAspectRatio);
}

ImageViewState ImageView::viewState() const
{
    return {transform(), mapToScene(viewport()->rect().center()), fitted_};
}

void ImageView::restoreViewState(const ImageViewState &state)
{
    fitted_ = state.fitted;
    if (fitted_)
        fit();
    else
    {
        setTransform(state.transform);
        centerOn(state.center);
        // Scrollbars can resize the viewport on the next event-loop turn.
        QTimer::singleShot(0, this,
                           [this, state]
                           {
                               if (!fitted_ && transform() == state.transform)
                                   centerOn(state.center);
                           });
    }
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (fitted_)
    {
        fit();
    }
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    fitted_ = false;
    const qreal factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
    const qreal nextScale = transform().m11() * factor;
    if (nextScale > 0.01 && nextScale < 64.0)
    {
        scale(factor, factor);
    }
    event->accept();
}
