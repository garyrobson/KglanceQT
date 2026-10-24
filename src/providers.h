// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "imageanimation.h"

#include <QImage>
#include <QStringList>
#include <QVector>
#include <optional>
#include <stop_token>

struct ImageResult
{
    QImage image;
    QString error;
    std::optional<ImageAnimation> animation = std::nullopt;
    QString notice = {};
};

struct ArchiveRow
{
    QString path;
    QString size;
    bool operator==(const ArchiveRow &) const = default;
};

struct ArchiveResult
{
    QVector<ArchiveRow> rows;
    QString error;
    bool truncated = false;
};

ImageResult loadImage(const QString &path, std::stop_token cancellation = {},
                      int maxMegapixels = 64);
ImageResult loadImagePreview(const QString &path, std::stop_token cancellation = {},
                             int maxMegapixels = 64, AnimationLimits limits = {});
ArchiveResult listArchive(const QString &path, const QString &mime,
                          std::stop_token cancellation = {}, int maxEntries = 10000);
bool isArchive(const QString &mime);
