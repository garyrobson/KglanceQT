// SPDX-License-Identifier: AGPL-3.0-only

#include "informationpanel.h"
#include "markdownview.h"
#include "preferences.h"
#include "previewwindow.h"

#include <KParts/ReadOnlyPart>
#include <KProtocolInfo>
#include <KRatingWidget>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDataStream>
#include <QFile>
#include <QGraphicsView>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMenu>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextTable>
#include <QToolBar>
#include <QToolButton>
#include <QVideoWidget>
#include <QtTest>
#include <baloo/filemetadatawidget.h>

class WindowTests final : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void init()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("kglanceqt-tests"));
        QVERIFY(QSettings().fileName().startsWith(QCoreApplication::applicationDirPath()));
        QSettings().clear();
    }

    void applicationMenuAndCopyLocation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("file with spaces.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("preview\n");
        file.close();
        PreviewWindow window(false);
        window.ShowPreview({file.fileName()}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>() != nullptr);
        auto *menu = window.findChild<QMenu *>(QStringLiteral("applicationMenu"));
        auto *button = window.findChild<QToolButton *>(QStringLiteral("applicationMenuButton"));
        auto *quit = window.findChild<QAction *>(QStringLiteral("quitKGlanceQT"));
        QVERIFY(menu);
        QVERIFY(button);
        QVERIFY(quit);
        QCOMPARE(button->menu(), menu);
        QVERIFY(menu->actions().contains(quit));
        QCOMPARE(quit->shortcut(), QKeySequence(QKeySequence::Quit));
        for (auto *toolbar : window.findChildren<QToolBar *>())
            QVERIFY(!toolbar->actions().contains(quit));
        QVERIFY(menu->actions().contains(window.findChild<QAction *>(QStringLiteral("openWith"))));
        QVERIFY(menu->actions().contains(
            window.findChild<QAction *>(QStringLiteral("showInFileManager"))));
        auto *copy = window.findChild<QAction *>(QStringLiteral("copyLocation"));
        QVERIFY(copy);
        copy->trigger();
        QCOMPARE(QApplication::clipboard()->text(), file.fileName());
        auto *help = window.findChild<QMenu *>(QStringLiteral("helpMenu"));
        auto *types = window.findChild<QAction *>(QStringLiteral("showSupportedTypes"));
        QVERIFY(help);
        QVERIFY(types);
        QVERIFY(menu->actions().contains(help->menuAction()));
        QVERIFY(help->actions().contains(types));
        types->trigger();
        auto *dialog = window.findChild<QDialog *>(QStringLiteral("supportedTypesDialog"));
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        auto *details = dialog->findChild<QTextBrowser *>(QStringLiteral("supportedTypesDetails"));
        QVERIFY(details);
        const auto text = details->toPlainText();
        QVERIFY(text.contains(QStringLiteral("Markdown")));
        QVERIFY(text.contains(QStringLiteral("ZIP")));
        for (const auto &format : QImageReader::supportedImageFormats())
            QVERIFY(text.contains(QString::fromLatin1(format).toUpper()));
        types->trigger();
        QCOMPARE(window.findChildren<QDialog *>(QStringLiteral("supportedTypesDialog")).size(), 1);
        QPointer<QDialog> closed = dialog;
        dialog->close();
        QTRY_VERIFY(closed.isNull());
        types->trigger();
        QPointer<QDialog> reopened =
            window.findChild<QDialog *>(QStringLiteral("supportedTypesDialog"));
        QVERIFY(reopened);
        window.HidePreview();
        QTRY_VERIFY(reopened.isNull());
        QVERIFY(!window.IsVisible());
        // Hiding leaves the resident window reusable.
        window.ShowPreview({file.fileName()}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>() != nullptr);
        window.HidePreview();
    }

    void imagePositionMemory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QStringList paths;
        for (const auto &name : {QStringLiteral("first.png"), QStringLiteral("second.png")})
        {
            QImage image(1800, 1400, QImage::Format_RGB32);
            image.fill(Qt::green);
            const auto path = directory.filePath(name);
            QVERIFY(image.save(path));
            paths.append(path);
        }
        PreviewWindow window(false);
        window.ShowPreview(paths, {});
        auto currentImage = [&window]() -> ImageView *
        {
            for (auto *view : window.findChildren<QGraphicsView *>())
                if (auto *image = dynamic_cast<ImageView *>(view))
                    return image;
            return nullptr;
        };
        QTRY_VERIFY(currentImage());
        ImageViewState state;
        state.fitted = false;
        state.transform.scale(1.5, 1.5);
        state.center = QPointF(1000, 800);
        currentImage()->restoreViewState(state);
        const auto saved = currentImage()->viewState();
        QTest::keyClick(&window, Qt::Key_Right);
        QTRY_VERIFY(currentImage());
        QVERIFY(currentImage()->viewState().fitted);
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_VERIFY(currentImage());
        QTRY_COMPARE(currentImage()->viewState().transform, saved.transform);
        QVERIFY(!currentImage()->viewState().fitted);
        QTRY_VERIFY(QLineF(currentImage()->viewState().center, saved.center).length() < 2);
        window.HidePreview();
        window.ShowPreview(paths, {});
        QTRY_VERIFY(currentImage());
        QVERIFY(currentImage()->viewState().fitted);
        window.HidePreview();
    }

    void markdownRenderingAndSource()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage image(40, 20, QImage::Format_ARGB32);
        image.fill(Qt::cyan);
        QVERIFY(image.save(directory.filePath(QStringLiteral("local.png"))));
        QFile file(directory.filePath(QStringLiteral("example.md")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray markdown = "# Heading\n\n**bold example**\n\n- [x] Done\n\n"
                                    "| A | B |\n|---|---|\n| 1 | 2 |\n\n"
                                    "![local](local.png)\n\n[heading](#heading)\n";
        file.write(markdown);
        file.close();
        PreviewWindow window(false);
        window.ShowPreview({file.fileName(), directory.filePath(QStringLiteral("next.txt"))}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>() != nullptr);
        auto *browser = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QVERIFY(browser);
        QVERIFY(browser->toPlainText().contains(QStringLiteral("bold example")));
        QVERIFY(!browser->toPlainText().contains(QStringLiteral("**bold")));
        bool table = false;
        for (auto *frame : browser->document()->rootFrame()->childFrames())
            table |= qobject_cast<QTextTable *>(frame) != nullptr;
        QVERIFY(table);
        QVERIFY(browser->document()->begin().begin().fragment().charFormat().anchorNames().contains(
            QStringLiteral("heading")));
        const auto imageUrl = QUrl::fromLocalFile(directory.filePath(QStringLiteral("local.png")));
        QTRY_COMPARE(qvariant_cast<QImage>(
                         browser->document()->resource(QTextDocument::ImageResource, imageUrl))
                         .size(),
                     image.size());
        const auto remote = browser->document()->resource(
            QTextDocument::ImageResource, QUrl(QStringLiteral("https://invalid.example/no.png")));
        QCOMPARE(qvariant_cast<QImage>(remote).size(), QSize(20, 20));
        QVERIFY(!browser->openLinks());

        QTest::keyClick(browser, Qt::Key_F, Qt::ControlModifier);
        auto *query = window.findChild<QLineEdit *>(QStringLiteral("markdownSearch"));
        QVERIFY(query);
        QTRY_VERIFY(query->isVisible());
        QTest::keyClicks(query, "bold");
        QTest::keyClick(query, Qt::Key_Return);
        QCOMPARE(browser->textCursor().selectedText(), QStringLiteral("bold"));
        QTest::keyClick(query, Qt::Key_Right);
        QVERIFY(window.windowTitle().contains(QStringLiteral("example.md")));
        auto *tabs = window.findChild<QTabBar *>(QStringLiteral("markdownMode"));
        QVERIFY(tabs);
        tabs->setCurrentIndex(1);
        auto *source = window.findChild<KTextEditor::View *>();
        QVERIFY(source->isVisible());
        QVERIFY(!source->document()->isReadWrite());
        QCOMPARE(source->document()->text().trimmed(), QString::fromUtf8(markdown).trimmed());
        tabs->setCurrentIndex(0);
        QTest::keyClick(browser, Qt::Key_Right);
        QCOMPARE(window.findChild<MarkdownView *>(), nullptr);
        window.HidePreview();
    }

    void markdownLimitsAndDefaultMode()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("large.markdown")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(300 * 1024, 'x'));
        file.close();
        PreviewWindow window(false);
        window.ShowPreview({file.fileName()}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>() != nullptr);
        auto *tabs = window.findChild<QTabBar *>(QStringLiteral("markdownMode"));
        QVERIFY(tabs);
        QCOMPARE(tabs->currentIndex(), 1);
        QVERIFY(!tabs->isTabEnabled(0));
        window.HidePreview();
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("# Small\n");
        file.close();
        QSettings().setValue(QStringLiteral("markdown/source"), true);
        window.ShowPreview({file.fileName()}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>() != nullptr);
        tabs = window.findChild<QTabBar *>(QStringLiteral("markdownMode"));
        QCOMPARE(tabs->currentIndex(), 1);
        QVERIFY(tabs->isTabEnabled(0));
        window.HidePreview();
    }

    void informationFollowsSelection()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QStringList paths;
        for (const auto &name : {QStringLiteral("first.png"), QStringLiteral("second.png")})
        {
            QImage image(31, 17, QImage::Format_ARGB32);
            image.fill(Qt::red);
            const auto path = directory.filePath(name);
            QVERIFY(image.save(path));
            paths.append(path);
        }
        PreviewWindow window(false);
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<QGraphicsView *>() != nullptr);
        auto *panel = window.findChild<InformationPanel *>();
        QVERIFY(panel);
        QVERIFY(!panel->isVisible());
        QTest::keyClick(&window, Qt::Key_F11);
        QTRY_VERIFY(panel->isVisible());
        auto *metadata = panel->findChild<Baloo::FileMetaDataWidget *>();
        QVERIFY(metadata);
        QVERIFY(metadata->isReadOnly());
        QCOMPARE(metadata->items().first().url(), QUrl::fromLocalFile(paths.first()));
        QPointer<Baloo::FileMetaDataWidget> first = metadata;
        QTest::keyClick(&window, Qt::Key_Right);
        QVERIFY(first.isNull());
        metadata = panel->findChild<Baloo::FileMetaDataWidget *>();
        QVERIFY(metadata);
        QCOMPARE(metadata->items().first().url(), QUrl::fromLocalFile(paths.last()));
        QSignalSpy finished(metadata, &Baloo::FileMetaDataWidget::metaDataRequestFinished);
        QTRY_VERIFY(!finished.isEmpty());
        QVERIFY(metadata->isReadOnly());
        for (auto *rating : metadata->findChildren<KRatingWidget *>())
            QVERIFY(!rating->isEnabled());
        QVERIFY(!panel->findChildren<QLabel *>(QStringLiteral("L_dimensions")).isEmpty());
        window.HidePreview();
        QCOMPARE(panel->findChild<Baloo::FileMetaDataWidget *>(), nullptr);
        QVERIFY(readPreferences().information);
    }

    void preferencesApplyCancelAndBounds()
    {
        QSettings().setValue(QStringLiteral("window/geometry"), QByteArray("preserved"));
        {
            PreferencesDialog dialog;
            auto *audio = dialog.findChild<QCheckBox *>(QStringLiteral("autoplayAudio"));
            QVERIFY(audio);
            audio->setChecked(false);
            dialog.reject();
            QVERIFY(readPreferences().autoplayAudio);
        }
        PreferencesDialog dialog;
        dialog.findChild<QCheckBox *>(QStringLiteral("autoplayAudio"))->setChecked(false);
        dialog.findChild<QCheckBox *>(QStringLiteral("autoplayVideo"))->setChecked(false);
        dialog.findChild<QComboBox *>(QStringLiteral("defaultMarkdownView"))->setCurrentIndex(1);
        dialog.findChild<QSpinBox *>(QStringLiteral("defaultVolume"))->setValue(35);
        dialog.findChild<QSpinBox *>(QStringLiteral("textLimit"))->setValue(2);
        dialog.findChild<QSpinBox *>(QStringLiteral("imageLimit"))->setValue(4);
        dialog.findChild<QSpinBox *>(QStringLiteral("archiveLimit"))->setValue(500);
        QSignalSpy applied(&dialog, &PreferencesDialog::applied);
        dialog.button(QDialogButtonBox::Apply)->click();
        QCOMPARE(applied.count(), 1);
        const auto p = readPreferences();
        QVERIFY(!p.autoplayAudio);
        QVERIFY(!p.autoplayVideo);
        QVERIFY(p.markdownSource);
        QCOMPARE(p.volume, 35);
        QCOMPARE(p.textMiB, 2);
        QCOMPARE(p.imageMP, 4);
        QCOMPARE(p.archiveEntries, 500);
        QCOMPARE(QSettings().value(QStringLiteral("window/geometry")).toByteArray(),
                 QByteArray("preserved"));
        dialog.findChild<QCheckBox *>(QStringLiteral("autoplayAudio"))->setChecked(true);
        dialog.accept();
        QCOMPARE(applied.count(), 2);
        QVERIFY(readPreferences().autoplayAudio);
        PreviewWindow window(false);
        const QString video = QFINDTESTDATA("data/autoplay.webm");
        window.ShowPreview({video}, {});
        QTRY_VERIFY(window.findChild<QMediaPlayer *>() != nullptr);
        auto *player = window.findChild<QMediaPlayer *>();
        QTRY_VERIFY(player->mediaStatus() == QMediaPlayer::LoadedMedia);
        QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
        QVERIFY(qAbs(player->audioOutput()->volume() - 0.35f) < 0.01f);
        window.HidePreview();
        QSettings().setValue(QStringLiteral("limits/textMiB"), 1000);
        QSettings().setValue(QStringLiteral("limits/imageMP"), -10);
        QSettings().setValue(QStringLiteral("limits/archiveEntries"), 1000000);
        QCOMPARE(readPreferences().textMiB, 16);
        QCOMPARE(readPreferences().imageMP, 1);
        QCOMPARE(readPreferences().archiveEntries, 10000);
    }

    void audioControlsAndLifecycle()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("silent.wav")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray samples(160000, '\0'); // Ten seconds, 8 kHz, mono PCM.
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + samples.size());
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000)
               << quint16(2) << quint16(16);
        stream.writeRawData("data", 4);
        stream << quint32(samples.size());
        QCOMPARE(stream.writeRawData(samples.constData(), samples.size()), samples.size());
        file.close();
        PreviewWindow window(false);
        window.ShowPreview({file.fileName(), directory.filePath(QStringLiteral("missing.png"))},
                           {});
        QTRY_VERIFY(window.findChild<QMediaPlayer *>() != nullptr);
        QPointer<QMediaPlayer> player = window.findChild<QMediaPlayer *>();
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QTRY_VERIFY(player->position() > 0);
        QCOMPARE(window.findChild<QVideoWidget *>(), nullptr);
        QVERIFY(window.findChildren<QPushButton *>().isEmpty());
        auto *surface = window.findChild<QWidget *>(QStringLiteral("previewSurface"));
        QVERIFY(surface);
        QVERIFY(surface->property("mimeType").toString().startsWith(QLatin1String("audio/")));
        auto *controls = window.findChild<QWidget *>(QStringLiteral("audioControls"));
        QVERIFY(controls);
        QVERIFY((controls->geometry().center() - surface->rect().center()).manhattanLength() < 5);
        const auto background = surface->grab().toImage();
        const auto card = controls->geometry().adjusted(-4, -4, 4, 4);
        int watermarkPixels = 0;
        for (int y = 0; y < background.height(); y += 4)
            for (int x = 0; x < background.width(); x += 4)
                if (!card.contains(x, y) &&
                    background.pixelColor(x, y) != surface->palette().color(QPalette::Window))
                    ++watermarkPixels;
        QVERIFY(watermarkPixels > 100);
        auto *play = window.findChild<QToolButton *>(QStringLiteral("mediaPlayPause"));
        QVERIFY(play);
        QCOMPARE(play->accessibleName(), QStringLiteral("Pause"));
        QTest::mouseClick(play, Qt::LeftButton);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QCOMPARE(play->accessibleName(), QStringLiteral("Play"));
        auto *seek = window.findChild<QSlider *>(QStringLiteral("mediaSeek"));
        QVERIFY(seek);
        QTRY_VERIFY(seek->isEnabled());
        QTRY_COMPARE(seek->maximum(), 10);
        seek->setValue(4);
        QTRY_COMPARE(player->position(), qint64(4000));
        auto *time = window.findChild<QLabel *>(QStringLiteral("mediaTime"));
        QVERIFY(time);
        QTRY_COMPARE(time->text(), QStringLiteral("0:04 / 0:10"));
        auto *mute = window.findChild<QToolButton *>(QStringLiteral("mediaMute"));
        QVERIFY(mute);
        QTest::mouseClick(mute, Qt::LeftButton);
        QVERIFY(player->audioOutput()->isMuted());
        auto *volume = window.findChild<QSlider *>(QStringLiteral("mediaVolume"));
        QVERIFY(volume);
        volume->setValue(35);
        QVERIFY(qAbs(player->audioOutput()->volume() - 0.35f) < 0.01f);
        QTest::mouseClick(play, Qt::LeftButton);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QTest::keyClick(&window, Qt::Key_Right);
        QVERIFY(player.isNull());
        QCOMPARE(window.findChild<QWidget *>(QStringLiteral("audioControls")), nullptr);
        surface = window.findChild<QWidget *>(QStringLiteral("previewSurface"));
        QVERIFY(surface);
        QCOMPARE(surface->property("mimeType").toString(), QStringLiteral("image/png"));
        window.HidePreview();
        QCOMPARE(window.findChild<QWidget *>(QStringLiteral("previewSurface")), nullptr);
    }

    void unsupportedFileWatermark()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("unknown.kglanceunknown")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(100, '\0'));
        file.close();
        PreviewWindow window(false);
        window.ShowPreview({file.fileName()}, {});
        QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("previewSurface")) != nullptr);
        auto failed = [&window]
        {
            for (const auto *label : window.findChildren<QLabel *>())
                if (label->text().contains(QStringLiteral("No preview provider")))
                    return true;
            return false;
        };
        QTRY_VERIFY(failed());
        auto *surface = window.findChild<QWidget *>(QStringLiteral("previewSurface"));
        QVERIFY(surface);
        QCOMPARE(surface->property("mimeType").toString(),
                 QStringLiteral("application/octet-stream"));
        window.HidePreview();
    }

    void videoAutoplayAndNavigation()
    {
        const QString fixture = QFINDTESTDATA("data/autoplay.webm");
        QVERIFY(!fixture.isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString next = directory.filePath(QStringLiteral("next.webm"));
        QVERIFY(QFile::copy(fixture, next));
        PreviewWindow window(false);
        window.ShowPreview({fixture, next}, {});
        QTRY_VERIFY(window.findChild<QMediaPlayer *>() != nullptr);
        QPointer<QMediaPlayer> first = window.findChild<QMediaPlayer *>();
        QTRY_COMPARE(first->playbackState(), QMediaPlayer::PlayingState);
        QTRY_VERIFY(first->position() > 0);
        QVERIFY(window.findChildren<QPushButton *>().isEmpty());
        auto *video = window.findChild<QVideoWidget *>();
        QVERIFY(video);
        QTest::mouseClick(video, Qt::RightButton);
        QCOMPARE(first->playbackState(), QMediaPlayer::PlayingState);
        QTest::mouseClick(video, Qt::LeftButton);
        QTRY_COMPARE(first->playbackState(), QMediaPlayer::PausedState);
        QTest::mouseClick(video, Qt::LeftButton);
        QTRY_COMPARE(first->playbackState(), QMediaPlayer::PlayingState);

        QTest::keyClick(&window, Qt::Key_Right);
        QVERIFY(first.isNull());
        QPointer<QMediaPlayer> second = window.findChild<QMediaPlayer *>();
        QVERIFY(second);
        QCOMPARE(second->source(), QUrl::fromLocalFile(next));
        QTRY_COMPARE(second->playbackState(), QMediaPlayer::PlayingState);
        QTRY_VERIFY(second->position() > 0);
        window.HidePreview();
        QVERIFY(second.isNull());
        QCOMPARE(window.findChild<QMediaPlayer *>(), nullptr);
    }

    void selectionArrowNavigation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QStringList paths;
        for (const auto &name : {QStringLiteral("first.cpp"), QStringLiteral("second.cpp")})
        {
            QFile file(directory.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("int value = 1;\n");
            paths.append(file.fileName());
        }
        const QString imagePath = directory.filePath(QStringLiteral("third.png"));
        QImage image(50, 30, QImage::Format_ARGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(imagePath));
        paths.append(imagePath);

        PreviewWindow window(false);
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>() != nullptr);
        auto press = [&window](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
        {
            auto *target = window.focusWidget();
            QVERIFY(target);
            // Navigation destroys the focused viewer during the key press.
            QTest::keyPress(target, key, modifiers);
            QTest::keyRelease(&window, key, modifiers);
        };
        press(Qt::Key_Right, Qt::ShiftModifier);
        QVERIFY(window.windowTitle().contains(QStringLiteral("first.cpp")));
        press(Qt::Key_Right);
        QVERIFY(window.windowTitle().contains(QStringLiteral("second.cpp")));
        QVERIFY(window.findChild<KTextEditor::View *>() != nullptr);
        press(Qt::Key_Right);
        QTRY_VERIFY(window.findChild<QGraphicsView *>() != nullptr);
        QVERIFY(window.windowTitle().contains(QStringLiteral("third.png")));
        press(Qt::Key_Right);
        QVERIFY(window.windowTitle().contains(QStringLiteral("first.cpp")));
        press(Qt::Key_Left);
        QTRY_VERIFY(window.findChild<QGraphicsView *>() != nullptr);
        QVERIFY(window.windowTitle().contains(QStringLiteral("third.png")));
        press(Qt::Key_Left, Qt::AltModifier);
        QVERIFY(window.windowTitle().contains(QStringLiteral("second.cpp")));
        window.HidePreview();

        window.ShowPreview({paths.first()}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>() != nullptr);
        QPointer<KTextEditor::View> singleView = window.findChild<KTextEditor::View *>();
        press(Qt::Key_Right);
        press(Qt::Key_Left);
        QCOMPARE(window.findChild<KTextEditor::View *>(), singleView.data());
        QVERIFY(window.windowTitle().contains(QStringLiteral("first.cpp")));
        window.HidePreview();
    }

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
