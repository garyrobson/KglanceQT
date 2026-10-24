// SPDX-License-Identifier: AGPL-3.0-only

#include "providers.h"

#include <K7Zip>
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KTar>
#include <KZip>
#include <QImageReader>
#include <QLocale>
#include <memory>

ImageResult loadImage(const QString &path, std::stop_token cancellation)
{
    if (cancellation.stop_requested())
    {
        return {{}, QStringLiteral("Preview cancelled.")};
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // Bound decoded allocations; compressed file size is not a useful image
    // limit.
    const QSize size = reader.size();
    constexpr qint64 maxPixels = 64 * 1024 * 1024;
    if (size.isValid() && qint64(size.width()) * size.height() > maxPixels)
    {
        return {{}, QStringLiteral("Image exceeds the 64 megapixel preview limit.")};
    }
    auto image = reader.read();
    const QString error = image.isNull() ? reader.errorString() : QString{};
    return {std::move(image), error};
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

ArchiveResult listArchive(const QString &path, const QString &mime, std::stop_token cancellation)
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
    constexpr qsizetype rowLimit = 10000;
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
