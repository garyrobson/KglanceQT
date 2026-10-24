// SPDX-License-Identifier: AGPL-3.0-only

#include "providers.h"

#include <KZip>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QtTest>

class ProviderTests final : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void imageRoundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("sample.png"));
        QImage original(13, 7, QImage::Format_ARGB32);
        original.fill(Qt::blue);
        QVERIFY(original.save(path));
        const auto result = loadImage(path);
        QCOMPARE(result.error, QString{});
        QCOMPARE(result.image.convertToFormat(original.format()), original);
    }

    void invalidImage()
    {
        const auto result = loadImage(QStringLiteral("/nonexistent/kglanceqt-image.png"));
        QVERIFY(result.image.isNull());
        QVERIFY(!result.error.isEmpty());
    }

    void configuredImageLimit()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("large.png"));
        QImage image(1500, 1500, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path));
        const auto limited = loadImage(path, {}, 1);
        QVERIFY(limited.image.isNull());
        QVERIFY(limited.error.contains(QStringLiteral("1 megapixel")));
        QVERIFY(!loadImage(path, {}, 4).image.isNull());
    }

    void cancelledJobsDoNotOpenFiles()
    {
        std::stop_source stop;
        stop.request_stop();
        const auto image = loadImage(QStringLiteral("unused"), stop.get_token());
        QVERIFY(image.image.isNull());
        QCOMPARE(image.error, QStringLiteral("Preview cancelled."));
        const auto archive = listArchive(QStringLiteral("unused"),
                                         QStringLiteral("application/zip"), stop.get_token());
        QCOMPARE(archive.rows, QVector<ArchiveRow>{});
        QCOMPARE(archive.error, QStringLiteral("Preview cancelled."));
        QVERIFY(!archive.truncated);
    }

    void archiveListsWithoutExtracting()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("sample.zip"));
        KZip archive(path);
        QVERIFY(archive.open(QIODevice::WriteOnly));
        QVERIFY(archive.writeFile(QStringLiteral("nested/example.txt"), QByteArray("hello")));
        QVERIFY(archive.close());
        const auto result = listArchive(path, QStringLiteral("application/zip"));
        const QVector<ArchiveRow> expected{
            {QStringLiteral("nested/"), {}},
            {QStringLiteral("nested/example.txt"), QLocale().formattedDataSize(5)}};
        QCOMPARE(result.rows, expected);
        QCOMPARE(result.error, QString{});
        QVERIFY(!result.truncated);
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("nested"))));
    }

    void unsupportedArchive()
    {
        const auto result =
            listArchive(QStringLiteral("unused"), QStringLiteral("application/pdf"));
        QCOMPARE(result.rows, QVector<ArchiveRow>{});
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.truncated);
    }

    void configuredArchiveLimit()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("limited.zip"));
        KZip archive(path);
        QVERIFY(archive.open(QIODevice::WriteOnly));
        QVERIFY(archive.writeFile(QStringLiteral("one.txt"), QByteArray("one")));
        QVERIFY(archive.writeFile(QStringLiteral("two.txt"), QByteArray("two")));
        QVERIFY(archive.close());
        const auto result = listArchive(path, QStringLiteral("application/zip"), {}, 1);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.rows.size(), 1);
        QVERIFY(result.truncated);
    }
};

QTEST_GUILESS_MAIN(ProviderTests)
#include "provider_tests.moc"
