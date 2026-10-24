// SPDX-License-Identifier: AGPL-3.0-only

#include "previewwindow.h"
#include "imageview.h"
#include "informationpanel.h"
#include "markdownview.h"
#include "mediaview.h"
#include "preferences.h"
#include "previewsurface.h"
#include "providers.h"
#include "supportedtypesdialog.h"

#include <KFileItem>
#include <KIO/ApplicationLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KIO/PreviewJob>
#include <KIO/StatJob>
#include <KParts/PartLoader>
#include <KParts/ReadOnlyPart>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>
#include <KWindowSystem>
#include <QApplication>
#include <QAudioOutput>
#include <QClipboard>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMenu>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QtConcurrentRun>

PreviewWindow::PreviewWindow(bool standalone)
    : content_(new QStackedWidget(this)), info_(new QLabel(this)),
      information_(new InformationPanel(this)), standalone_(standalone)
{
    setWindowIcon(QIcon::fromTheme(QStringLiteral("preview")));
    setCentralWidget(content_);
    resize(960, 720);
    restoreGeometry(QSettings().value(QStringLiteral("window/geometry")).toByteArray());
    auto *toolbar = addToolBar(tr("Preview"));
    toolbar->setMovable(false);
    const auto action = [toolbar](const QString &icon, const QString &text,
                                  const QKeySequence &shortcut, auto callback)
    {
        auto *item = toolbar->addAction(QIcon::fromTheme(icon), text, callback);
        item->setShortcut(shortcut);
        return item;
    };
    action(QStringLiteral("go-previous"), tr("Previous file"), QKeySequence(Qt::ALT | Qt::Key_Left),
           [this] { navigate(-1); });
    action(QStringLiteral("go-next"), tr("Next file"), QKeySequence(Qt::ALT | Qt::Key_Right),
           [this] { navigate(1); });
    toolbar->addSeparator();
    action(QStringLiteral("document-open"), tr("Open in application"),
           QKeySequence(Qt::CTRL | Qt::Key_O),
           [this]
           {
               if (currentUrl_.isValid())
               {
                   auto *job = new KIO::OpenUrlJob(currentUrl_);
                   job->start();
               }
           });
    action(QStringLiteral("zoom-fit-best"), tr("Fit image"), QKeySequence(Qt::CTRL | Qt::Key_0),
           [this]
           {
               if (auto *image = dynamic_cast<ImageView *>(content_->currentWidget()))
               {
                   image->fit();
               }
           });
    toolbar->addSeparator();
    action(QStringLiteral("window-close"), tr("Hide preview"), QKeySequence(Qt::Key_Escape),
           [this] { HidePreview(); });
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("applicationMenu"));
    auto *openWith =
        menu->addAction(QIcon::fromTheme(QStringLiteral("document-open")), tr("Open With…"), this,
                        [this]
                        {
                            auto *job = new KIO::ApplicationLauncherJob(this);
                            job->setUrls({currentUrl_});
                            job->setUiDelegate(KIO::createDefaultJobUiDelegate(
                                KJobUiDelegate::AutoHandlingEnabled, this));
                            job->start();
                        });
    openWith->setObjectName(QStringLiteral("openWith"));
    auto *reveal = menu->addAction(QIcon::fromTheme(QStringLiteral("folder-open")),
                                   tr("Show in File Manager"), this,
                                   [this]
                                   {
                                       auto *job = new KIO::OpenFileManagerWindowJob(this);
                                       job->setHighlightUrls({currentUrl_});
                                       job->setUiDelegate(KIO::createDefaultJobUiDelegate(
                                           KJobUiDelegate::AutoHandlingEnabled, this));
                                       job->start();
                                   });
    reveal->setObjectName(QStringLiteral("showInFileManager"));
    auto *copyLocation =
        menu->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copy Location"), this,
                        [this]
                        {
                            QApplication::clipboard()->setText(currentUrl_.isLocalFile()
                                                                   ? currentUrl_.toLocalFile()
                                                                   : currentUrl_.toDisplayString());
                        });
    copyLocation->setObjectName(QStringLiteral("copyLocation"));
    connect(menu, &QMenu::aboutToShow, this,
            [this, openWith, reveal, copyLocation]
            {
                const bool available = !paths_.isEmpty() && currentUrl_.isValid();
                for (auto *item : {openWith, reveal, copyLocation})
                    item->setEnabled(available);
            });
    menu->addSeparator();
    auto *quit = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                 tr("Quit KGlanceQT"), this, &PreviewWindow::Quit);
    quit->setObjectName(QStringLiteral("quitKGlanceQT"));
    quit->setShortcut(QKeySequence::Quit);
    quit->setStatusTip(
        tr("Stop the background preview service. Use Dolphin's preview action to start it again."));
    addAction(quit);
    auto *menuButton = new QToolButton(toolbar);
    menuButton->setObjectName(QStringLiteral("applicationMenuButton"));
    menuButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    menuButton->setText(tr("Application menu"));
    menuButton->setToolTip(tr("Application menu"));
    menuButton->setAccessibleName(tr("Application menu"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    menuButton->setMenu(menu);
    auto *toggle = new QAction(this);
    toggle->setShortcut(QKeySequence(Qt::Key_Space));
    addAction(toggle);
    connect(toggle, &QAction::triggered, this, &PreviewWindow::HidePreview);
    statusBar()->addWidget(info_, 1);
    addDockWidget(Qt::RightDockWidgetArea, information_);
    information_->setVisible(
        QSettings().value(QStringLiteral("window/information"), false).toBool());
    auto *informationAction = information_->toggleViewAction();
    informationAction->setIcon(QIcon::fromTheme(QStringLiteral("dialog-information")));
    informationAction->setShortcut(QKeySequence(Qt::Key_F11));
    informationAction->setObjectName(QStringLiteral("showInformation"));
    toolbar->addAction(informationAction);
    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    auto *configure =
        action(QStringLiteral("configure"), tr("Configure KGlanceQT…"),
               QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma), [this] { showPreferences(); });
    configure->setObjectName(QStringLiteral("configureKGlanceQT"));
    menu->insertAction(quit, configure);
    auto *help = new QMenu(tr("Help"), menu);
    help->setObjectName(QStringLiteral("helpMenu"));
    help->setIcon(QIcon::fromTheme(QStringLiteral("help-contents")));
    auto *types =
        help->addAction(tr("Supported Preview Types…"), this, &PreviewWindow::showSupportedTypes);
    types->setObjectName(QStringLiteral("showSupportedTypes"));
    menu->insertMenu(quit, help);
    menu->insertSeparator(quit);
    toolbar->addWidget(menuButton);
    connect(informationAction, &QAction::triggered, this,
            [this](bool visible)
            {
                QSettings().setValue(QStringLiteral("window/information"), visible);
                if (visible && isVisible())
                    information_->setFile(currentUrl_);
                else
                    information_->clear();
            });
    connect(information_, &QDockWidget::visibilityChanged, this,
            [this](bool visible)
            {
                if (isVisible() && !visible)
                {
                    QSettings().setValue(QStringLiteral("window/information"), false);
                    information_->clear();
                }
            });
    qApp->installEventFilter(this);
}

bool PreviewWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (isVisible() && widget && widget->window() == this && qobject_cast<QVideoWidget *>(widget) &&
        event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
    {
        togglePlayback();
        return true;
    }
    if (isVisible() && paths_.size() > 1 && widget && !qobject_cast<QLineEdit *>(widget) &&
        widget->window() == this &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress))
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier &&
            (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right))
        {
            // Embedded viewers may claim arrow keys before window shortcuts run.
            key->accept();
            if (event->type() == QEvent::KeyPress)
            {
                navigate(key->key() == Qt::Key_Left ? -1 : 1);
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void PreviewWindow::ShowPreview(const QStringList &paths, const QString &activationToken)
{
    if (paths.isEmpty())
    {
        return;
    }
    if (isVisible() && paths == paths_)
    {
        HidePreview();
        return;
    }
    imageStates_.clear();
    paths_ = paths;
    index_ = 0;
    if (!activationToken.isEmpty())
    {
        KWindowSystem::setCurrentXdgActivationToken(activationToken);
    }
    show();
    KWindowSystem::activateWindow(windowHandle());
    // Return control to DBus before asking a viewer to open the file.
    QTimer::singleShot(0, this, &PreviewWindow::loadCurrent);
}

void PreviewWindow::clearContent()
{
    ++generation_;
    information_->clear();
    workerStop_.request_stop();
    workerStop_ = std::stop_source{};
    if (thumbnailJob_)
    {
        thumbnailJob_->kill();
    }
    if (player_)
    {
        player_->stop();
    }
    if (part_)
    {
        delete part_.data();
    }
    while (content_->count())
    {
        delete content_->widget(0);
    }
}

void PreviewWindow::HidePreview()
{
    if (supportedTypes_)
        supportedTypes_->close();
    if (preferences_)
        preferences_->close();
    QSettings().setValue(QStringLiteral("window/geometry"), saveGeometry());
    hide();
    clearContent();
    imageStates_.clear();
    if (standalone_)
    {
        qApp->quit();
    }
}

void PreviewWindow::showPreferences()
{
    if (preferences_)
    {
        preferences_->raise();
        preferences_->activateWindow();
        return;
    }
    preferences_ = new PreferencesDialog(this);
    preferences_->setAttribute(Qt::WA_DeleteOnClose);
    connect(preferences_, &PreferencesDialog::applied, this,
            [this]
            {
                const auto settings = readPreferences();
                information_->setVisible(settings.information);
                if (settings.information && isVisible())
                    information_->setFile(currentUrl_);
                else
                    information_->clear();
                if (player_ && player_->audioOutput())
                    player_->audioOutput()->setVolume(settings.volume / 100.0f);
            });
    preferences_->show();
}

void PreviewWindow::showSupportedTypes()
{
    if (!supportedTypes_)
    {
        supportedTypes_ = new SupportedTypesDialog(this);
        supportedTypes_->setAttribute(Qt::WA_DeleteOnClose);
    }
    supportedTypes_->show();
    supportedTypes_->raise();
    supportedTypes_->activateWindow();
}

bool PreviewWindow::IsVisible() const { return isVisible(); }
void PreviewWindow::Quit()
{
    HidePreview();
    qApp->quit();
}

void PreviewWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    HidePreview();
}

void PreviewWindow::navigate(int offset)
{
    if (paths_.size() > 1)
    {
        if (auto *image = dynamic_cast<ImageView *>(content_->currentWidget()))
            imageStates_.insert(currentUrl_, image->viewState());
        index_ = (index_ + offset + paths_.size()) % paths_.size();
        loadCurrent();
    }
}

void PreviewWindow::message(const QString &text)
{
    const auto mime = QMimeDatabase().mimeTypeForUrl(currentUrl_);
    auto *surface = new PreviewSurface(mime, content_);
    auto *layout = new QVBoxLayout(surface);
    auto *label = new QLabel(text, surface);
    label->setTextFormat(Qt::PlainText);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label);
    showContent(surface);
}

void PreviewWindow::showContent(QWidget *widget)
{
    while (content_->count())
    {
        delete content_->widget(0);
    }
    content_->addWidget(widget);
    widget->setFocus();
}

bool PreviewWindow::showPart(const QUrl &url, const QString &mime)
{
    for (const auto &metadata : KParts::PartLoader::partsForMimeType(mime))
    {
        // Only viewer components; never embed a shell, file manager, or web engine.
        const auto id = metadata.pluginId();
        if (id != QLatin1String("okularpart") && id != QLatin1String("kfontviewpart"))
        {
            continue;
        }
        auto result =
            KParts::PartLoader::instantiatePart<KParts::ReadOnlyPart>(metadata, content_, this);
        if (result)
        {
            part_ = result.plugin;
            showContent(part_->widget());
            if (part_->openUrl(url))
            {
                return true;
            }
            delete part_.data();
        }
    }
    return false;
}

void PreviewWindow::loadCurrent()
{
    if (!isVisible() || paths_.isEmpty())
    {
        return;
    }
    clearContent();
    const quint64 request = generation_;
    const QString input = paths_.at(index_);
    const QUrl supplied(input);
    currentUrl_ = supplied.scheme().isEmpty() ? QUrl::fromLocalFile(input) : supplied;
    if (currentUrl_.scheme() == QLatin1String("desktop"))
    {
        message(tr("Resolving desktop file…"));
        auto *job = KIO::mostLocalUrl(currentUrl_, KIO::HideProgressInfo);
        thumbnailJob_ = job;
        connect(job, &KJob::result, this,
                [this, job, request]
                {
                    if (request != generation_)
                    {
                        return;
                    }
                    if (job->error())
                    {
                        message(job->errorString());
                    }
                    else
                    {
                        loadLocal(job->mostLocalUrl());
                    }
                });
        return;
    }
    loadLocal(currentUrl_);
}

void PreviewWindow::loadLocal(const QUrl &url)
{
    currentUrl_ = url;
    const auto request = generation_;
    if (!currentUrl_.isLocalFile())
    {
        message(tr("This location cannot be previewed locally."));
        return;
    }
    const QString path = currentUrl_.toLocalFile();
    const QFileInfo file(path);
    setWindowTitle(tr("%1 — KGlanceQT").arg(file.fileName()));
    const auto mime = QMimeDatabase().mimeTypeForFile(file);
    if (information_->isVisible())
        information_->setFile(url);
    info_->setText(tr("%1 / %2 · %3").arg(index_ + 1).arg(paths_.size()).arg(mime.comment()));
    info_->setToolTip(path);
    if (!file.exists() || !file.isReadable())
    {
        message(tr("This file is missing or cannot be read.\n%1").arg(path));
    }
    else if (file.isDir())
    {
        auto *view = new QTreeView(content_);
        auto *model = new QFileSystemModel(view);
        model->setReadOnly(true);
        model->setRootPath(path);
        view->setModel(model);
        view->setRootIndex(model->index(path));
        view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        showContent(view);
    }
    else if (mime.name().startsWith(QLatin1String("image/")))
    {
        message(tr("Loading image…"));
        auto *watcher = new QFutureWatcher<ImageResult>(this);
        connect(watcher, &QFutureWatcher<ImageResult>::finished, this,
                [this, watcher, request]
                {
                    const auto result = watcher->result();
                    watcher->deleteLater();
                    if (request != generation_)
                    {
                        return;
                    }
                    if (result.image.isNull())
                    {
                        message(result.error);
                    }
                    else
                    {
                        auto *image = new ImageView(result.image, content_);
                        showContent(image);
                        if (imageStates_.contains(currentUrl_))
                        {
                            const auto state = imageStates_.value(currentUrl_);
                            QTimer::singleShot(0, image,
                                               [image, state] { image->restoreViewState(state); });
                        }
                    }
                });
        watcher->setFuture(
            QtConcurrent::run(loadImage, path, workerStop_.get_token(),
                              QSettings().value(QStringLiteral("limits/imageMP"), 64).toInt()));
    }
    else if (mime.name().startsWith(QLatin1String("audio/")) ||
             mime.name().startsWith(QLatin1String("video/")))
    {
        showMedia(currentUrl_, mime.name());
    }
    else if (mime.inherits(QStringLiteral("text/markdown")) ||
             QStringList{QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mkd")}
                 .contains(file.suffix().toLower()))
    {
        showContent(new MarkdownView(path, content_));
    }
    else if (mime.inherits(QStringLiteral("text/plain")) ||
             mime.inherits(QStringLiteral("application/json")))
    {
        const int limit =
            qBound(1, QSettings().value(QStringLiteral("limits/textMiB"), 16).toInt(), 16);
        if (file.size() > limit * 1024LL * 1024)
        {
            message(tr("Text exceeds the %1 MiB preview limit.").arg(limit));
            return;
        }
        auto *document = KTextEditor::Editor::instance()->createDocument(this);
        document->setReadWrite(false);
        auto *view = document->createView(content_);
        view->setStatusBarEnabled(false);
        part_ = document;
        showContent(view);
        if (!document->openUrl(currentUrl_))
        {
            message(tr("Could not open this text file."));
        }
    }
    else if (isArchive(mime.name()))
    {
        message(tr("Reading archive…"));
        auto *watcher = new QFutureWatcher<ArchiveResult>(this);
        connect(watcher, &QFutureWatcher<ArchiveResult>::finished, this,
                [this, watcher, request]
                {
                    const auto result = watcher->result();
                    watcher->deleteLater();
                    if (request != generation_)
                    {
                        return;
                    }
                    if (!result.error.isEmpty())
                    {
                        message(result.error);
                        return;
                    }
                    auto *view = new QTreeWidget(content_);
                    view->setHeaderLabels({tr("Archive path"), tr("Size")});
                    view->setRootIsDecorated(false);
                    for (const auto &row : result.rows)
                    {
                        new QTreeWidgetItem(view, {row.path, row.size});
                    }
                    view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
                    showContent(view);
                    if (result.truncated)
                    {
                        info_->setText(
                            tr("Archive entry limit reached; showing a partial listing."));
                    }
                });
        watcher->setFuture(QtConcurrent::run(
            listArchive, path, mime.name(), workerStop_.get_token(),
            QSettings().value(QStringLiteral("limits/archiveEntries"), 10000).toInt()));
    }
    else if (!showPart(currentUrl_, mime.name()))
    {
        showFallback(currentUrl_, mime.name());
    }
}

void PreviewWindow::showFallback(const QUrl &url, const QString &mime)
{
    message(tr("No interactive viewer for %1. Looking for a KDE thumbnail…").arg(mime));
    const auto request = generation_;
    auto *job = KIO::filePreview({KFileItem(url, mime, 0)}, QSize(768, 768));
    thumbnailJob_ = job;
    connect(job, &KIO::PreviewJob::gotPreview, this,
            [this, request](const KFileItem &, const QPixmap &preview)
            {
                if (request == generation_)
                {
                    showContent(new ImageView(preview.toImage(), content_));
                }
            });
    connect(job, &KIO::PreviewJob::failed, this,
            [this, request, mime](const KFileItem &)
            {
                if (request == generation_)
                {
                    message(tr("No preview provider for %1.\nUse Open in "
                               "application to view this file.")
                                .arg(mime));
                }
            });
}

void PreviewWindow::showMedia(const QUrl &url, const QString &mime)
{
    auto *view = new MediaView(url, QMimeDatabase().mimeTypeForName(mime), content_);
    player_ = view->player();
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &error) { info_->setText(error); });
    showContent(view);
}

void PreviewWindow::togglePlayback()
{
    if (!player_)
    {
        return;
    }
    if (player_->playbackState() == QMediaPlayer::PlayingState)
    {
        player_->pause();
    }
    else
    {
        player_->play();
    }
}
