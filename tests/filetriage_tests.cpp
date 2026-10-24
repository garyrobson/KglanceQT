// SPDX-License-Identifier: AGPL-3.0-only

#include "filetriage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

class FileTriageTests final : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void initTestCase()
    {
        const auto application = QCoreApplication::applicationDirPath();
        QVERIFY(qEnvironmentVariable("XDG_DATA_HOME").startsWith(application));
        QVERIFY(qEnvironmentVariable("XDG_CONFIG_HOME").startsWith(application));
    }

    void trashAndUndo()
    {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() +
                                QStringLiteral("/triage-XXXXXX"));
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("recoverable.txt"));
        QVERIFY(writeFile(path, "keep this content"));
        FileTriage triage;
        QSignalSpy trashed(&triage, &FileTriage::trashed);
        QSignalSpy restored(&triage, &FileTriage::restored);
        QSignalSpy errors(&triage, &FileTriage::error);
        triage.trash(QUrl::fromLocalFile(path));
        QVERIFY(triage.busy());
        QTRY_COMPARE_WITH_TIMEOUT(trashed.count(), 1, 10000);
        QVERIFY2(errors.isEmpty(),
                 errors.isEmpty() ? "" : qPrintable(errors.first().at(0).toString()));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(triage.canUndo());
        const auto command = trashed.first().at(1).toULongLong();
        QVERIFY(command > 0);
        triage.undo();
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 10000);
        QVERIFY(errors.isEmpty());
        QCOMPARE(restored.first().at(0).toUrl(), QUrl::fromLocalFile(path));
        QCOMPARE(restored.first().at(1).toULongLong(), command);
        QCOMPARE(readFile(path), QByteArray("keep this content"));
        QVERIFY(!triage.busy());
        QVERIFY(!triage.canUndo());
    }

    void undoDoesNotOverwriteOccupiedLocation()
    {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() +
                                QStringLiteral("/triage-XXXXXX"));
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("occupied.txt"));
        QVERIFY(writeFile(path, "original"));
        FileTriage triage;
        QSignalSpy trashed(&triage, &FileTriage::trashed);
        QSignalSpy restored(&triage, &FileTriage::restored);
        QSignalSpy errors(&triage, &FileTriage::error);
        triage.trash(QUrl::fromLocalFile(path));
        QTRY_COMPARE_WITH_TIMEOUT(trashed.count(), 1, 10000);
        QVERIFY(triage.canUndo());
        QVERIFY(writeFile(path, "replacement"));
        triage.undo();
        QCOMPARE(errors.count(), 1);
        QVERIFY(errors.first().at(0).toString().contains(QStringLiteral("occupied")));
        QCOMPARE(restored.count(), 0);
        QCOMPARE(readFile(path), QByteArray("replacement"));
        QVERIFY(triage.canUndo());
        QVERIFY(QFile::remove(path));
        triage.undo();
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 10000);
        QCOMPARE(readFile(path), QByteArray("original"));
    }

    void undoMultipleOperationsInOrder()
    {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() +
                                QStringLiteral("/triage-XXXXXX"));
        QVERIFY(directory.isValid());
        const auto a = QUrl::fromLocalFile(directory.filePath(QStringLiteral("first.txt")));
        const auto b = QUrl::fromLocalFile(directory.filePath(QStringLiteral("second.txt")));
        QVERIFY(writeFile(a.toLocalFile(), "first"));
        QVERIFY(writeFile(b.toLocalFile(), "second"));
        FileTriage triage;
        QSignalSpy trashed(&triage, &FileTriage::trashed);
        QSignalSpy restored(&triage, &FileTriage::restored);
        triage.trash(a);
        QTRY_COMPARE_WITH_TIMEOUT(trashed.count(), 1, 10000);
        triage.trash(b);
        QTRY_COMPARE_WITH_TIMEOUT(trashed.count(), 2, 10000);
        triage.undo();
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 10000);
        QCOMPARE(restored.last().at(0).toUrl(), b);
        QVERIFY(triage.canUndo());
        triage.undo();
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 2, 10000);
        QCOMPARE(restored.last().at(0).toUrl(), a);
    }

    void trashSymlinkPreservesTarget()
    {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() +
                                QStringLiteral("/triage-XXXXXX"));
        QVERIFY(directory.isValid());
        const auto target = directory.filePath(QStringLiteral("target.txt"));
        const auto link = directory.filePath(QStringLiteral("link.txt"));
        QVERIFY(writeFile(target, "target content"));
        QVERIFY(QFile::link(target, link));
        FileTriage triage;
        QSignalSpy trashed(&triage, &FileTriage::trashed);
        QSignalSpy restored(&triage, &FileTriage::restored);
        triage.trash(QUrl::fromLocalFile(link));
        QTRY_COMPARE_WITH_TIMEOUT(trashed.count(), 1, 10000);
        QVERIFY(!QFileInfo(link).isSymLink());
        QCOMPARE(readFile(target), QByteArray("target content"));
        triage.undo();
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 10000);
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(readFile(target), QByteArray("target content"));
    }

    void rejectsMissingAndRemoteFiles()
    {
        FileTriage triage;
        QSignalSpy trashed(&triage, &FileTriage::trashed);
        QSignalSpy errors(&triage, &FileTriage::error);
        triage.trash(QUrl::fromLocalFile(QStringLiteral("/nonexistent/kglanceqt-trash.txt")));
        QCOMPARE(errors.count(), 1);
        QVERIFY(!triage.busy());
        triage.trash(QUrl(QStringLiteral("sftp://example.test/remote.txt")));
        QCOMPARE(errors.count(), 2);
        QCOMPARE(trashed.count(), 0);
        QVERIFY(!triage.canUndo());
    }
};

QTEST_MAIN(FileTriageTests)
#include "filetriage_tests.moc"
