// SPDX-License-Identifier: AGPL-3.0-only

#include "filedragbutton.h"
#include "imagecomparison.h"
#include "informationpanel.h"
#include "markdownview.h"
#include "metadataeditor.h"
#include "preferences.h"
#include "previewsurface.h"
#include "previewwindow.h"
#include "providers.h"
#include "remotepreview.h"
#include "selectionfilmstrip.h"
#include "tableview.h"
#ifdef KGLANCEQT_WITH_SQLITE
#include "sqlitepreview.h"
#include <sqlite3.h>
#endif
#include <KColorScheme>
#include <KColorSchemeManager>
#include <KConfigGroup>
#include <KFileMetaData/UserMetaData>
#include <KIconTheme>
#include <KSharedConfig>
#include <KShortcutsDialog>
#include <KStandardShortcut>
#include <KWaylandExtras>
#include <QAccessible>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QScreen>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVBoxLayout>
#include <QWindow>
#include <kcolorscheme_version.h>
#include <kwindowsystem_version.h>

#include <KActionCollection>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KIO/PreviewJob>
#include <KMessageWidget>
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
#include <QDrag>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextTable>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVideoWidget>
#include <QtTest>
#include <baloo/filemetadatawidget.h>

namespace
{
bool writePreviewFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

bool replacePreviewFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
} // namespace

class WindowTests final : public QObject
{
    Q_OBJECT
  public:
    static void initMain() { KIconTheme::initTheme(); }
  private Q_SLOTS:
    void init()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("kglanceqt-tests"));
        QVERIFY(QSettings().fileName().startsWith(QCoreApplication::applicationDirPath()));
        QSettings().clear();
        auto shortcutConfig = KSharedConfig::openConfig(QStringLiteral("kglanceqtshortcutsrc"),
                                                        KConfig::SimpleConfig);
        KConfigGroup(shortcutConfig, QStringLiteral("Shortcuts")).deleteGroup();
        shortcutConfig->sync();
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
        QCOMPARE(quit->shortcuts(), KStandardShortcut::quit());
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

    void fileDragGestureAndUrls()
    {
        FileDragButton button;
        button.show();
        QVERIFY(!button.isEnabled());
        const auto url = QUrl::fromLocalFile(QStringLiteral("/tmp/file with #? ü.txt"));
        button.setFile(url);
        QVERIFY(button.isEnabled());
        QVERIFY(button.toolTip().contains(url.fileName()));
        auto move = [&button](int distance, Qt::MouseButtons buttons = Qt::LeftButton)
        {
            const auto point = button.rect().center() + QPoint(distance, 0);
            QMouseEvent event(QEvent::MouseMove, QPointF(point), QPointF(button.mapToGlobal(point)),
                              Qt::NoButton, buttons, Qt::NoModifier);
            QApplication::sendEvent(&button, &event);
        };
        const int threshold = QApplication::startDragDistance();
        QTest::mouseClick(&button, Qt::LeftButton);
        move(threshold + 1);
        QVERIFY(!button.findChild<QDrag *>());
        QTest::mousePress(&button, Qt::RightButton);
        move(threshold + 1, Qt::RightButton);
        QVERIFY(!button.findChild<QDrag *>());
        QTest::mouseRelease(&button, Qt::RightButton);
        QTest::mousePress(&button, Qt::LeftButton);
        move(qMax(0, threshold - 1));
        QVERIFY(!button.findChild<QDrag *>());
        // Inspect the real native drag while its event loop runs, then cancel it.
        QList<QUrl> urls;
        QByteArray uriList;
        Qt::DropActions actions;
        QTimer::singleShot(0, &button,
                           [&]
                           {
                               if (auto *drag = button.findChild<QDrag *>())
                               {
                                   urls = drag->mimeData()->urls();
                                   uriList =
                                       drag->mimeData()->data(QStringLiteral("text/uri-list"));
                                   actions = drag->supportedActions();
                               }
                               QDrag::cancel();
                           });
        move(threshold + 1);
        QCoreApplication::processEvents();
        QCOMPARE(urls, QList<QUrl>{url});
        QCOMPARE(actions, Qt::CopyAction);
        QVERIFY(uriList.contains("%23%3F"));
        QVERIFY(uriList.contains("%C3%BC"));
        QTest::mouseRelease(&button, Qt::LeftButton);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!button.findChild<QDrag *>());

        // A selection change during a press must not drag the replacement file.
        QTest::mousePress(&button, Qt::LeftButton);
        button.setFile(QUrl::fromLocalFile(QStringLiteral("/tmp/replacement.txt")));
        move(threshold + 1);
        QVERIFY(!button.findChild<QDrag *>());
        QTest::mouseRelease(&button, Qt::LeftButton);
        button.setFile(QUrl(QStringLiteral("sftp://example.test/file")));
        QVERIFY(!button.isEnabled());
        button.setFile({});
        QVERIFY(!button.isEnabled());
    }

    void fileDragFollowsCurrentFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("current.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("preview\n");
        file.close();
        const QStringList paths{path, directory.filePath(QStringLiteral("missing.txt")),
                                QStringLiteral("sftp://example.test/remote.txt")};
        PreviewWindow window(false);
        auto *button = window.findChild<FileDragButton *>();
        QVERIFY(button);
        QVERIFY(!button->isEnabled());
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        QVERIFY(button->isEnabled());
        QVERIFY(button->toolTip().contains(QStringLiteral("current.txt")));
        QTest::keyClick(&window, Qt::Key_Right);
        QVERIFY(!button->isEnabled());
        QTest::keyClick(&window, Qt::Key_Right);
        QVERIFY(!button->isEnabled());
        QTest::keyClick(&window, Qt::Key_Right);
        QVERIFY(button->isEnabled());
        window.HidePreview();
        QVERIFY(!button->isEnabled());
        window.ShowPreview({directory.path()}, {});
        QTRY_VERIFY(button->isEnabled());
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

    void animatedImagePlaybackAndCleanup()
    {
        const QString path = QFINDTESTDATA("data/animation.gif");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString next = directory.filePath(QStringLiteral("next.png"));
        QImage still(32, 24, QImage::Format_RGB32);
        still.fill(Qt::yellow);
        QVERIFY(still.save(next));
        const QStringList paths{path, next};
        PreviewWindow window(false);
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<ImageView *>());
        QPointer<ImageView> image = window.findChild<ImageView *>();
        QVERIFY(image->isAnimated());
        QVERIFY(image->isPlaying());
        auto *playback = window.findChild<QAction *>(QStringLiteral("animationPlayback"));
        QVERIFY(playback);
        QVERIFY(playback->isVisible());
        QCOMPARE(playback->text(), QStringLiteral("Pause animation"));
        QSignalSpy frames(image, &ImageView::frameChanged);
        QTRY_VERIFY(!frames.isEmpty());
        playback->trigger();
        QVERIFY(!image->isPlaying());
        QCOMPARE(playback->text(), QStringLiteral("Play animation"));
        const int paused = image->currentFrame();
        const int count = frames.count();
        QTest::qWait(300);
        QCOMPARE(image->currentFrame(), paused);
        QCOMPARE(frames.count(), count);
        ImageViewState zoom;
        zoom.fitted = false;
        zoom.transform.scale(3, 3);
        zoom.center = QPointF(12, 8);
        image->restoreViewState(zoom);
        const auto saved = image->viewState();
        playback->trigger();
        QVERIFY(image->isPlaying());
        QTRY_VERIFY(frames.count() > count);
        QCOMPARE(image->viewState().transform, saved.transform);
        QVERIFY(!image->viewState().fitted);

        QTest::keyClick(&window, Qt::Key_Right);
        QVERIFY(image.isNull());
        QVERIFY(!playback->isVisible());
        QTRY_VERIFY(window.findChild<ImageView *>());
        QVERIFY(!window.findChild<ImageView *>()->isAnimated());
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_VERIFY(window.findChild<ImageView *>());
        image = window.findChild<ImageView *>();
        QVERIFY(image->isAnimated());
        QTRY_COMPARE(image->viewState().transform, saved.transform);
        window.HidePreview();
        QVERIFY(image.isNull());
        QVERIFY(!playback->isVisible());
        QCOMPARE(window.findChild<ImageView *>(), nullptr);
        // A late decode must not resurrect content after navigation or hide.
        window.ShowPreview(paths, {});
        QCoreApplication::processEvents();
        QTest::keyClick(&window, Qt::Key_Right);
        QTRY_VERIFY(window.findChild<ImageView *>());
        QVERIFY(!window.findChild<ImageView *>()->isAnimated());
        QVERIFY(!playback->isVisible());
        window.HidePreview();
        QCoreApplication::processEvents();
        QCOMPARE(window.findChild<ImageView *>(), nullptr);
    }

    void animationFiniteLoopsAndReplay()
    {
        const auto decoded = loadImagePreview(QFINDTESTDATA("data/animation-once.gif"));
        QVERIFY(decoded.animation);
        ImageView image(decoded.image);
        image.show();
        auto animation = *decoded.animation;
        animation.loops = 1;
        QSignalSpy frames(&image, &ImageView::frameChanged);
        image.setAnimation(animation, false);
        QVERIFY(!image.isPlaying());
        QCOMPARE(image.currentFrame(), 0);
        frames.clear();
        image.toggleAnimation();
        QTRY_VERIFY(!image.isPlaying());
        // Two passes: 1, 2, 0, 1, 2. Playback stops on the last frame.
        QCOMPARE(frames.count(), 5);
        QCOMPARE(image.currentFrame(), 2);
        image.toggleAnimation();
        QVERIFY(image.isPlaying());
        QCOMPARE(image.currentFrame(), 0);
        QTRY_VERIFY(!image.isPlaying());
        QCOMPARE(image.currentFrame(), 2);
        QCOMPARE(frames.count(), 11);
    }

    void animationPreferencesAndLimitNotice()
    {
        PreviewPreferences settings;
        settings.autoplayAnimation = false;
        settings.animationFrames = 2;
        settings.animationMiB = 3;
        QVERIFY(savePreferences(settings));
        QCOMPARE(readPreferences().animationFrames, 2);
        QCOMPARE(readPreferences().animationMiB, 3);
        QVERIFY(!readPreferences().autoplayAnimation);
        PreferencesDialog dialog;
        auto *autoplay = dialog.findChild<QCheckBox *>(QStringLiteral("autoplayAnimation"));
        auto *frames = dialog.findChild<QSpinBox *>(QStringLiteral("animationFrameLimit"));
        auto *memory = dialog.findChild<QSpinBox *>(QStringLiteral("animationMemoryLimit"));
        QVERIFY(autoplay);
        QVERIFY(frames);
        QVERIFY(memory);
        QVERIFY(!autoplay->isChecked());
        QCOMPARE(frames->value(), 2);
        QCOMPARE(memory->value(), 3);
        const QString path = QFINDTESTDATA("data/animation.gif");
        PreviewWindow window(false);
        window.ShowPreview({path}, {});
        QTRY_VERIFY(window.findChild<ImageView *>());
        QVERIFY(!window.findChild<ImageView *>()->isAnimated());
        auto *playback = window.findChild<QAction *>(QStringLiteral("animationPlayback"));
        QVERIFY(!playback->isVisible());
        bool explained = false;
        for (const auto *label : window.findChildren<QLabel *>())
            explained |= label->text().contains(QStringLiteral("2-frame preview limit"));
        QVERIFY(explained);
        window.HidePreview();
        frames->setValue(256);
        dialog.button(QDialogButtonBox::Apply)->click();
        window.ShowPreview({path}, {});
        QTRY_VERIFY(window.findChild<ImageView *>());
        auto *image = window.findChild<ImageView *>();
        QVERIFY(image->isAnimated());
        QVERIFY(!image->isPlaying());
        QVERIFY(playback->isVisible());
        QCOMPARE(playback->text(), QStringLiteral("Play animation"));
        playback->trigger();
        QVERIFY(image->isPlaying());
        window.HidePreview();
        settings.animationFrames = 9999;
        settings.animationMiB = -5;
        QVERIFY(savePreferences(settings));
        QCOMPARE(readPreferences().animationFrames, MaxAnimationFrames);
        QCOMPARE(readPreferences().animationMiB, 1);
    }

    void selectionFilmstripNavigationAndLifecycle()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QStringList paths;
        for (const auto &name : {QStringLiteral("first.txt"), QStringLiteral("second.txt"),
                                 QStringLiteral("third.txt")})
        {
            QFile file(directory.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("preview\n");
            paths.append(file.fileName());
        }
        PreviewWindow window(false);
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        auto *strip = window.findChild<SelectionFilmstrip *>();
        auto *toggle = window.findChild<QAction *>(QStringLiteral("showFilmstrip"));
        QVERIFY(strip);
        QVERIFY(toggle);
        QVERIFY(!strip->isVisible());
        toggle->trigger();
        QVERIFY(strip->isVisible());
        QVERIFY(readPreferences().filmstrip);
        QCOMPARE(strip->model()->rowCount(), 3);
        QCOMPARE(strip->currentIndex().row(), 0);
        QCOMPARE(strip->model()->index(1, 0).data().toString(), QStringLiteral("second.txt"));
        QCOMPARE(strip->model()->index(1, 0).data(Qt::ToolTipRole).toString(), paths.at(1));
        QTRY_VERIFY(!strip->visualRect(strip->model()->index(2, 0)).isEmpty());
        QTest::mouseClick(strip->viewport(), Qt::LeftButton, Qt::NoModifier,
                          strip->visualRect(strip->model()->index(2, 0)).center());
        QTRY_VERIFY(window.windowTitle().contains(QStringLiteral("third.txt")));
        QCOMPARE(strip->currentIndex().row(), 2);
        QTest::keyClick(strip, Qt::Key_Right);
        QTRY_VERIFY(window.windowTitle().contains(QStringLiteral("first.txt")));
        QCOMPARE(strip->currentIndex().row(), 0);

        // Replacing a live selection resets both the model and the current item.
        window.ShowPreview({paths.at(1), paths.at(2)}, {});
        QTRY_VERIFY(window.windowTitle().contains(QStringLiteral("second.txt")));
        QCOMPARE(strip->model()->rowCount(), 2);
        QCOMPARE(strip->currentIndex().row(), 0);
        window.HidePreview();
        QCOMPARE(strip->model()->rowCount(), 0);
        QVERIFY(!strip->isVisible());
        window.ShowPreview({paths.first()}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        QVERIFY(!strip->isVisible());
        window.ShowPreview(paths, {});
        QTRY_VERIFY(strip->isVisible());
        auto *configure = window.findChild<QAction *>(QStringLiteral("configureKGlanceQT"));
        configure->trigger();
        auto *dialog = window.findChild<PreferencesDialog *>();
        QVERIFY(dialog);
        auto *enabled = dialog->findChild<QCheckBox *>(QStringLiteral("defaultFilmstrip"));
        QVERIFY(enabled);
        QVERIFY(enabled->isChecked());
        enabled->setChecked(false);
        dialog->button(QDialogButtonBox::Apply)->click();
        QVERIFY(!strip->isVisible());
        QVERIFY(!toggle->isChecked());
        QVERIFY(!readPreferences().filmstrip);
        window.HidePreview();
    }

    void filmstripThumbnailsAndReplacement()
    {
        if (!KIO::PreviewJob::availablePlugins().contains(QStringLiteral("imagethumbnail")))
            QSKIP("The optional KDE image thumbnailer is not installed.");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QStringList paths;
        for (int i = 0; i < 40; ++i)
        {
            QImage image(100, 60, QImage::Format_RGB32);
            image.fill(i == 39 ? Qt::blue : Qt::red);
            const auto path = directory.filePath(QString::number(i) + QStringLiteral(".png"));
            QVERIFY(image.save(path));
            paths.append(path);
        }
        SelectionFilmstrip strip;
        strip.resize(600, 120);
        strip.setPaths(paths);
        strip.setCurrent(0);
        strip.show();
        auto color = [&strip](int row)
        {
            const auto icon =
                qvariant_cast<QIcon>(strip.model()->index(row, 0).data(Qt::DecorationRole));
            const auto image = icon.pixmap(80, 64).toImage();
            return image.isNull() ? QColor{} : image.pixelColor(image.rect().center());
        };
        QTRY_COMPARE_WITH_TIMEOUT(color(0), QColor(Qt::red), 10000);
        strip.setCurrent(39);
        // Replace the selection while the scroll's next thumbnail request is pending.
        strip.setPaths({paths.last(), paths.last()});
        strip.setCurrent(0);
        QTRY_COMPARE_WITH_TIMEOUT(color(0), QColor(Qt::blue), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(color(1), QColor(Qt::blue), 10000);
        strip.hide();
        strip.clear();
        QCOMPARE(strip.model()->rowCount(), 0);
        strip.setPaths({paths.first(), paths.at(1)});
        strip.setCurrent(0);
        strip.show();
        QTRY_COMPARE_WITH_TIMEOUT(color(0), QColor(Qt::red), 10000);
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

    void textPositionMemoryAndClear()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QStringList paths;
        for (int i = 0; i < 2; ++i)
        {
            QFile file(directory.filePath(QString::number(i) + QStringLiteral(".txt")));
            QVERIFY(file.open(QIODevice::WriteOnly));
            for (int line = 0; line < 500; ++line)
                file.write("A line of text for reading position tests.\n");
            paths.append(file.fileName());
        }
        PreviewWindow window(false);
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        auto *view = window.findChild<KTextEditor::View *>();
        view->setCursorPosition(KTextEditor::Cursor(140, 3));
        view->setScrollPosition(KTextEditor::Cursor(130, 0));
        QTRY_COMPARE(view->firstDisplayedLine(), 130);
        const auto saved = textPosition(view);
        QTest::keyClick(&window, Qt::Key_Right);
        view = window.findChild<KTextEditor::View *>();
        QVERIFY(view);
        QCOMPARE(view->firstDisplayedLine(), 0);
        QTest::keyClick(&window, Qt::Key_Left);
        view = window.findChild<KTextEditor::View *>();
        QTRY_COMPARE(view->cursorPosition(), saved.cursor);
        QTRY_COMPARE(view->firstDisplayedLine(), saved.firstLine);

        // A second navigation before deferred restoration must retain the saved state.
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Left);
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Left);
        view = window.findChild<KTextEditor::View *>();
        QTRY_COMPARE(view->firstDisplayedLine(), saved.firstLine);
        auto *clear = window.findChild<QAction *>(QStringLiteral("clearPositions"));
        QVERIFY(clear);
        clear->trigger();
        view = window.findChild<KTextEditor::View *>();
        QCOMPARE(view->firstDisplayedLine(), 0);
        QCOMPARE(view->cursorPosition(), KTextEditor::Cursor(0, 0));
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Left);
        view = window.findChild<KTextEditor::View *>();
        QTRY_COMPARE(view->firstDisplayedLine(), 0);
        view->setScrollPosition(KTextEditor::Cursor(100, 0));
        window.HidePreview();
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        QCOMPARE(window.findChild<KTextEditor::View *>()->firstDisplayedLine(), 0);
        window.HidePreview();
    }

    void fileRefreshPreservesTextAndRecoversMissingFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("reading.txt"));
        const QByteArray body = QByteArray("A line of text to scroll through.\n").repeated(500);
        QVERIFY(writePreviewFile(path, "original\n" + body));
        PreviewWindow window(false);
        window.ShowPreview({path}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        QPointer<KTextEditor::View> old = window.findChild<KTextEditor::View *>();
        old->setCursorPosition(KTextEditor::Cursor(150, 4));
        old->setScrollPosition(KTextEditor::Cursor(140, 0));
        QTRY_COMPARE(old->firstDisplayedLine(), 140);
        const auto saved = textPosition(old);
        QVERIFY(writePreviewFile(path, "updated\n" + body));
        QTRY_VERIFY(old.isNull());
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        auto *view = window.findChild<KTextEditor::View *>();
        QVERIFY(view->document()->text().startsWith(QStringLiteral("updated\n")));
        QTRY_COMPARE(view->cursorPosition(), saved.cursor);
        QTRY_COMPARE(view->firstDisplayedLine(), saved.firstLine);
        auto *notice = window.findChild<KMessageWidget *>(QStringLiteral("fileChangeNotice"));
        QVERIFY(notice);
        QVERIFY(notice->isVisible());
        QVERIFY(notice->text().contains(QStringLiteral("changed on disk")));
        old = view;
        QVERIFY(replacePreviewFile(path, "replaced\n" + body));
        QTRY_VERIFY(old.isNull());
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        QVERIFY(window.findChild<KTextEditor::View *>()->document()->text().startsWith(
            QStringLiteral("replaced\n")));
        QVERIFY(notice->text().contains(QStringLiteral("replaced on disk")));
        QVERIFY(QFile::remove(path));
        QTRY_COMPARE(window.findChild<KTextEditor::View *>(), nullptr);
        QVERIFY(notice->text().contains(QStringLiteral("removed or moved")));
        auto *drag = window.findChild<FileDragButton *>();
        QVERIFY(!drag->isEnabled());
        QVERIFY(writePreviewFile(path, "shorter\nrestored"));
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        view = window.findChild<KTextEditor::View *>();
        QVERIFY(view->document()->text().startsWith(QStringLiteral("shorter\n")));
        QVERIFY(notice->text().contains(QStringLiteral("available again")));
        QTRY_VERIFY(view->cursorPosition().line() < view->document()->lines());
        QVERIFY(drag->isEnabled());
        window.HidePreview();
    }

    void fileRefreshUpdatesImageAndThumbnail()
    {
        if (!KIO::PreviewJob::availablePlugins().contains(QStringLiteral("imagethumbnail")))
            QSKIP("The optional KDE image thumbnailer is not installed.");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("current.png"));
        const auto next = directory.filePath(QStringLiteral("next.png"));
        QImage source(1200, 900, QImage::Format_RGB32);
        source.fill(Qt::green);
        QVERIFY(source.save(path));
        QVERIFY(source.save(next));
        QSettings().setValue(QStringLiteral("window/filmstrip"), true);
        PreviewWindow window(false);
        window.ShowPreview({path, next}, {});
        QTRY_VERIFY(window.findChild<ImageView *>());
        QPointer<ImageView> old = window.findChild<ImageView *>();
        ImageViewState state;
        state.fitted = false;
        state.transform.scale(2, 2);
        state.center = QPointF(700, 500);
        old->restoreViewState(state);
        const auto saved = old->viewState();
        auto *strip = window.findChild<SelectionFilmstrip *>();
        auto thumbnailColor = [strip]
        {
            const auto icon =
                qvariant_cast<QIcon>(strip->model()->index(0, 0).data(Qt::DecorationRole));
            const auto image = icon.pixmap(80, 64).toImage();
            return image.isNull() ? QColor{} : image.pixelColor(image.rect().center());
        };
        QTRY_COMPARE_WITH_TIMEOUT(thumbnailColor(), QColor(Qt::green), 10000);
        source.fill(Qt::red);
        QSaveFile replacement(path);
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        QVERIFY(source.save(&replacement, "PNG"));
        QVERIFY(replacement.commit());
        QTRY_VERIFY(old.isNull());
        QTRY_VERIFY(window.findChild<ImageView *>());
        auto *image = window.findChild<ImageView *>();
        QTRY_COMPARE(image->viewState().transform, saved.transform);
        QTRY_VERIFY(QLineF(image->viewState().center, saved.center).length() < 2);
        auto *pixmap = qgraphicsitem_cast<QGraphicsPixmapItem *>(image->scene()->items().first());
        QVERIFY(pixmap);
        QCOMPARE(pixmap->pixmap().toImage().pixelColor(0, 0), QColor(Qt::red));
        QTRY_COMPARE_WITH_TIMEOUT(thumbnailColor(), QColor(Qt::red), 10000);
        QCOMPARE(strip->currentIndex().row(), 0);
        // An event already queued for this file must not reload the next item.
        source.fill(Qt::blue);
        QVERIFY(source.save(path));
        QCoreApplication::processEvents();
        QTest::keyClick(&window, Qt::Key_Right);
        QTRY_VERIFY(window.findChild<ImageView *>());
        QPointer<ImageView> nextImage = window.findChild<ImageView *>();
        QTest::qWait(400);
        QVERIFY(nextImage);
        QVERIFY(window.windowTitle().contains(QStringLiteral("next.png")));
        window.HidePreview();
        auto *monitor = window.findChild<FileMonitor *>();
        QVERIFY(monitor);
        auto *watcher = monitor->findChild<QFileSystemWatcher *>();
        QVERIFY(watcher->files().isEmpty());
        QVERIFY(watcher->directories().isEmpty());
        QVERIFY(source.save(next));
        QTest::qWait(400);
        QCOMPARE(window.findChild<ImageView *>(), nullptr);
        QVERIFY(!window.IsVisible());
    }

    void fileRefreshKeepsPausedAnimationFrame()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile fixture(QFINDTESTDATA("data/animation.gif"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto data = fixture.readAll();
        const auto path = directory.filePath(QStringLiteral("animation.gif"));
        QVERIFY(writePreviewFile(path, data));
        PreviewWindow window(false);
        window.ShowPreview({path}, {});
        QTRY_VERIFY(window.findChild<ImageView *>());
        QPointer<ImageView> old = window.findChild<ImageView *>();
        QTRY_VERIFY(old->currentFrame() > 0);
        old->toggleAnimation();
        QVERIFY(!old->isPlaying());
        const auto frame = old->currentFrame();
        QVERIFY(replacePreviewFile(path, data));
        QTRY_VERIFY(old.isNull());
        QTRY_VERIFY(window.findChild<ImageView *>());
        auto *image = window.findChild<ImageView *>();
        QTRY_VERIFY(!image->isPlaying());
        QCOMPARE(image->currentFrame(), frame);
        window.HidePreview();
    }

    void nativeWaylandLifecycle()
    {
        if (!qEnvironmentVariableIsSet("KGLANCEQT_NATIVE_DISPLAY_TEST"))
            QSKIP("Run tests/wayland_smoke.py for native compositor validation");
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("wayland"));
        const auto screens = QGuiApplication::screens();
        QVERIFY(!screens.isEmpty());
        for (auto *screen : screens)
            qInfo() << "OUTPUT" << screen->name() << screen->geometry()
                    << screen->devicePixelRatio();
        QGuiApplication::setDesktopFileName(QStringLiteral("org.kglanceqt.KGlanceQT"));
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("native.png"));
        QImage pixels(800, 600, QImage::Format_RGB32);
        pixels.fill(QColor("#299bdc"));
        QVERIFY(pixels.save(path));
        PreviewWindow window(false);
        const QStringList paths{path, directory.filePath(QStringLiteral("missing.txt"))};
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<ImageView *>());
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto bus = QDBusConnection::sessionBus();
        const auto owner = bus.interface()->servicePid(QStringLiteral("org.kde.KWin"));
        QVERIFY(owner.isValid());
        QCOMPARE(owner.value(), qEnvironmentVariableIntValue("KGLANCEQT_KWIN_PID"));
        auto activate = [&](const QString &output)
        {
            const auto scriptPath = directory.filePath(QStringLiteral("activate.js"));
            const QString script =
                QStringLiteral("for (const w of workspace.stackingOrder) { if(w.pid === %1) { "
                               "workspace.activeWindow=w; "
                               "const output=workspace.screens.find(s=>s.name===%2); if(output) "
                               "workspace.sendClientToScreen(w,output); } }")
                    .arg(QCoreApplication::applicationPid())
                    .arg(QString::fromUtf8(
                             QJsonDocument(QJsonArray{output}).toJson(QJsonDocument::Compact))
                             .mid(1)
                             .chopped(1));
            if (!writePreviewFile(scriptPath, script.toUtf8()))
                return false;
            QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                                     QStringLiteral("org.kde.kwin.Scripting"), bus);
            const QDBusReply<int> loaded = scripting.call(
                QStringLiteral("loadScript"), scriptPath,
                QStringLiteral("kglanceqt-validation-%1").arg(QCoreApplication::applicationPid()));
            if (!loaded.isValid() || loaded.value() < 0)
                return false;
            QDBusInterface instance(QStringLiteral("org.kde.KWin"),
                                    QStringLiteral("/Scripting/Script%1").arg(loaded.value()),
                                    QStringLiteral("org.kde.kwin.Script"), bus);
            const auto reply = instance.call(QStringLiteral("run"));
            scripting.call(
                QStringLiteral("unloadScript"),
                QStringLiteral("kglanceqt-validation-%1").arg(QCoreApplication::applicationPid()));
            return reply.type() != QDBusMessage::ErrorMessage;
        };
        QVERIFY(activate(screens.first()->name()));
        QTRY_VERIFY(window.isActiveWindow());
        for (auto *screen : screens)
        {
            QVERIFY(activate(screen->name()));
            QTRY_COMPARE(window.windowHandle()->screen()->name(), screen->name());
            QTRY_VERIFY(window.windowHandle()->isExposed());
            QVERIFY(window.windowHandle()->devicePixelRatio() >= 1.0);
            QVERIFY(!window.windowIcon().pixmap(64).isNull());
            qInfo() << "PREVIEW" << screen->name() << window.size() << window.devicePixelRatioF();
        }
#if KWINDOWSYSTEM_VERSION >= QT_VERSION_CHECK(6, 19, 0)
        QFutureWatcher<QString> token;
        token.setFuture(KWaylandExtras::xdgActivationToken(
            window.windowHandle(), QStringLiteral("org.kglanceqt.KGlanceQT")));
        QTRY_VERIFY(token.isFinished());
        QVERIFY(!token.result().isEmpty());
        window.HidePreview();
        QVERIFY(!window.IsVisible());
        window.ShowPreview(paths, token.result());
        QTRY_VERIFY(window.findChild<ImageView *>());
        QTRY_VERIFY(window.isActiveWindow());
        qInfo() << "Native xdg-activation token restored focus";
#endif
        window.findChild<QAction *>(QStringLiteral("nextFile"))->trigger();
        QTRY_VERIFY(window.windowTitle().startsWith(QStringLiteral("missing.txt")));
#if KCOLORSCHEME_VERSION >= QT_VERSION_CHECK(6, 27, 0)
        auto *schemes = KColorSchemeManager::instance();
        schemes->setAutosaveChanges(false);
        const auto originalScheme = schemes->activeSchemeId();
        const auto originalPalette = qApp->palette();
        for (const auto &scheme : {QStringLiteral("BreezeLight"), QStringLiteral("BreezeDark")})
        {
            schemes->activateSchemeId(scheme);
            auto *surface = window.findChild<QWidget *>(QStringLiteral("previewSurface"));
            QVERIFY(surface);
            QTRY_COMPARE(surface->palette().color(QPalette::Window),
                         qApp->palette().color(QPalette::Window));
            QVERIFY(!window.grab().isNull());
            qInfo() << "Native theme" << scheme << surface->palette().color(QPalette::Window);
            if (!qEnvironmentVariable("KGLANCEQT_TEST_SCREENSHOTS").isEmpty())
            {
                QDir output(qEnvironmentVariable("KGLANCEQT_TEST_SCREENSHOTS"));
                output.mkpath(QStringLiteral("."));
                QVERIFY(window.grab().save(
                    output.filePath(QStringLiteral("native-%1.png").arg(scheme))));
            }
        }
        schemes->activateSchemeId(originalScheme);
        qApp->setPalette(originalPalette);
#endif
        window.findChild<QAction *>(QStringLiteral("previousFile"))->trigger();
        QTRY_VERIFY(window.findChild<ImageView *>());
        window.ShowPreview(paths, {});
        QVERIFY(!window.IsVisible());
    }

    void nativeShortcutConfigurationAndRefresh()
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("kglanceqtshortcutsrc"),
                                                KConfig::SimpleConfig);
        KConfigGroup group(config, QStringLiteral("Shortcuts"));
        group.writeEntry("refreshPreview", QStringLiteral("F6"));
        config->sync();
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("refresh.txt"));
        QVERIFY(writePreviewFile(path, "content\n"));
        PreviewWindow window(false);
        window.ShowPreview({path}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        QVERIFY(!window.windowIcon().isNull());
        auto *collection = window.findChild<KActionCollection *>(QStringLiteral("shortcutActions"));
        QVERIFY(collection);
        auto *refresh = collection->action(QStringLiteral("refreshPreview"));
        QCOMPARE(refresh->shortcut(), QKeySequence(Qt::Key_F6));
        QCOMPARE(collection->action(QStringLiteral("togglePreview"))->shortcut(),
                 QKeySequence(Qt::Key_Space));
        QVERIFY(!KActionCollection::isShortcutsConfigurable(
            collection->action(QStringLiteral("togglePreview"))));
        QPointer<KTextEditor::View> old = window.findChild<KTextEditor::View *>();
        QTest::keyClick(old, Qt::Key_F6);
        QTRY_VERIFY(old.isNull());
        bool opened = false;
        QTimer::singleShot(0, &window,
                           [&]
                           {
                               auto *dialog = window.findChild<KShortcutsDialog *>(
                                   QStringLiteral("applicationShortcutsDialog"));
                               opened = dialog && !dialog->actionCollections().isEmpty();
                               if (dialog)
                                   dialog->reject();
                           });
        collection->action(QStringLiteral("configureShortcuts"))->trigger();
        QVERIFY(opened);
        QCOMPARE(refresh->shortcut(), QKeySequence(Qt::Key_F6));
        window.HidePreview();
    }

    void tableReadingPositionMemory()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("positions.csv"));
        QByteArray data;
        for (int row = 0; row < 500; ++row)
        {
            for (int col = 0; col < 16; ++col)
            {
                if (col)
                    data += ',';
                data += QByteArray("row ") + QByteArray::number(row) + " column " +
                        QByteArray::number(col);
            }
            data += '\n';
        }
        QVERIFY(writePreviewFile(path, data));
        PreviewWindow window(false);
        window.ShowPreview({path, directory.filePath(QStringLiteral("missing.txt"))}, {});
        QTRY_VERIFY(window.findChild<TableView *>());
        auto *table = window.findChild<TableView *>();
        auto *cells = table->findChild<QTableView *>();
        QTRY_VERIFY(table->position().has_value());
        auto *search = table->findChild<QLineEdit *>();
        search->setText(QStringLiteral("row"));
        cells->sortByColumn(2, Qt::DescendingOrder);
        cells->setCurrentIndex(cells->model()->index(100, 3));
        QTRY_VERIFY(cells->verticalScrollBar()->maximum() > 120);
        cells->verticalScrollBar()->setValue(120);
        cells->horizontalScrollBar()->setValue(150);
        const auto saved = *table->position();
        window.findChild<QAction *>(QStringLiteral("nextFile"))->trigger();
        QTRY_VERIFY(!window.findChild<TableView *>());
        window.findChild<QAction *>(QStringLiteral("previousFile"))->trigger();
        QTRY_VERIFY(window.findChild<TableView *>());
        table = window.findChild<TableView *>();
        cells = table->findChild<QTableView *>();
        QTRY_VERIFY(table->position().has_value());
        QTRY_COMPARE(table->position()->query, saved.query);
        QTRY_COMPARE(cells->verticalScrollBar()->value(), saved.vertical);
        QCOMPARE(cells->horizontalScrollBar()->value(), saved.horizontal);
        QCOMPARE(table->position()->sortColumn, saved.sortColumn);
        QCOMPARE(table->position()->sortOrder, saved.sortOrder);
        QCOMPARE(cells->currentIndex().row(), saved.currentRow);
        window.findChild<QAction *>(QStringLiteral("clearPositions"))->trigger();
        QTRY_VERIFY(window.findChild<TableView *>());
        table = window.findChild<TableView *>();
        QTRY_VERIFY(table->position().has_value());
        QTRY_COMPARE(table->position()->query, QString{});
        window.HidePreview();
    }

    void accessibleTableInputAndThemeChanges()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("accessible.csv"));
        QVERIFY(writePreviewFile(path, "two words,10\nother row,20\n"));
        PreviewWindow window(false);
        window.ShowPreview({path, directory.filePath(QStringLiteral("missing.txt"))}, {});
        QTRY_VERIFY(window.findChild<TableView *>());
        auto *table = window.findChild<TableView *>();
        QTRY_VERIFY(table->position().has_value());
        auto *search = table->findChild<QLineEdit *>();
        search->setFocus();
        QTRY_VERIFY(search->hasFocus());
        QTest::keyClicks(search, "two words");
        QCOMPARE(search->text(), QStringLiteral("two words"));
        QVERIFY(window.IsVisible());
        auto *cells = table->findChild<QTableView *>();
        QTRY_COMPARE(cells->model()->rowCount(), 1);
        cells->setCurrentIndex(cells->model()->index(0, 0));
        cells->setFocus();
        QTest::keyClick(cells, Qt::Key_Right);
        QCOMPARE(cells->currentIndex().column(), 1);
        QVERIFY(window.findChild<TableView *>() == table);
        auto *interface = QAccessible::queryAccessibleInterface(search);
        QVERIFY(interface);
        QCOMPARE(interface->role(), QAccessible::EditableText);
        QCOMPARE(interface->text(QAccessible::Name), QStringLiteral("Search table cells"));
        QVERIFY(!QAccessible::queryAccessibleInterface(cells)->text(QAccessible::Name).isEmpty());
        const auto original = qApp->palette();
#if KCOLORSCHEME_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        auto *schemes = KColorSchemeManager::instance();
#else
        KColorSchemeManager localSchemes;
        auto *schemes = &localSchemes;
#endif
        schemes->setAutosaveChanges(false);
        const auto restorePalette = qScopeGuard(
            [original, schemes]
            {
                schemes->activateScheme(QModelIndex{});
                qApp->setPalette(original);
            });
        QList<QPalette> palettes;
        for (const auto &name : {QStringLiteral("BreezeLight"), QStringLiteral("BreezeDark")})
        {
            const auto path =
                QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                       QStringLiteral("color-schemes/%1.colors").arg(name));
            if (!path.isEmpty())
                palettes.append(KColorScheme::createApplicationPalette(
                    KSharedConfig::openConfig(path, KConfig::SimpleConfig)));
            else
            {
                auto palette = original;
                if (name == QLatin1String("BreezeDark"))
                {
                    for (auto role : {QPalette::Window, QPalette::Base, QPalette::Button})
                        palette.setColor(role, QColor("#232629"));
                    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
                        palette.setColor(role, QColor("#eff0f1"));
                }
                palettes.append(palette);
            }
        }
        auto custom = palettes.last();
        for (auto role : {QPalette::Window, QPalette::Base, QPalette::Button})
            custom.setColor(role, QColor("#000040"));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            custom.setColor(role, QColor("#ffff80"));
        palettes.append(custom);
        PreviewSurface surface(QMimeDatabase().mimeTypeForName(QStringLiteral("text/plain")));
        surface.resize(420, 240);
        auto *layout = new QVBoxLayout(&surface);
        auto *label = new QLabel(QStringLiteral("Readable preview message"), &surface);
        layout->addWidget(label);
        surface.show();
        for (int i = 0; i < palettes.size(); ++i)
        {
            if (i < 2)
            {
#if KCOLORSCHEME_VERSION >= QT_VERSION_CHECK(6, 27, 0)
                schemes->activateSchemeId(i == 0 ? QStringLiteral("BreezeLight")
                                                 : QStringLiteral("BreezeDark"));
#else
                schemes->activateScheme(schemes->indexForScheme(
                    i == 0 ? QStringLiteral("Breeze Light") : QStringLiteral("Breeze Dark")));
#endif
            }
            const auto palette = palettes.at(i);
            qApp->setPalette(palette);
            QTRY_COMPARE(surface.palette().color(QPalette::Window),
                         palette.color(QPalette::Window));
            QCOMPARE(label->palette().color(QPalette::WindowText),
                     palette.color(QPalette::WindowText));
            if (!qEnvironmentVariable("KGLANCEQT_TEST_SCREENSHOTS").isEmpty())
            {
                QDir output(qEnvironmentVariable("KGLANCEQT_TEST_SCREENSHOTS"));
                output.mkpath(QStringLiteral("."));
                QVERIFY(
                    surface.grab().save(output.filePath(QStringLiteral("surface-%1.png").arg(i))));
                QVERIFY(window.grab().save(output.filePath(QStringLiteral("table-%1.png").arg(i))));
            }
        }
        qApp->setPalette(original);
        surface.close();
        window.HidePreview();
    }

    void boundedTablesAndCellInteraction()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("data.csv"));
        QVERIFY(replacePreviewFile(path, "name,value\r\n\"multi\nline\",10\r\n\"a\"\"b\",2\r\n"));
        auto parsed = readTable(path, QLatin1Char(','));
        QVERIFY(parsed.error.isEmpty());
        QCOMPARE(parsed.rows.size(), 3);
        QCOMPARE(parsed.rows[1][0], QStringLiteral("multi\nline"));
        QCOMPARE(parsed.rows[2][0], QStringLiteral("a\"b"));
        auto limited = readTable(path, QLatin1Char(','), {}, 1024, 2, 1, 100);
        QCOMPARE(limited.rows.size(), 2);
        QCOMPARE(limited.columns, 1);
        QVERIFY(!limited.notice.isEmpty());
        auto byteLimited = readTable(path, QLatin1Char(','), {}, 12);
        QVERIFY(!byteLimited.notice.isEmpty());
        QCOMPARE(byteLimited.rows.size(), 1);
        std::stop_source cancelled;
        cancelled.request_stop();
        QVERIFY(!readTable(path, QLatin1Char(','), cancelled.get_token()).error.isEmpty());
        QVERIFY(replacePreviewFile(path, "\"unclosed"));
        QVERIFY(!readTable(path, QLatin1Char(',')).error.isEmpty());
        QVERIFY(replacePreviewFile(path, "a\t2\n\"b\tc\"\t10\n"));
        QCOMPARE(readTable(path, QLatin1Char('\t')).rows[1][0], QStringLiteral("b\tc"));
        QVERIFY(replacePreviewFile(path, "name,value\nalpha,10\nbeta,2\n,empty first cell\n"));
        PreviewWindow window(false);
        window.ShowPreview({path}, {});
        QTRY_VERIFY(window.findChild<QTableView *>(QStringLiteral("tableCells")));
        auto *view = window.findChild<QTableView *>(QStringLiteral("tableCells"));
        QTRY_VERIFY(view->model());
        QCOMPARE(view->model()->rowCount(), 4);
        view->sortByColumn(1, Qt::AscendingOrder);
        QCOMPARE(view->model()->index(0, 1).data().toString(), QStringLiteral("2"));
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("tableSearch"));
        search->setText(QStringLiteral("alpha"));
        QTRY_COMPARE(view->model()->rowCount(), 1);
        view->selectAll();
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("alpha\t10"));
        search->setText(QStringLiteral("empty first cell"));
        QTRY_COMPARE(view->model()->rowCount(), 1);
        QTRY_COMPARE(view->model()->index(0, 1).data().toString(),
                     QStringLiteral("empty first cell"));
        view->selectAll();
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("\tempty first cell"));
        window.HidePreview();
        TableView earlySearch(path, QLatin1Char(','));
        earlySearch.findChild<QLineEdit *>(QStringLiteral("tableSearch"))
            ->setText(QStringLiteral("beta"));
        auto *earlyCells = earlySearch.findChild<QTableView *>(QStringLiteral("tableCells"));
        QTRY_VERIFY(earlyCells->model());
        QCOMPARE(earlyCells->model()->rowCount(), 1);
        QCOMPARE(earlyCells->model()->index(0, 0).data().toString(), QStringLiteral("beta"));
    }

    void trashAdvancesAndUndoRestoresSelection()
    {
        QVERIFY(qEnvironmentVariable("XDG_DATA_HOME")
                    .startsWith(QCoreApplication::applicationDirPath()));
        QTemporaryDir directory(QCoreApplication::applicationDirPath() +
                                QStringLiteral("/window-trash-XXXXXX"));
        QVERIFY(directory.isValid());
        const auto a = directory.filePath(QStringLiteral("a.txt")),
                   b = directory.filePath(QStringLiteral("b.txt"));
        QVERIFY(replacePreviewFile(a, "first"));
        QVERIFY(replacePreviewFile(b, "second"));
        PreviewWindow window(false);
        window.ShowPreview({a, b, a}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        auto *trash = window.findChild<QAction *>(QStringLiteral("trashCurrentFile"));
        auto *undo = window.findChild<QAction *>(QStringLiteral("undoTrash"));
        QVERIFY(trash->isEnabled());
        trash->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(a), 10000);
        QTRY_VERIFY(window.windowTitle().startsWith(QStringLiteral("b.txt")));
        QTRY_VERIFY(undo->isEnabled());
        undo->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(a), 10000);
        QTRY_VERIFY(window.windowTitle().startsWith(QStringLiteral("a.txt")));
        QTRY_VERIFY(trash->isEnabled());
        trash->trigger();
        QTRY_VERIFY(window.windowTitle().startsWith(QStringLiteral("b.txt")));
        QTRY_VERIFY(trash->isEnabled());
        trash->trigger();
        QTRY_VERIFY(window.windowTitle().startsWith(QStringLiteral("Selection empty")));
        QVERIFY(!trash->isEnabled());
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("copyLocation"))->isEnabled());
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("openWith"))->isEnabled());
        QTRY_VERIFY(undo->isEnabled());
        undo->trigger();
        QTRY_VERIFY(window.windowTitle().startsWith(QStringLiteral("b.txt")));
        QTRY_VERIFY(undo->isEnabled());
        undo->trigger();
        QTRY_VERIFY(window.windowTitle().startsWith(QStringLiteral("a.txt")));
        QVERIFY(QFileInfo::exists(a));
        QVERIFY(QFileInfo::exists(b));
        // A completed operation from a previous session must not alter this selection.
        QTRY_VERIFY(trash->isEnabled());
        trash->trigger();
        window.ShowPreview({b}, {});
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(a), 10000);
        QTRY_VERIFY(undo->isEnabled());
        undo->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(a), 10000);
        QVERIFY(window.windowTitle().startsWith(QStringLiteral("b.txt")));
        window.HidePreview();
    }

    void explicitDolphinMetadataEditing()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("metadata.txt"));
        QVERIFY(replacePreviewFile(path, "metadata test"));
        KFileMetaData::UserMetaData metadata(path);
        if (!metadata.isSupported())
            QSKIP("Fixture filesystem does not support user metadata");
        PreviewWindow window(false);
        QSettings().setValue(QStringLiteral("window/information"), true);
        window.ShowPreview({path}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        auto *panel = window.findChild<InformationPanel *>();
        QVERIFY(panel);
        panel->show();
        panel->setFile(QUrl::fromLocalFile(path));
        auto *edit = panel->findChild<QPushButton *>(QStringLiteral("editUserMetadata"));
        QVERIFY(edit);
        edit->click();
        QTRY_VERIFY(window.findChild<MetadataEditor *>());
        auto *editor = window.findChild<MetadataEditor *>();
        auto *tags = editor->findChild<QPlainTextEdit *>(QStringLiteral("metadataTags"));
        auto *comment = editor->findChild<QPlainTextEdit *>(QStringLiteral("metadataComment"));
        auto *rating = editor->findChild<KRatingWidget *>();
        QTRY_VERIFY(tags->isEnabled());
        tags->setPlainText(QStringLiteral("preview\ntag, with comma"));
        comment->setPlainText(QStringLiteral("Shared with Dolphin"));
        rating->setRating(7);
        auto *buttons = editor->findChild<QDialogButtonBox *>();
        QSignalSpy saved(editor, &MetadataEditor::saved);
        buttons->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(saved.count(), 0);
        QVERIFY(buttons->button(QDialogButtonBox::Apply)->isEnabled());
        tags->setPlainText(QStringLiteral("preview\nsecond tag"));
        buttons->button(QDialogButtonBox::Apply)->click();
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(metadata.rating(), 7);
        QCOMPARE(metadata.tags(),
                 QStringList({QStringLiteral("preview"), QStringLiteral("second tag")}));
        QCOMPARE(metadata.userComment(), QStringLiteral("Shared with Dolphin"));
        // Refreshing the sidebar must not destroy an in-progress form.
        panel->setFile(QUrl::fromLocalFile(path));
        QCOMPARE(window.findChild<MetadataEditor *>(), editor);
        QVERIFY(replacePreviewFile(path, "replacement file"));
        comment->setPlainText(QStringLiteral("Must not reach replacement"));
        buttons->button(QDialogButtonBox::Apply)->click();
        auto *status = editor->findChild<QLabel *>(QStringLiteral("metadataStatus"));
        QTRY_VERIFY(status->text().contains(QStringLiteral("replaced")));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(KFileMetaData::UserMetaData(path).userComment(), QString{});
        window.HidePreview();
        QTRY_VERIFY(!window.findChild<MetadataEditor *>());
    }

    void boundedRemoteTransfersAndCleanup()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("source.txt"));
        QVERIFY(replacePreviewFile(path, "temporary content"));
        QString cached;
        {
            RemotePreview transfer(nullptr, 1024);
            QSignalSpy ready(&transfer, &RemotePreview::ready);
            QSignalSpy errors(&transfer, &RemotePreview::failed);
            transfer.start(QUrl::fromLocalFile(path));
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
            QVERIFY(errors.isEmpty());
            cached = ready.first().first().toUrl().toLocalFile();
            QVERIFY(QFileInfo::exists(cached));
            QFile file(cached);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("temporary content"));
        }
        QVERIFY(!QFileInfo::exists(cached));
        RemotePreview tooLarge(nullptr, 4);
        QSignalSpy rejected(&tooLarge, &RemotePreview::failed);
        QSignalSpy accepted(&tooLarge, &RemotePreview::ready);
        tooLarge.start(QUrl::fromLocalFile(path));
        QTRY_COMPARE_WITH_TIMEOUT(rejected.count(), 1, 10000);
        QCOMPARE(accepted.count(), 0);
        RemotePreview cancelled;
        QSignalSpy stopped(&cancelled, &RemotePreview::failed);
        cancelled.start(QUrl::fromLocalFile(path));
        cancelled.cancel();
        QCOMPARE(stopped.count(), 1);
    }

    void remoteHttpPreviewAndNavigation()
    {
        if (!KProtocolInfo::isKnownProtocol(QStringLiteral("http")))
            QSKIP("HTTP KIO worker unavailable");
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QPointer<QTcpSocket>> sockets;
        connect(
            &server, &QTcpServer::newConnection, this,
            [&]
            {
                while (server.hasPendingConnections())
                {
                    auto *socket = server.nextPendingConnection();
                    sockets.append(socket);
                    connect(
                        socket, &QTcpSocket::readyRead, socket,
                        [socket]
                        {
                            const auto request = socket->readAll();
                            if (request.contains("/stall"))
                                return;
                            if (request.contains("/unknown"))
                            {
                                socket->write(
                                    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: "
                                    "close\r\n\r\nbody exceeding limit");
                                socket->disconnectFromHost();
                                return;
                            }
                            socket->write(
                                "HTTP/1.1 200 OK\r\nContent-Length: 16\r\nContent-Type: "
                                "text/plain\r\nConnection: close\r\n\r\nremote test body");
                            socket->disconnectFromHost();
                        });
                }
            });
        const QString root = QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort());
        RemotePreview unknownLength(nullptr, 4);
        QSignalSpy overLimit(&unknownLength, &RemotePreview::failed);
        QSignalSpy unexpected(&unknownLength, &RemotePreview::ready);
        unknownLength.start(QUrl(root + QStringLiteral("unknown.txt")));
        QTRY_COMPARE_WITH_TIMEOUT(overLimit.count(), 1, 10000);
        QCOMPARE(unexpected.count(), 0);
        QVERIFY(overLimit.first().first().toString().contains(QStringLiteral("transfer exceeded")));
        RemotePreview deadline(nullptr, 1024, 150);
        QSignalSpy timedOut(&deadline, &RemotePreview::failed);
        deadline.start(QUrl(root + QStringLiteral("stall.txt")));
        QTRY_COMPARE_WITH_TIMEOUT(timedOut.count(), 1, 10000);
        QVERIFY(timedOut.first().first().toString().contains(QStringLiteral("timed out")));

        PreviewWindow window(false);
        window.ShowPreview({root + QStringLiteral("remote.txt")}, {});
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<KTextEditor::View *>(), 10000);
        auto *text = window.findChild<KTextEditor::View *>();
        QCOMPARE(text->document()->text(), QStringLiteral("remote test body"));
        const auto cached = text->document()->url().toLocalFile();
        QVERIFY(QFileInfo::exists(cached));
        auto *copy = window.findChild<QAction *>(QStringLiteral("copyLocation"));
        copy->trigger();
        QCOMPARE(QApplication::clipboard()->text(), root + QStringLiteral("remote.txt"));
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("trashCurrentFile"))->isEnabled());
        window.HidePreview();
        QVERIFY(!QFileInfo::exists(cached));
        QTemporaryDir directory;
        const auto local = directory.filePath(QStringLiteral("local.txt"));
        QVERIFY(replacePreviewFile(local, "local data"));
        window.ShowPreview({root + QStringLiteral("stall.txt"), local}, {});
        QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("cancelRemoteTransfer")));
        QTest::keyClick(&window, Qt::Key_Right);
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        QCOMPARE(window.findChild<KTextEditor::View *>()->document()->text(),
                 QStringLiteral("local data"));
        window.HidePreview();
        window.ShowPreview({root + QStringLiteral("stall.txt")}, {});
        QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("cancelRemoteTransfer")));
        window.findChild<QPushButton *>(QStringLiteral("cancelRemoteTransfer"))->click();
        QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("cancelRemoteTransfer")));
        bool cancelled = false;
        for (auto *label : window.findChildren<QLabel *>())
            cancelled |= label->text().contains(QStringLiteral("cancelled"));
        QVERIFY(cancelled);
        window.HidePreview();
        window.ShowPreview({root + QStringLiteral("stall.txt")}, {});
        QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("cancelRemoteTransfer")));
        window.HidePreview();
        QVERIFY(!window.IsVisible());
        QVERIFY(!window.findChild<RemotePreview *>());
    }

    void imageComparisonMetadataAndLinkedViews()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto first = directory.filePath(QStringLiteral("left.png"));
        const auto second = directory.filePath(QStringLiteral("right.png"));
        QImage left(1600, 1000, QImage::Format_RGB32);
        left.fill(Qt::red);
        left.setText(QStringLiteral("Author"), QStringLiteral("First author"));
        QVERIFY(left.save(first));
        QImage right(1200, 800, QImage::Format_RGB32);
        right.fill(Qt::blue);
        right.setText(QStringLiteral("Author"), QStringLiteral("Second author"));
        QVERIFY(right.save(second));
        PreviewWindow window(false);
        window.ShowPreview({first, second}, {});
        QTRY_VERIFY(window.findChild<ImageView *>());
        auto *action = window.findChild<QAction *>(QStringLiteral("compareImages"));
        QVERIFY(action);
        QVERIFY(action->isEnabled());
        action->trigger();
        QPointer<ImageComparison> comparison = window.findChild<ImageComparison *>();
        QVERIFY(comparison);
        QTRY_VERIFY(comparison->findChild<ImageView *>(QStringLiteral("comparisonImage0")));
        QTRY_VERIFY(comparison->findChild<ImageView *>(QStringLiteral("comparisonImage1")));
        auto *a = comparison->findChild<ImageView *>(QStringLiteral("comparisonImage0"));
        auto *b = comparison->findChild<ImageView *>(QStringLiteral("comparisonImage1"));
        auto *metadata =
            comparison->findChild<QTableWidget *>(QStringLiteral("comparisonMetadata"));
        QVERIFY(metadata);
        const auto dimensions = metadata->findItems(QStringLiteral("Dimensions"), Qt::MatchExactly);
        QCOMPARE(dimensions.size(), 1);
        QCOMPARE(metadata->item(dimensions.first()->row(), 1)->text(),
                 QStringLiteral("1600 × 1000"));
        QCOMPARE(metadata->item(dimensions.first()->row(), 2)->text(),
                 QStringLiteral("1200 × 800"));
        QVERIFY(metadata->item(dimensions.first()->row(), 1)->font().bold());
        const auto authors =
            metadata->findItems(QStringLiteral("Embedded: Author"), Qt::MatchExactly);
        QCOMPARE(authors.size(), 1);
        QCOMPARE(metadata->item(authors.first()->row(), 1)->text(), QStringLiteral("First author"));
        QCOMPARE(metadata->item(authors.first()->row(), 2)->text(),
                 QStringLiteral("Second author"));
        auto *linked = comparison->findChild<QCheckBox *>(QStringLiteral("linkedImageViews"));
        QVERIFY(linked);
        QVERIFY(!linked->isChecked());
        const auto independent = b->viewState();
        ImageViewState zoom;
        zoom.fitted = false;
        zoom.transform.scale(1.5, 1.5);
        zoom.center = QPointF(800, 500);
        a->restoreViewState(zoom);
        QCoreApplication::processEvents();
        QCOMPARE(b->viewState().transform, independent.transform);
        linked->setChecked(true);
        QTRY_COMPARE(b->viewState().transform, a->viewState().transform);
        QTRY_VERIFY(QLineF(b->viewState().center, QPointF(600, 400)).length() < 2);
        zoom.center = QPointF(1000, 600);
        a->restoreViewState(zoom);
        QTRY_VERIFY(QLineF(b->viewState().center, QPointF(750, 480)).length() < 2);
        auto *fit = comparison->findChild<QPushButton *>(QStringLiteral("fitComparedImages"));
        fit->click();
        QTRY_VERIFY(a->viewState().fitted && b->viewState().fitted);
        // Changing one pane rapidly must not install the previous request's image.
        auto *source = comparison->findChild<QComboBox *>(QStringLiteral("comparisonSource1"));
        source->setCurrentIndex(0);
        source->setCurrentIndex(1);
        QTRY_VERIFY(comparison->findChild<ImageView *>(QStringLiteral("comparisonImage1")));
        b = comparison->findChild<ImageView *>(QStringLiteral("comparisonImage1"));
        auto *pixmap = qgraphicsitem_cast<QGraphicsPixmapItem *>(b->scene()->items().first());
        QCOMPARE(pixmap->pixmap().toImage().pixelColor(0, 0), QColor(Qt::blue));
        window.HidePreview();
        QTRY_VERIFY(comparison.isNull());
    }

    void explicitRefreshPreservesPosition()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("manual.txt"));
        QVERIFY(writePreviewFile(path, QByteArray("text\n").repeated(400)));
        PreviewWindow window(false);
        window.ShowPreview({path}, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        QPointer<KTextEditor::View> old = window.findChild<KTextEditor::View *>();
        old->setCursorPosition(KTextEditor::Cursor(110, 2));
        old->setScrollPosition(KTextEditor::Cursor(100, 0));
        QTRY_COMPARE(old->firstDisplayedLine(), 100);
        QTest::keyClick(&window, Qt::Key_F5);
        QTRY_VERIFY(old.isNull());
        auto *view = window.findChild<KTextEditor::View *>();
        QVERIFY(view);
        QTRY_COMPARE(view->firstDisplayedLine(), 100);
        QCOMPARE(view->cursorPosition(), KTextEditor::Cursor(110, 2));
        window.HidePreview();
    }

    void markdownPositionAndModeMemory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("reading.md")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int i = 0; i < 300; ++i)
            file.write("## Heading\n\nA paragraph of **Markdown** to scroll through.\n\n");
        file.close();
        const QStringList paths{file.fileName(), directory.filePath(QStringLiteral("missing.txt"))};
        PreviewWindow window(false);
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        auto *browser = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QTRY_VERIFY(browser->verticalScrollBar()->maximum() > 1000);
        browser->verticalScrollBar()->setValue(700);
        auto *tabs = window.findChild<QTabBar *>(QStringLiteral("markdownMode"));
        tabs->setCurrentIndex(1);
        auto *source = window.findChild<KTextEditor::View *>();
        source->setCursorPosition(KTextEditor::Cursor(210, 4));
        source->setScrollPosition(KTextEditor::Cursor(200, 0));
        QTRY_COMPARE(source->firstDisplayedLine(), 200);
        const auto saved = window.findChild<MarkdownView *>()->position();
        QVERIFY(saved);
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        tabs = window.findChild<QTabBar *>(QStringLiteral("markdownMode"));
        QTRY_COMPARE(tabs->currentIndex(), 1);
        source = window.findChild<KTextEditor::View *>();
        QTRY_COMPARE(source->cursorPosition(), saved->text.cursor);
        QTRY_COMPARE(source->firstDisplayedLine(), saved->text.firstLine);
        tabs->setCurrentIndex(0);
        browser = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QTRY_COMPARE(browser->verticalScrollBar()->value(), saved->vertical);
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Left);
        // Leave while Markdown is still loading, then return once more.
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        browser = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QTRY_COMPARE(browser->verticalScrollBar()->value(), saved->vertical);
        window.HidePreview();
        window.ShowPreview(paths, {});
        QTRY_VERIFY(window.findChild<KTextEditor::View *>());
        tabs = window.findChild<QTabBar *>(QStringLiteral("markdownMode"));
        QCOMPARE(tabs->currentIndex(), 0);
        browser = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QCOMPARE(browser->verticalScrollBar()->value(), 0);
        window.HidePreview();
    }

    void documentNavigationPositionMemory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("pages.pdf"));
        {
            QPdfWriter pdf(path);
            QPainter painter(&pdf);
            for (int page = 0; page < 3; ++page)
            {
                if (page)
                    QVERIFY(pdf.newPage());
                painter.drawText(100, 100, QStringLiteral("Page %1").arg(page + 1));
            }
        }
        PreviewWindow window(false);
        window.ShowPreview({path, directory.filePath(QStringLiteral("missing.txt"))}, {});
        QTRY_VERIFY(window.findChild<KParts::ReadOnlyPart *>());
        auto *part = window.findChild<KParts::ReadOnlyPart *>();
        auto page = [&window]
        {
            uint page = 0;
            auto *part = window.findChild<KParts::ReadOnlyPart *>();
            QMetaObject::invokeMethod(part, "currentPage", Qt::DirectConnection,
                                      Q_RETURN_ARG(uint, page));
            return page;
        };
        auto *next = part->actionCollection()->action(QStringLiteral("go_next"));
        QVERIFY(next);
        QTRY_VERIFY(next->isEnabled());
        QTRY_COMPARE(page(), 1U);
        next->trigger();
        QTRY_COMPARE(page(), 2U);
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_VERIFY(window.findChild<KParts::ReadOnlyPart *>());
        QTRY_COMPARE(page(), 2U);
        auto *clear = window.findChild<QAction *>(QStringLiteral("clearPositions"));
        clear->trigger();
        QTRY_VERIFY(window.findChild<KParts::ReadOnlyPart *>());
        QTRY_COMPARE(page(), 1U);
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
        player->pause();
        player->setPosition(4000);
        QTRY_COMPARE(player->position(), qint64(4000));
        QTest::keyClick(&window, Qt::Key_Right);
        QVERIFY(player.isNull());
        QCOMPARE(window.findChild<QWidget *>(QStringLiteral("audioControls")), nullptr);
        surface = window.findChild<QWidget *>(QStringLiteral("previewSurface"));
        QVERIFY(surface);
        QCOMPARE(surface->property("mimeType").toString(), QStringLiteral("image/png"));
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_VERIFY(window.findChild<QMediaPlayer *>());
        player = window.findChild<QMediaPlayer *>();
        QTRY_COMPARE(player->position(), qint64(4000));
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
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

    void previewPerformance()
    {
        if (!qEnvironmentVariableIsSet("KGLANCEQT_BENCHMARK_REPORT"))
            QSKIP("Run tests/benchmark.py for explicit performance measurement.");
        QTemporaryDir directory;
        const auto text = directory.filePath(QStringLiteral("sample.cpp"));
        const auto image = directory.filePath(QStringLiteral("sample.png"));
        QVERIFY(writePreviewFile(text, QByteArray("// benchmark sample\n").repeated(1000)));
        QImage pixels(1920, 1080, QImage::Format_RGB32);
        pixels.fill(Qt::blue);
        QVERIFY(pixels.save(image));
        QElapsedTimer timer;
        timer.start();
        PreviewWindow window(false);
        const double construction = timer.nsecsElapsed() / 1e6;
        timer.restart();
        window.ShowPreview({text, image}, {});
        const double show = timer.nsecsElapsed() / 1e6;
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<KTextEditor::View *>(), 10000);
        const double useful = timer.nsecsElapsed() / 1e6;
        const qint64 processFirst =
            QDateTime::currentMSecsSinceEpoch() -
            qEnvironmentVariable("KGLANCEQT_BENCHMARK_START_MS").toLongLong();
        timer.restart();
        window.findChild<QAction *>(QStringLiteral("nextFile"))->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<ImageView *>(), 10000);
        const double transition = timer.nsecsElapsed() / 1e6;
        window.HidePreview();
        timer.restart();
        window.ShowPreview({text}, {});
        const double resident = timer.nsecsElapsed() / 1e6;
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<KTextEditor::View *>(), 10000);
        const double residentContent = timer.nsecsElapsed() / 1e6;
        QTest::qWait(500);
        QFile stat(QStringLiteral("/proc/self/stat"));
        QVERIFY(stat.open(QIODevice::ReadOnly));
        const auto first = stat.readAll();
        QTest::qWait(1000);
        stat.seek(0);
        const auto last = stat.readAll();
        const auto ticks = [](const QByteArray &bytes)
        {
            const auto fields = bytes.mid(bytes.lastIndexOf(')') + 2).split(' ');
            return fields.value(11).toLongLong() + fields.value(12).toLongLong();
        };
        QFile memory(QStringLiteral("/proc/self/status"));
        QVERIFY(memory.open(QIODevice::ReadOnly));
        QJsonObject report{{"platform", QGuiApplication::platformName()},
                           {"window_construction_ms", construction},
                           {"show_dispatch_ms", show},
                           {"first_text_ms", useful},
                           {"fresh_process_first_text_ms", processFirst},
                           {"text_to_image_ms", transition},
                           {"resident_show_dispatch_ms", resident},
                           {"resident_text_ms", residentContent},
                           {"idle_cpu_ticks_1s", ticks(last) - ticks(first)},
                           {"memory_status", QString::fromUtf8(memory.readAll())}};
        QFile output(qEnvironmentVariable("KGLANCEQT_BENCHMARK_REPORT"));
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(report).toJson());
        window.HidePreview();
    }

    void positionMemoryCanBeDisabled()
    {
        PreviewPreferences p;
        p.rememberPositions = false;
        p.sqliteEnabled = false;
        p.sqliteRows = 25;
        QVERIFY(savePreferences(p));
        QCOMPARE(readPreferences().rememberPositions, false);
        QCOMPARE(readPreferences().sqliteEnabled, false);
        QCOMPARE(readPreferences().sqliteRows, 25);
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("data.csv"));
        const auto other = directory.filePath(QStringLiteral("other.csv"));
        QVERIFY(writePreviewFile(path, "one,two\nthree,four\n"));
        QVERIFY(writePreviewFile(other, "five,six\n"));
        PreviewWindow window(false);
        window.ShowPreview({path, other}, {});
        QTRY_VERIFY(window.findChild<TableView *>());
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("tableSearch"));
        search->setText(QStringLiteral("three"));
        window.findChild<QAction *>(QStringLiteral("nextFile"))->trigger();
        window.findChild<QAction *>(QStringLiteral("previousFile"))->trigger();
        QTRY_VERIFY(window.findChild<TableView *>());
        QCOMPARE(window.findChild<QLineEdit *>(QStringLiteral("tableSearch"))->text(), QString{});
        window.HidePreview();
    }
#ifdef KGLANCEQT_WITH_SQLITE
    void sqliteNavigationAndHide()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("sample.sqlite"));
        sqlite3 *db = nullptr;
        QCOMPARE(sqlite3_open(QFile::encodeName(path).constData(), &db), SQLITE_OK);
        QCOMPARE(sqlite3_exec(db,
                              "CREATE TABLE one(id, name); INSERT INTO one VALUES(1,'sample'); "
                              "CREATE TABLE two(value); INSERT INTO two VALUES(2);",
                              nullptr, nullptr, nullptr),
                 SQLITE_OK);
        sqlite3_close(db);
        PreviewWindow window(false);
        window.ShowPreview({path}, {});
        QTRY_VERIFY(window.findChild<SQLiteView *>());
        auto *choice = window.findChild<SQLiteView *>()->findChild<QComboBox *>();
        QTRY_COMPARE(choice->count(), 2);
        QTRY_VERIFY(window.findChild<QTableView *>(QStringLiteral("tableCells")));
        QTRY_VERIFY(window.findChild<QTableView *>(QStringLiteral("tableCells"))->model());
        QCOMPARE(window.findChild<QTableView *>(QStringLiteral("tableCells"))
                     ->model()
                     ->headerData(1, Qt::Horizontal)
                     .toString(),
                 QStringLiteral("name"));
        choice->setCurrentIndex(1);
        QTRY_VERIFY(window.findChild<QTableView *>(QStringLiteral("tableCells"))->model());
        QCOMPARE(window.findChild<QTableView *>(QStringLiteral("tableCells"))
                     ->model()
                     ->index(0, 0)
                     .data()
                     .toString(),
                 QStringLiteral("2"));
        if (qEnvironmentVariableIsSet("KGLANCEQT_TEST_SCREENSHOTS"))
        {
            QDir().mkpath(qEnvironmentVariable("KGLANCEQT_TEST_SCREENSHOTS"));
            window.grab().save(qEnvironmentVariable("KGLANCEQT_TEST_SCREENSHOTS") +
                               QStringLiteral("/sqlite.png"));
        }
        window.HidePreview();
        QVERIFY(!window.findChild<SQLiteView *>());
        window.ShowPreview({path}, {});
        window.HidePreview();
        QTest::qWait(50);
        QVERIFY(!window.findChild<SQLiteView *>());
    }
#endif

    void desktopContextMenuDiscovery()
    {
        QTemporaryDir directory;
        const auto first = directory.filePath(QStringLiteral("first file.cpp"));
        const auto second = directory.filePath(QStringLiteral("second #file.csv"));
        QVERIFY(writePreviewFile(first, "int first;\n"));
        QVERIFY(writePreviewFile(second, "one,two\n"));
        for (const KFileItemList &items :
             {KFileItemList{KFileItem(QUrl::fromLocalFile(first))},
              KFileItemList{KFileItem(QUrl::fromLocalFile(first)),
                            KFileItem(QUrl::fromLocalFile(second))},
              KFileItemList{KFileItem(QUrl(QStringLiteral("desktop:/first%20file.cpp")),
                                      QStringLiteral("text/x-c++src"), S_IFREG),
                            KFileItem(QUrl(QStringLiteral("desktop:/second%20%23file.csv")),
                                      QStringLiteral("text/csv"), S_IFREG)}})
        {
            KFileItemActions actions;
            actions.setItemListProperties(KFileItemListProperties(items));
            QMenu menu;
            actions.addActionsTo(&menu, KFileItemActions::MenuActionSource::Services);
            bool found = false;
            auto candidates = menu.actions();
            for (int i = 0; i < candidates.size(); ++i)
                if (candidates[i]->menu())
                    candidates.append(candidates[i]->menu()->actions());
            for (auto *action : candidates)
                if (action->text().contains(QStringLiteral("Preview with KGlanceQT")))
                {
                    QVERIFY(action->isEnabled());
                    QVERIFY(!action->icon().isNull());
                    found = true;
                }
            QVERIFY2(
                found,
                "KIO must expose the preview action for single, mixed and desktop selections.");
        }
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
        const auto second = desktop + QStringLiteral("/second #preview.csv");
        QVERIFY(writePreviewFile(second, "one,two\n"));
        window.HidePreview();
        window.ShowPreview({QStringLiteral("desktop:/desktop%20preview.cpp"),
                            QStringLiteral("desktop:/second%20%23preview.csv")},
                           {});
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<KTextEditor::View *>(), 10000);
        window.findChild<QAction *>(QStringLiteral("nextFile"))->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<TableView *>(), 10000);
        QVERIFY(window.windowTitle().contains(QStringLiteral("second #preview.csv")));
        window.findChild<QAction *>(QStringLiteral("previousFile"))->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<KTextEditor::View *>(), 10000);
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
