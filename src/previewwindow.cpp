// SPDX-License-Identifier: AGPL-3.0-only

#include "previewwindow.h"
#include "imageview.h"
#include "providers.h"

#include <KFileItem>
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
#include <QCloseEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QMediaPlayer>
#include <QMimeDatabase>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QtConcurrentRun>

PreviewWindow::PreviewWindow(bool standalone)
    : content_(new QStackedWidget(this)), info_(new QLabel(this)), standalone_(standalone)
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
    action(QStringLiteral("application-exit"), tr("Quit KGlanceQT"), QKeySequence::Quit,
           [this] { Quit(); });
    auto *toggle = new QAction(this);
    toggle->setShortcut(QKeySequence(Qt::Key_Space));
    addAction(toggle);
    connect(toggle, &QAction::triggered, this, &PreviewWindow::HidePreview);
    statusBar()->addWidget(info_, 1);
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
    QSettings().setValue(QStringLiteral("window/geometry"), saveGeometry());
    hide();
    clearContent();
    if (standalone_)
    {
        qApp->quit();
    }
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
        index_ = (index_ + offset + paths_.size()) % paths_.size();
        loadCurrent();
    }
}

void PreviewWindow::message(const QString &text)
{
    auto *label = new QLabel(text, content_);
    label->setTextFormat(Qt::PlainText);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    showContent(label);
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
                        showContent(new ImageView(result.image, content_));
                    }
                });
        watcher->setFuture(QtConcurrent::run(loadImage, path, workerStop_.get_token()));
    }
    else if (mime.name().startsWith(QLatin1String("audio/")) ||
             mime.name().startsWith(QLatin1String("video/")))
    {
        showMedia(currentUrl_);
    }
    else if (mime.inherits(QStringLiteral("text/plain")) ||
             mime.inherits(QStringLiteral("application/json")))
    {
        if (file.size() > 16 * 1024 * 1024)
        {
            message(tr("Text exceeds the 16 MiB preview limit."));
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
                        info_->setText(tr("Showing the first 10,000 archive entries."));
                    }
                });
        watcher->setFuture(
            QtConcurrent::run(listArchive, path, mime.name(), workerStop_.get_token()));
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

void PreviewWindow::showMedia(const QUrl &url)
{
    auto *panel = new QWidget(content_);
    auto *layout = new QVBoxLayout(panel);
    auto *video = new QVideoWidget(panel);
    auto *play = new QPushButton(tr("Play / pause"), panel);
    auto *seek = new QSlider(Qt::Horizontal, panel);
    auto *player = new QMediaPlayer(panel);
    auto *audio = new QAudioOutput(panel);
    player_ = player;
    player->setAudioOutput(audio);
    player->setVideoOutput(video);
    layout->addWidget(video, 1);
    layout->addWidget(seek);
    layout->addWidget(play);
    connect(play, &QPushButton::clicked, player,
            [player]
            {
                if (player->playbackState() == QMediaPlayer::PlayingState)
                {
                    player->pause();
                }
                else
                {
                    player->play();
                }
            });
    connect(player, &QMediaPlayer::durationChanged, seek,
            [seek](qint64 value) { seek->setMaximum(int(value / 1000)); });
    connect(player, &QMediaPlayer::positionChanged, seek,
            [seek](qint64 value)
            {
                if (!seek->isSliderDown())
                {
                    seek->setValue(int(value / 1000));
                }
            });
    connect(seek, &QSlider::sliderMoved, player,
            [player](int value) { player->setPosition(qint64(value) * 1000); });
    connect(player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &error) { info_->setText(error); });
    showContent(panel);
    player->setSource(url);
    // Opening a preview should not unexpectedly start loud playback.
}
