// SPDX-License-Identifier: AGPL-3.0-only

#include "filemonitor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

bool replaceFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

FileMonitor::Change change(const QSignalSpy &spy)
{
    return spy.last().at(1).value<FileMonitor::Change>();
}
} // namespace

class FileMonitorTests final : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void coalescesChangesAndIgnoresNeighbours()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("current.txt"));
        QVERIFY(writeFile(path, "original"));
        FileMonitor monitor;
        QVERIFY(monitor.setPath(path));
        QSignalSpy changed(&monitor, &FileMonitor::changed);
        QVERIFY(writeFile(directory.filePath(QStringLiteral("neighbour.txt")), "unrelated"));
        QTest::qWait(400);
        QCOMPARE(changed.count(), 0);
        for (int i = 0; i < 3; ++i)
        {
            QVERIFY(writeFile(path, QByteArray::number(i)));
            QTest::qWait(30);
        }
        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(changed.last().at(0).toString(), path);
        QCOMPARE(change(changed), FileMonitor::Change::Modified);
        monitor.clear();
        auto *watcher = monitor.findChild<QFileSystemWatcher *>();
        QVERIFY(watcher);
        QVERIFY(watcher->files().isEmpty());
        QVERIFY(watcher->directories().isEmpty());
        QVERIFY(writeFile(path, "hidden change"));
        QTest::qWait(400);
        QCOMPARE(changed.count(), 1);
    }

    void atomicReplacementRearmsFileWatch()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("atomic.txt"));
        QVERIFY(writeFile(path, "before"));
        const auto modified = QFileInfo(path).lastModified();
        FileMonitor monitor;
        QVERIFY(monitor.setPath(path));
        QSignalSpy changed(&monitor, &FileMonitor::changed);
        QVERIFY(replaceFile(path, "after!"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(QFileInfo(path).lastModified(), modified);
        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(change(changed), FileMonitor::Change::Replaced);
        // An in-place write does not change the parent directory's contents.
        QVERIFY(writeFile(path, "a subsequent write"));
        QTRY_COMPARE(changed.count(), 2);
        QCOMPARE(change(changed), FileMonitor::Change::Modified);
    }

    void missingParentAndFileCanReturn()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto parent = directory.filePath(QStringLiteral("nested/parent"));
        QVERIFY(QDir().mkpath(parent));
        const auto path = parent + QStringLiteral("/current.txt");
        QVERIFY(writeFile(path, "before"));
        FileMonitor monitor;
        QVERIFY(monitor.setPath(path));
        QSignalSpy changed(&monitor, &FileMonitor::changed);
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().rmdir(parent));
        QVERIFY(QDir().rmdir(directory.filePath(QStringLiteral("nested"))));
        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(change(changed), FileMonitor::Change::Removed);
        QVERIFY(QDir().mkpath(parent));
        QVERIFY(writeFile(path, "restored"));
        QTRY_COMPARE(changed.count(), 2);
        QCOMPARE(change(changed), FileMonitor::Change::Restored);
    }

    void watchesResolvedSymlinkTarget()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("target"))));
        const auto target = directory.filePath(QStringLiteral("target/source.txt"));
        const auto link = directory.filePath(QStringLiteral("link.txt"));
        QVERIFY(writeFile(target, "before"));
        QVERIFY(QFile::link(target, link));
        FileMonitor monitor;
        QVERIFY(monitor.setPath(link));
        auto *watcher = monitor.findChild<QFileSystemWatcher *>();
        QVERIFY(watcher);
        QVERIFY(watcher->files().size() + watcher->directories().size() <= 4);
        QSignalSpy changed(&monitor, &FileMonitor::changed);
        QVERIFY(replaceFile(target, "replacement"));
        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(change(changed), FileMonitor::Change::Replaced);
        QVERIFY(writeFile(target, "in-place update"));
        QTRY_COMPARE(changed.count(), 2);
        QCOMPARE(change(changed), FileMonitor::Change::Modified);
    }

    void navigationAndClearCancelPendingChanges()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto first = directory.filePath(QStringLiteral("first.txt"));
        const auto second = directory.filePath(QStringLiteral("second.txt"));
        QVERIFY(writeFile(first, "first"));
        QVERIFY(writeFile(second, "second"));
        FileMonitor monitor;
        QVERIFY(monitor.setPath(first));
        QSignalSpy changed(&monitor, &FileMonitor::changed);
        QVERIFY(writeFile(first, "old selection"));
        QCoreApplication::processEvents();
        QVERIFY(monitor.setPath(second));
        QTest::qWait(400);
        QCOMPARE(changed.count(), 0);
        QVERIFY(writeFile(second, "new selection"));
        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(changed.last().at(0).toString(), second);
        QVERIFY(writeFile(second, "cancelled"));
        QCoreApplication::processEvents();
        monitor.clear();
        QTest::qWait(400);
        QCOMPARE(changed.count(), 1);
    }

    void continuousWritesDoNotStarveRefresh()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("log.txt"));
        QVERIFY(writeFile(path, "log"));
        FileMonitor monitor;
        QVERIFY(monitor.setPath(path));
        QSignalSpy changed(&monitor, &FileMonitor::changed);
        QTimer writer;
        int count = 0;
        connect(&writer, &QTimer::timeout, this,
                [&] { QVERIFY(writeFile(path, QByteArray::number(++count))); });
        writer.start(50);
        QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 2500);
        QVERIFY(writer.isActive());
        writer.stop();
        QCOMPARE(change(changed), FileMonitor::Change::Modified);
    }
};

QTEST_GUILESS_MAIN(FileMonitorTests)
#include "filemonitor_tests.moc"
