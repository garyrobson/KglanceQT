// SPDX-License-Identifier: AGPL-3.0-only

#include "imageview.h"

#include <QGraphicsPixmapItem>
#include <QTimer>
#include <QWheelEvent>

ImageView::ImageView(const QImage &image, QWidget *parent)
    : QGraphicsView(parent), scene_(this), pixmap_(scene_.addPixmap(QPixmap::fromImage(image)))
{
    setScene(&scene_);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setFrameShape(QFrame::NoFrame);
    frameTimer_.setSingleShot(true);
    connect(&frameTimer_, &QTimer::timeout, this, &ImageView::advanceFrame);
}

void ImageView::setAnimation(ImageAnimation animation, bool autoplay)
{
    frameTimer_.stop();
    animation_ = std::move(animation);
    frame_ = 0;
    loopsLeft_ = animation_.loops;
    finished_ = false;
    playing_ = isAnimated() && autoplay;
    remainingMs_ = 0;
    if (isAnimated())
    {
        showFrame();
        if (playing_)
            frameTimer_.start(qMax(20, animation_.frames.first().delayMs));
    }
    Q_EMIT animationPlayingChanged(playing_);
}

bool ImageView::isAnimated() const { return animation_.frames.size() > 1; }
bool ImageView::isPlaying() const { return playing_; }
int ImageView::currentFrame() const { return frame_; }

void ImageView::toggleAnimation()
{
    if (!isAnimated())
        return;
    if (playing_)
    {
        remainingMs_ = qMax(1, frameTimer_.remainingTime());
        frameTimer_.stop();
        playing_ = false;
    }
    else
    {
        if (finished_)
        {
            frame_ = 0;
            loopsLeft_ = animation_.loops;
            finished_ = false;
            showFrame();
        }
        playing_ = true;
        frameTimer_.start(remainingMs_ > 0 ? remainingMs_
                                           : qMax(20, animation_.frames.at(frame_).delayMs));
        remainingMs_ = 0;
    }
    Q_EMIT animationPlayingChanged(playing_);
}

void ImageView::showFrame()
{
    const QSize previous = pixmap_->pixmap().size();
    pixmap_->setPixmap(QPixmap::fromImage(animation_.frames.at(frame_).image));
    scene_.setSceneRect(pixmap_->boundingRect());
    if (fitted_ && previous != pixmap_->pixmap().size())
        fit();
    Q_EMIT frameChanged(frame_);
}

void ImageView::advanceFrame()
{
    if (!playing_)
        return;
    if (frame_ + 1 == animation_.frames.size())
    {
        if (loopsLeft_ == 0)
        {
            playing_ = false;
            finished_ = true;
            Q_EMIT animationPlayingChanged(false);
            return;
        }
        if (loopsLeft_ > 0)
            --loopsLeft_;
        frame_ = 0;
    }
    else
        ++frame_;
    showFrame();
    frameTimer_.start(qMax(20, animation_.frames.at(frame_).delayMs));
}

void ImageView::fit()
{
    fitted_ = true;
    fitInView(scene_.itemsBoundingRect(), Qt::KeepAspectRatio);
}

ImageViewState ImageView::viewState() const
{
    ImageViewState state{transform(), mapToScene(viewport()->rect().center()), fitted_};
    if (isAnimated())
        state.playback = ImagePlaybackState{frame_, playing_, finished_};
    return state;
}

void ImageView::restoreViewState(const ImageViewState &state)
{
    fitted_ = state.fitted;
    if (isAnimated() && state.playback)
    {
        frameTimer_.stop();
        frame_ = qBound(0, state.playback->frame, int(animation_.frames.size()) - 1);
        finished_ = state.playback->finished && frame_ + 1 == animation_.frames.size();
        playing_ = state.playback->playing && !finished_;
        loopsLeft_ = animation_.loops;
        remainingMs_ = 0;
        showFrame();
        if (playing_)
            frameTimer_.start(qMax(20, animation_.frames.at(frame_).delayMs));
        Q_EMIT animationPlayingChanged(playing_);
    }
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
