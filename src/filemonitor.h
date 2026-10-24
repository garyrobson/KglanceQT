// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QTimer>

class FileMonitor final : public QObject
{
    Q_OBJECT
  public:
    enum class Change
    {
        Modified,
        Removed,
        Replaced,
        Restored,
        Unavailable
    };
    Q_ENUM(Change)

    explicit FileMonitor(QObject *parent = nullptr);
    bool setPath(const QString &path);
    void clear();

  Q_SIGNALS:
    void changed(const QString &path, FileMonitor::Change change);
    void monitoringFailed(const QString &path);

  private:
    struct Snapshot
    {
        bool present = false;
        bool missing = true;
        bool readable = false;
        quint64 device = 0;
        quint64 inode = 0;
        quint32 mode = 0;
        qint64 size = 0;
        qint64 modifiedSeconds = 0;
        qint64 modifiedNanoseconds = 0;
        qint64 changedSeconds = 0;
        qint64 changedNanoseconds = 0;
        QString resolved;
        bool operator==(const Snapshot &) const = default;
    };

    static Snapshot snapshot(const QString &path);
    bool arm();
    void scheduleCheck();
    void check();
    QFileSystemWatcher watcher_{this};
    QTimer debounce_{this};
    QElapsedTimer burst_;
    QString path_;
    QString resolved_;
    Snapshot previous_;
    bool watching_ = false;
};
