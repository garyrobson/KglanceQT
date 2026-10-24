// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QImage>
#include <QVector>

inline constexpr int MaxAnimationFrames = 256;
inline constexpr qint64 MaxAnimationBytes = 64 * 1024LL * 1024;
inline constexpr int MaxAnimationDecodeMs = 5000;

struct ImageFrame
{
    QImage image;
    int delayMs = 100;
};

struct ImageAnimation
{
    QVector<ImageFrame> frames;
    int loops = 0; // Extra passes after the first; -1 means repeat indefinitely.
};

struct AnimationLimits
{
    int frames = MaxAnimationFrames;
    qint64 bytes = MaxAnimationBytes;
};
