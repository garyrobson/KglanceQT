// SPDX-License-Identifier: AGPL-3.0-only

#include "providers.h"

#include <KZip>
#include <QFile>
#include <QImageReader>
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

    void animatedImageFramesAndComposition()
    {
        const QString path = QFINDTESTDATA("data/animation.gif");
        QVERIFY(!path.isEmpty());
        const auto result = loadImagePreview(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY2(result.animation.has_value(), qPrintable(result.notice));
        QVERIFY(result.notice.isEmpty());
        const auto &animation = *result.animation;
        QCOMPARE(animation.frames.size(), 3);
        QCOMPARE(animation.loops, -1);
        QCOMPARE(animation.frames.at(0).image, result.image);
        QCOMPARE(animation.frames.at(0).delayMs, 100);
        QCOMPARE(animation.frames.at(1).delayMs, 160);
        QCOMPARE(animation.frames.at(2).delayMs, 220);
        for (const auto &frame : animation.frames)
            QCOMPARE(frame.image.size(), QSize(24, 16));
        QCOMPARE(animation.frames.at(0).image.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(animation.frames.at(1).image.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(animation.frames.at(1).image.pixelColor(10, 8), QColor(Qt::green));
        QCOMPARE(animation.frames.at(2).image.pixelColor(0, 0), QColor(Qt::blue));
        const auto once = loadImagePreview(QFINDTESTDATA("data/animation-once.gif"));
        QVERIFY(once.animation);
        QCOMPARE(once.animation->loops, 0);
        // Markdown's still-image loader retains its first-frame-only contract.
        const auto still = loadImage(path);
        QCOMPARE(still.image, result.image);
        QVERIFY(!still.animation);
    }

    void animationLimitsKeepFirstFrame()
    {
        const QString path = QFINDTESTDATA("data/animation.gif");
        const auto frameLimited = loadImagePreview(path, {}, 64, {2, MaxAnimationBytes});
        QVERIFY(!frameLimited.image.isNull());
        QVERIFY(frameLimited.error.isEmpty());
        QVERIFY(!frameLimited.animation);
        QVERIFY(frameLimited.notice.contains(QStringLiteral("2-frame")));
        const auto memoryLimited = loadImagePreview(path, {}, 64, {256, 24 * 16 * 4 * 2});
        QCOMPARE(memoryLimited.image, frameLimited.image);
        QVERIFY(!memoryLimited.animation);
        QVERIFY(memoryLimited.notice.contains(QStringLiteral("memory limit")));
        const auto hardLimited = loadImagePreview(QFINDTESTDATA("data/animation-over-limit.gif"),
                                                  {}, 64, {99999, MaxAnimationBytes});
        QVERIFY(!hardLimited.image.isNull());
        QVERIFY(!hardLimited.animation);
        QVERIFY(hardLimited.notice.contains(QStringLiteral("256-frame")));
        std::stop_source stop;
        stop.request_stop();
        const auto cancelled = loadImagePreview(path, stop.get_token());
        QVERIFY(cancelled.image.isNull());
        QVERIFY(!cancelled.animation);
        QCOMPARE(cancelled.error, QStringLiteral("Preview cancelled."));
    }

    void invalidAnimationPreview()
    {
        const auto result = loadImagePreview(QStringLiteral("/nonexistent/animation.gif"));
        QVERIFY(result.image.isNull());
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.animation);
    }

    void corruptAnimationKeepsUsablePreview()
    {
        const auto result = loadImagePreview(QFINDTESTDATA("data/animation-corrupt.gif"));
        QVERIFY(!result.image.isNull());
        QCOMPARE(result.image.pixelColor(0, 0), QColor(Qt::red));
        QVERIFY(result.error.isEmpty());
        // Qt's GIF plugin can treat a malformed tail as EOF without an error.
        // Keep its valid prefix; if a plugin reports failure, explain the fallback.
        if (result.animation)
        {
            QCOMPARE(result.animation->frames.size(), 2);
            QCOMPARE(result.animation->frames.first().image, result.image);
            QCOMPARE(result.animation->frames.last().image.pixelColor(10, 8), QColor(Qt::green));
        }
        else
            QVERIFY2(result.notice.contains(QStringLiteral("Could not decode")),
                     qPrintable(result.notice));
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
