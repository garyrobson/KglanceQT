// SPDX-License-Identifier: AGPL-3.0-only

#include "providers.h"

#include <K7Zip>
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KTar>
#include <KZip>
#include <QElapsedTimer>
#include <QImageReader>
#include <QLocale>
#include <memory>

namespace
{
ImageResult readImage(const QString &path, std::stop_token cancellation, int maxMegapixels,
                      const AnimationLimits *animationLimits)
{
    if (cancellation.stop_requested())
    {
        return {{}, QStringLiteral("Preview cancelled.")};
    }
    QElapsedTimer elapsed;
    elapsed.start();
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // Bound decoded allocations; compressed file size is not a useful image
    // limit.
    const QSize size = reader.size();
    const qint64 maxPixels = qBound(1, maxMegapixels, 64) * 1024LL * 1024;
    if (size.isValid() && qint64(size.width()) * size.height() > maxPixels)
    {
        return {{},
                QStringLiteral("Image exceeds the %1 megapixel preview limit.")
                    .arg(qBound(1, maxMegapixels, 64))};
    }
    auto image = reader.read();
    if (cancellation.stop_requested())
        return {{}, QStringLiteral("Preview cancelled.")};
    if (image.isNull())
        return {{}, reader.errorString()};
    if (qint64(image.width()) * image.height() > maxPixels)
        return {{}, QStringLiteral("Decoded image exceeds the preview pixel limit.")};
    ImageResult result{std::move(image), {}};
    // A plugin's canRead() can parse the next frame header, changing its delay.
    // Capture the displayed frame's delay before probing for another frame.
    const int firstDelay = qMax(20, reader.nextImageDelay());
    // Markdown and other still-image callers do not decode an animation cache.
    if (!animationLimits || !reader.supportsAnimation() || !reader.canRead())
        return result;
    const int frameLimit = qBound(1, animationLimits->frames, MaxAnimationFrames);
    const qint64 byteLimit = qBound(qint64(1), animationLimits->bytes, MaxAnimationBytes);
    // Charge at least 32 bits/pixel, including indexed images that need GUI conversion.
    auto frameBytes = [](const QImage &frame)
    { return qMax(qint64(frame.sizeInBytes()), qint64(frame.width()) * frame.height() * 4); };
    qint64 bytes = frameBytes(result.image);
    ImageAnimation animation;
    animation.loops = qMax(-1, reader.loopCount());
    animation.frames.append({result.image, firstDelay});
    auto still = [&result](const QString &reason)
    {
        result.notice = reason + QStringLiteral(" Showing the first frame.");
        return result;
    };
    while (reader.canRead())
    {
        if (cancellation.stop_requested())
            return {{}, QStringLiteral("Preview cancelled.")};
        if (animation.frames.size() >= frameLimit)
            return still(
                QStringLiteral("Animation exceeds the %1-frame preview limit.").arg(frameLimit));
        if (elapsed.elapsed() >= MaxAnimationDecodeMs)
            return still(QStringLiteral("Animation exceeded the five-second decoding limit."));
        const QSize nextSize = reader.size();
        if (nextSize.isValid() && qint64(nextSize.width()) * nextSize.height() > maxPixels)
            return still(QStringLiteral("An animation frame exceeds the preview pixel limit."));
        if (bytes >= byteLimit ||
            (nextSize.isValid() &&
             qint64(nextSize.width()) * nextSize.height() * 4 > byteLimit - bytes))
            return still(QStringLiteral("Animation exceeds the frame memory limit."));
        auto frame = reader.read();
        if (cancellation.stop_requested())
            return {{}, QStringLiteral("Preview cancelled.")};
        if (frame.isNull())
            return still(
                QStringLiteral("Could not decode the animation: %1.").arg(reader.errorString()));
        if (qint64(frame.width()) * frame.height() > maxPixels)
            return still(QStringLiteral("An animation frame exceeds the preview pixel limit."));
        if (frameBytes(frame) > byteLimit - bytes)
            return still(QStringLiteral("Animation exceeds the frame memory limit."));
        bytes += frameBytes(frame);
        animation.frames.append({std::move(frame), qMax(20, reader.nextImageDelay())});
    }
    if (cancellation.stop_requested())
        return {{}, QStringLiteral("Preview cancelled.")};
    if (elapsed.elapsed() >= MaxAnimationDecodeMs)
        return still(QStringLiteral("Animation exceeded the five-second decoding limit."));
    if (animation.frames.size() > 1)
        result.animation = std::move(animation);
    return result;
}
} // namespace

ImageResult loadImage(const QString &path, std::stop_token cancellation, int maxMegapixels)
{
    return readImage(path, cancellation, maxMegapixels, nullptr);
}

ImageResult loadImagePreview(const QString &path, std::stop_token cancellation, int maxMegapixels,
                             AnimationLimits limits)
{
    return readImage(path, cancellation, maxMegapixels, &limits);
}

bool isArchive(const QString &mime)
{
    return mime == QLatin1String("application/zip") ||
           mime == QLatin1String("application/x-7z-compressed") ||
           mime == QLatin1String("application/x-tar") ||
           mime == QLatin1String("application/x-compressed-tar") ||
           mime == QLatin1String("application/x-bzip-compressed-tar") ||
           mime == QLatin1String("application/x-xz-compressed-tar") ||
           mime == QLatin1String("application/x-zstd-compressed-tar");
}

ArchiveResult listArchive(const QString &path, const QString &mime, std::stop_token cancellation,
                          int maxEntries)
{
    if (cancellation.stop_requested())
    {
        return {{}, QStringLiteral("Preview cancelled."), false};
    }
    std::unique_ptr<KArchive> archive;
    if (mime == QLatin1String("application/zip"))
    {
        archive = std::make_unique<KZip>(path);
    }
    else if (mime == QLatin1String("application/x-7z-compressed"))
    {
        archive = std::make_unique<K7Zip>(path);
    }
    else if (isArchive(mime))
    {
        archive = std::make_unique<KTar>(path);
    }
    else
    {
        return {{}, QStringLiteral("Unsupported archive format."), false};
    }
    if (!archive->open(QIODevice::ReadOnly))
    {
        return {{}, archive->errorString(), false};
    }
    ArchiveResult result;
    struct Directory
    {
        const KArchiveDirectory *entry;
        QString prefix;
    };
    QVector<Directory> pending{{archive->directory(), {}}};
    const qsizetype rowLimit = qBound(1, maxEntries, 10000);
    while (!pending.isEmpty())
    {
        const auto directory = pending.takeLast();
        for (const auto &name : directory.entry->entries())
        {
            if (cancellation.stop_requested())
            {
                return {{}, QStringLiteral("Preview cancelled."), false};
            }
            if (result.rows.size() == rowLimit)
            {
                result.truncated = true;
                return result;
            }
            const auto *entry = directory.entry->entry(name);
            if (!entry)
            {
                continue;
            }
            const QString fullPath = directory.prefix + name;
            if (entry->isDirectory())
            {
                result.rows.append({fullPath + '/', {}});
                pending.append({static_cast<const KArchiveDirectory *>(entry), fullPath + '/'});
            }
            else if (entry->isFile())
            {
                const auto *file = static_cast<const KArchiveFile *>(entry);
                result.rows.append({fullPath, QLocale().formattedDataSize(file->size())});
            }
        }
    }
    return result;
}
