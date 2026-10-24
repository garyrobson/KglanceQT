// SPDX-License-Identifier: AGPL-3.0-only

#include "filemonitor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <sys/stat.h>

namespace
{
QString existingParent(const QString &path)
{
    QString parent = QFileInfo(path).absolutePath();
    while (!QDir(parent).exists())
    {
        const auto ancestor = QFileInfo(parent).absolutePath();
        if (ancestor == parent)
            return {};
        parent = ancestor;
    }
    return parent;
}
} // namespace

FileMonitor::FileMonitor(QObject *parent) : QObject(parent)
{
    setObjectName(QStringLiteral("currentFileMonitor"));
    debounce_.setSingleShot(true);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &FileMonitor::scheduleCheck);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &FileMonitor::scheduleCheck);
    connect(&debounce_, &QTimer::timeout, this, &FileMonitor::check);
}

FileMonitor::Snapshot FileMonitor::snapshot(const QString &path)
{
    Snapshot result;
    struct stat status{};
    if (::stat(QFile::encodeName(path).constData(), &status) != 0)
    {
        result.missing = errno == ENOENT || errno == ENOTDIR;
        return result;
    }
    const QFileInfo file(path);
    result.present = true;
    result.missing = false;
    result.readable = file.isReadable();
    result.device = status.st_dev;
    result.inode = status.st_ino;
    result.mode = status.st_mode;
    result.size = status.st_size;
    result.modifiedSeconds = status.st_mtim.tv_sec;
    result.modifiedNanoseconds = status.st_mtim.tv_nsec;
    result.changedSeconds = status.st_ctim.tv_sec;
    result.changedNanoseconds = status.st_ctim.tv_nsec;
    result.resolved = file.canonicalFilePath();
    return result;
}

void FileMonitor::clear()
{
    debounce_.stop();
    const auto paths = watcher_.files() + watcher_.directories();
    if (!paths.isEmpty())
        watcher_.removePaths(paths);
    path_.clear();
    resolved_.clear();
    previous_ = {};
    watching_ = false;
}

bool FileMonitor::setPath(const QString &path)
{
    clear();
    if (path.isEmpty())
        return false;
    path_ = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    previous_ = snapshot(path_);
    resolved_ = previous_.resolved;
    watching_ = arm();
    return watching_;
}

bool FileMonitor::arm()
{
    const auto old = watcher_.files() + watcher_.directories();
    if (!old.isEmpty())
        watcher_.removePaths(old);
    QStringList paths;
    QStringList parents;
    for (const auto &path : {path_, resolved_})
    {
        if (path.isEmpty())
            continue;
        if (QFileInfo::exists(path))
            paths.append(path);
        const auto parent = existingParent(path);
        if (!parent.isEmpty())
        {
            parents.append(parent);
            paths.append(parent);
        }
    }
    paths.removeDuplicates();
    if (!paths.isEmpty())
        watcher_.addPaths(paths);
    // Parent watches survive atomic saves and allow a missing file to reappear.
    // Watching only the current path and its resolved target keeps this bounded
    // to at most four paths, irrespective of selection size or directory size.
    for (const auto &parent : parents)
        if (!watcher_.directories().contains(parent))
            return false;
    return !parents.isEmpty();
}

void FileMonitor::scheduleCheck()
{
    if (path_.isEmpty())
        return;
    if (!debounce_.isActive())
        burst_.start();
    // Coalesce save bursts, but do not starve previews of continuously written logs.
    debounce_.start(int(qBound(qint64(0), 1000 - burst_.elapsed(), qint64(250))));
}

void FileMonitor::check()
{
    if (path_.isEmpty())
        return;
    const auto current = snapshot(path_);
    if (!current.resolved.isEmpty())
        resolved_ = current.resolved;
    const bool wasWatching = watching_;
    watching_ = arm();
    if (wasWatching && !watching_)
        Q_EMIT monitoringFailed(path_);
    if (current == previous_)
        return;
    Change change = Change::Modified;
    if (!current.present)
        change = current.missing ? Change::Removed : Change::Unavailable;
    else if (!current.readable)
        change = Change::Unavailable;
    else if (!previous_.present || !previous_.readable)
        change = Change::Restored;
    else if (current.device != previous_.device || current.inode != previous_.inode ||
             current.resolved != previous_.resolved)
        change = Change::Replaced;
    previous_ = current;
    Q_EMIT changed(path_, change);
}
