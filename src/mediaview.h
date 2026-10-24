// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QWidget>

class QMediaPlayer;
class QMimeType;
class QUrl;

class MediaView final : public QWidget
{
  public:
    explicit MediaView(const QUrl &url, const QMimeType &mime, QWidget *parent = nullptr);
    QMediaPlayer *player() const { return player_; }

  private:
    QMediaPlayer *player_;
};
