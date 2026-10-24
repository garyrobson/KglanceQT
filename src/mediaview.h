// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "viewposition.h"

#include <QWidget>

class QMediaPlayer;
class QMimeType;
class QUrl;

class MediaView final : public QWidget
{
  public:
    explicit MediaView(const QUrl &url, const QMimeType &mime, QWidget *parent = nullptr);
    QMediaPlayer *player() const { return player_; }
    std::optional<MediaPosition> position() const;
    void restorePosition(const MediaPosition &position);

  private:
    void applyPosition();
    QMediaPlayer *player_;
    std::optional<MediaPosition> pending_;
};
