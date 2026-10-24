// SPDX-License-Identifier: AGPL-3.0-only

#include "previewwindow.h"

#include <KParts/ReadOnlyPart>
#include <KProtocolInfo>
#include <KTextEditor/View>
#include <QApplication>
#include <QFile>
#include <QGraphicsView>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class WindowTests final : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void desktopUrl()
    {
        if (!KProtocolInfo::isKnownProtocol(QStringLiteral("desktop")))
        {
            QSKIP("The optional Plasma desktop KIO worker is not installed.");
        }
        const auto desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        QVERIFY(desktop.startsWith(QCoreApplication::applicationDirPath()));
        QFile file(desktop + QStringLiteral("/desktop preview.cpp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("int desktop_preview = 1;\n");
        file.close();
        PreviewWindow window(false);
        window.ShowPreview({QStringLiteral("desktop:/desktop%20preview.cpp")}, {});
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<KTextEditor::View *>() != nullptr, 10000);
        QVERIFY(window.windowTitle().contains(QStringLiteral("desktop preview.cpp")));
        window.HidePreview();
    }

    void nativeViewersAndLifecycle()
    {
        qApp->setQuitOnLastWindowClosed(false);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile text(directory.filePath(QStringLiteral("hello.cpp")));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("int main() { return 0; }\n");
        text.close();
        PreviewWindow window(false);
        window.ShowPreview({text.fileName()}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>() != nullptr);
        QVERIFY(window.IsVisible());
        QTest::keyClick(window.findChild<KTextEditor::View *>(), Qt::Key_Space);
        QVERIFY(!window.IsVisible());
        QCOMPARE(window.findChild<KTextEditor::View *>(), nullptr);

        const QString imagePath = directory.filePath(QStringLiteral("hello.png"));
        QImage image(50, 30, QImage::Format_ARGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(imagePath));
        window.ShowPreview({imagePath}, {});
        QTRY_VERIFY(window.findChild<QGraphicsView *>() != nullptr);
        QVERIFY(window.IsVisible());
        window.HidePreview();
        QCOMPARE(window.findChild<QGraphicsView *>(), nullptr);

        const QString pdfPath = directory.filePath(QStringLiteral("hello.pdf"));
        {
            QPdfWriter pdf(pdfPath);
            QPainter painter(&pdf);
            painter.drawText(100, 100, QStringLiteral("KGlanceQT PDF test"));
        }
        window.ShowPreview({pdfPath}, {});
        QTRY_VERIFY(window.windowTitle().contains(QStringLiteral("hello.pdf")));
        QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("okular::Part")) != nullptr ||
                    !window.findChildren<KParts::ReadOnlyPart *>().isEmpty());
        window.HidePreview();
        QVERIFY(!window.IsVisible());
    }
};

QTEST_MAIN(WindowTests)
#include "window_tests.moc"
