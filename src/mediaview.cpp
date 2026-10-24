// SPDX-License-Identifier: AGPL-3.0-only

#include "mediaview.h"
#include "preferences.h"
#include "previewsurface.h"

#include <QAudioOutput>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QMimeType>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <algorithm>
#include <limits>

namespace
{
QString timestamp(qint64 milliseconds)
{
    const auto seconds = milliseconds / 1000;
    const auto minutes = seconds / 60;
    if (minutes >= 60)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(minutes / 60)
            .arg(minutes % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

MediaView::MediaView(const QUrl &url, const QMimeType &mime, QWidget *parent)
    : QWidget(parent), player_(new QMediaPlayer(this))
{
    auto *audio = new QAudioOutput(this);
    const auto preferences = readPreferences();
    audio->setVolume(preferences.volume / 100.0f);
    player_->setAudioOutput(audio);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    const bool video = mime.name().startsWith(QLatin1String("video/"));
    QVBoxLayout *controlsLayout = layout;
    if (video)
    {
        auto *output = new QVideoWidget(this);
        player_->setVideoOutput(output);
        layout->addWidget(output, 1);
    }
    else
    {
        auto *surface = new PreviewSurface(mime, this);
        layout->addWidget(surface);
        auto *center = new QVBoxLayout(surface);
        center->addStretch();
        auto *row = new QHBoxLayout;
        center->addLayout(row);
        row->addStretch();
        auto *controls = new QFrame(surface);
        controls->setObjectName(QStringLiteral("audioControls"));
        controls->setMaximumWidth(640);
        controls->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        controls->setFrameShape(QFrame::StyledPanel);
        controls->setAutoFillBackground(true);
        auto colors = palette();
        controls->setPalette(colors);
        row->addWidget(controls, 1);
        row->addStretch();
        controlsLayout = new QVBoxLayout(controls);
        controlsLayout->setContentsMargins(20, 16, 20, 16);
        controlsLayout->setSpacing(12);
        auto *type = new QLabel(mime.comment(), controls);
        type->setAlignment(Qt::AlignCenter);
        controlsLayout->addWidget(type);
        center->addStretch();
    }

    auto *seek = new QSlider(Qt::Horizontal, this);
    seek->setObjectName(QStringLiteral("mediaSeek"));
    seek->setAccessibleName(tr("Playback position"));
    seek->setRange(0, 0);
    seek->setEnabled(false);
    controlsLayout->addWidget(seek);
    auto *row = new QHBoxLayout;
    controlsLayout->addLayout(row);
    if (!video)
    {
        auto *play = new QToolButton(this);
        play->setObjectName(QStringLiteral("mediaPlayPause"));
        play->setIconSize(QSize(28, 28));
        row->addWidget(play);
        auto updatePlay = [this, play]
        {
            const bool playing = player_->playbackState() == QMediaPlayer::PlayingState;
            play->setIcon(QIcon::fromTheme(
                playing ? QStringLiteral("media-playback-pause")
                        : QStringLiteral("media-playback-start"),
                style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay)));
            play->setToolTip(playing ? tr("Pause") : tr("Play"));
            play->setAccessibleName(play->toolTip());
        };
        connect(player_, &QMediaPlayer::playbackStateChanged, this, updatePlay);
        updatePlay();
        connect(play, &QToolButton::clicked, player_,
                [this]
                {
                    if (player_->playbackState() == QMediaPlayer::PlayingState)
                        player_->pause();
                    else
                        player_->play();
                });
    }
    auto *time = new QLabel(this);
    time->setObjectName(QStringLiteral("mediaTime"));
    auto updateTime = [this, time]
    {
        time->setText(timestamp(player_->position()) + QStringLiteral(" / ") +
                      timestamp(player_->duration()));
    };
    updateTime();
    row->addWidget(time);
    row->addStretch();
    if (!video)
    {
        auto *mute = new QToolButton(this);
        mute->setObjectName(QStringLiteral("mediaMute"));
        mute->setCheckable(true);
        auto updateMute = [this, mute](bool muted)
        {
            mute->setChecked(muted);
            mute->setIcon(QIcon::fromTheme(muted ? QStringLiteral("audio-volume-muted")
                                                 : QStringLiteral("audio-volume-high"),
                                           style()->standardIcon(muted ? QStyle::SP_MediaVolumeMuted
                                                                       : QStyle::SP_MediaVolume)));
            mute->setToolTip(muted ? tr("Unmute") : tr("Mute"));
            mute->setAccessibleName(mute->toolTip());
        };
        updateMute(audio->isMuted());
        connect(mute, &QToolButton::toggled, audio, &QAudioOutput::setMuted);
        connect(audio, &QAudioOutput::mutedChanged, this, updateMute);
        row->addWidget(mute);
        auto *volume = new QSlider(Qt::Horizontal, this);
        volume->setObjectName(QStringLiteral("mediaVolume"));
        volume->setAccessibleName(tr("Volume"));
        volume->setToolTip(tr("Volume"));
        volume->setRange(0, 100);
        volume->setMaximumWidth(100);
        volume->setValue(qRound(audio->volume() * 100));
        connect(volume, &QSlider::valueChanged, audio,
                [audio](int value) { audio->setVolume(value / 100.0f); });
        connect(audio, &QAudioOutput::volumeChanged, volume,
                [volume](float value)
                {
                    const QSignalBlocker blocker(volume);
                    volume->setValue(qRound(value * 100));
                });
        row->addWidget(volume);
    }
    connect(player_, &QMediaPlayer::durationChanged, seek,
            [seek](qint64 duration)
            {
                const QSignalBlocker blocker(seek);
                seek->setMaximum(
                    int(std::min(duration / 1000, qint64(std::numeric_limits<int>::max()))));
            });
    connect(player_, &QMediaPlayer::seekableChanged, seek, &QSlider::setEnabled);
    connect(player_, &QMediaPlayer::positionChanged, seek,
            [seek](qint64 position)
            {
                if (!seek->isSliderDown())
                {
                    const QSignalBlocker blocker(seek);
                    seek->setValue(int(position / 1000));
                }
            });
    connect(seek, &QSlider::valueChanged, player_,
            [this](int value) { player_->setPosition(qint64(value) * 1000); });
    connect(player_, &QMediaPlayer::durationChanged, this, updateTime);
    connect(player_, &QMediaPlayer::positionChanged, this, updateTime);
    player_->setSource(url);
    if (video ? preferences.autoplayVideo : preferences.autoplayAudio)
        player_->play();
}
