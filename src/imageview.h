// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "imageanimation.h"

#include <QGraphicsView>
#include <QTimer>
#include <optional>

class QGraphicsPixmapItem;

struct ImagePlaybackState
{
    int frame = 0;
    bool playing = true;
    bool finished = false;
};

struct ImageViewState
{
    QTransform transform;
    QPointF center;
    bool fitted = true;
    std::optional<ImagePlaybackState> playback = std::nullopt;
};

class ImageView final : public QGraphicsView
{
    Q_OBJECT
  public:
    explicit ImageView(const QImage &image, QWidget *parent = nullptr);
    void fit();
    ImageViewState viewState() const;
    void restoreViewState(const ImageViewState &state);
    void setAnimation(ImageAnimation animation, bool autoplay = true);
    bool isAnimated() const;
    bool isPlaying() const;
    int currentFrame() const;
    void toggleAnimation();

  Q_SIGNALS:
    void animationPlayingChanged(bool playing);
    void frameChanged(int frame);
    void viewChanged();

  protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  private:
    void advanceFrame();
    void showFrame();
    QGraphicsScene scene_;
    QGraphicsPixmapItem *pixmap_;
    QTimer frameTimer_{this};
    ImageAnimation animation_;
    int frame_ = 0;
    int loopsLeft_ = 0;
    int remainingMs_ = 0;
    bool finished_ = false;
    bool playing_ = false;
    bool fitted_ = true;
};
