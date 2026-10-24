// SPDX-License-Identifier: AGPL-3.0-only

#include "previewwindow.h"
#include "appshortcuts.h"
#include "filedragbutton.h"
#include "filetriage.h"
#include "imagecomparison.h"
#include "imageview.h"
#include "informationpanel.h"
#include "markdownview.h"
#include "mediaview.h"
#include "preferences.h"
#include "previewsurface.h"
#include "providers.h"
#include "remotepreview.h"
#include "selectionfilmstrip.h"
#include "supportedtypesdialog.h"
#include "tableview.h"
#ifdef KGLANCEQT_WITH_SQLITE
#include "sqlitepreview.h"
#endif
#include <KStandardAction>
#include <KStandardShortcut>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>

#include <KFileItem>
#include <KIO/ApplicationLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KIO/PreviewJob>
#include <KIO/StatJob>
#include <KMessageWidget>
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
#include <QDir>
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
      information_(new InformationPanel(this)), filmstrip_(new SelectionFilmstrip(this)),
      dragFile_(new FileDragButton(this)), fileMonitor_(new FileMonitor(this)),
      fileNotice_(new KMessageWidget(this)), shortcuts_(new AppShortcuts(this)),
      triage_(new FileTriage(this)), operationNotice_(new KMessageWidget(this)),
      standalone_(standalone)
{
    setWindowIcon(QIcon::fromTheme(QStringLiteral("org.kglanceqt.KGlanceQT"),
                                   QIcon(QStringLiteral(":/icons/app.png"))));
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    fileNotice_->setObjectName(QStringLiteral("fileChangeNotice"));
    fileNotice_->setWordWrap(true);
    fileNotice_->hide();
    layout->addWidget(fileNotice_);
    operationNotice_->setObjectName(QStringLiteral("fileOperationNotice"));
    operationNotice_->setTextFormat(Qt::PlainText);
    operationNotice_->setWordWrap(true);
    operationNotice_->hide();
    layout->addWidget(operationNotice_);
    layout->addWidget(content_, 1);
    layout->addWidget(filmstrip_);
    setCentralWidget(central);
    filmstrip_->hide();
    connect(fileMonitor_, &FileMonitor::changed, this, &PreviewWindow::refreshCurrent);
    connect(fileMonitor_, &FileMonitor::monitoringFailed, this,
            [this](const QString &path)
            {
                if (isVisible() && currentUrl_.isLocalFile() &&
                    QDir::cleanPath(currentUrl_.toLocalFile()) == path)
                {
                    fileNotice_->setText(tr("Automatic refresh is unavailable for this location."));
                    fileNotice_->setMessageType(KMessageWidget::Warning);
                    fileNotice_->show();
                }
            });
    connect(filmstrip_, &SelectionFilmstrip::fileSelected, this,
            [this](int row)
            {
                if (row >= 0 && row < paths_.size() && row != index_)
                    navigate(row - index_);
            });
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
    auto *previous = action(QStringLiteral("go-previous"), tr("Previous file"),
                            QKeySequence(Qt::ALT | Qt::Key_Left), [this] { navigate(-1); });
    auto *next = action(QStringLiteral("go-next"), tr("Next file"),
                        QKeySequence(Qt::ALT | Qt::Key_Right), [this] { navigate(1); });
    shortcuts_->add(previous, QStringLiteral("previousFile"));
    shortcuts_->add(next, QStringLiteral("nextFile"));
    toolbar->addSeparator();
    auto *open = action(QStringLiteral("document-open"), tr("Open in application"),
                        QKeySequence(Qt::CTRL | Qt::Key_O),
                        [this]
                        {
                            if (currentUrl_.isValid())
                            {
                                auto *job = new KIO::OpenUrlJob(currentUrl_);
                                job->start();
                            }
                        });
    toolbar->addWidget(dragFile_);
    animationPlayback_ =
        action(QStringLiteral("media-playback-pause"), tr("Pause animation"), {},
               [this]
               {
                   if (auto *image = qobject_cast<ImageView *>(content_->currentWidget()))
                       image->toggleAnimation();
               });
    animationPlayback_->setObjectName(QStringLiteral("animationPlayback"));
    animationPlayback_->setVisible(false);
    action(QStringLiteral("zoom-fit-best"), tr("Fit image"), QKeySequence(Qt::CTRL | Qt::Key_0),
           [this]
           {
               if (auto *image = dynamic_cast<ImageView *>(content_->currentWidget()))
               {
                   image->fit();
               }
           });
    shortcuts_->add(open, QStringLiteral("openInApplication"));
    toolbar->addSeparator();
    auto *hide = action(QStringLiteral("window-close"), tr("Hide preview"),
                        QKeySequence(Qt::Key_Escape), [this] { HidePreview(); });
    shortcuts_->add(hide, QStringLiteral("hidePreview"));
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("applicationMenu"));
    compareImages_ = menu->addAction(QIcon::fromTheme(QStringLiteral("view-split-left-right")),
                                     tr("Compare Images…"), this, &PreviewWindow::showComparison);
    compareImages_->setObjectName(QStringLiteral("compareImages"));
    compareImages_->setEnabled(false);
    initializeTriage(menu);
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
    auto *refresh = menu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                    tr("Refresh Preview"), this,
                                    [this]
                                    {
                                        rememberPosition();
                                        filmstrip_->invalidateCurrent();
                                        loadCurrent();
                                    });
    refresh->setObjectName(QStringLiteral("refreshPreview"));
    refresh->setShortcuts(KStandardShortcut::reload());
    addAction(refresh);
    connect(menu, &QMenu::aboutToShow, this,
            [this, openWith, reveal, copyLocation, refresh]
            {
                const bool available = !paths_.isEmpty() && currentUrl_.isValid();
                for (auto *item : {openWith, reveal, copyLocation, refresh})
                    item->setEnabled(available);
            });
    menu->addSeparator();
    auto *quit = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                 tr("Quit KGlanceQT"), this, &PreviewWindow::Quit);
    quit->setObjectName(QStringLiteral("quitKGlanceQT"));
    quit->setShortcuts(KStandardShortcut::quit());
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
    auto *filmstripAction = menu->addAction(tr("Show Selection Filmstrip"));
    menu->insertAction(quit, filmstripAction);
    filmstripAction->setObjectName(QStringLiteral("showFilmstrip"));
    filmstripAction->setCheckable(true);
    filmstripAction->setChecked(readPreferences().filmstrip);
    connect(filmstripAction, &QAction::toggled, this,
            [this](bool enabled)
            {
                QSettings().setValue(QStringLiteral("window/filmstrip"), enabled);
                filmstrip_->setVisible(isVisible() && enabled && paths_.size() > 1);
                if (enabled)
                    filmstrip_->setCurrent(index_);
            });
    auto *clearPositions = menu->addAction(tr("Clear Remembered Positions"), this,
                                           [this]
                                           {
                                               this->clearPositions();
                                               loadCurrent();
                                           });
    clearPositions->setObjectName(QStringLiteral("clearPositions"));
    clearPositions->setStatusTip(
        tr("Forget positions in this selection and reset the current preview."));
    menu->insertAction(quit, clearPositions);
    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    auto *configure = KStandardAction::preferences(this, &PreviewWindow::showPreferences, this);
    configure->setText(tr("Configure KGlanceQT…"));
    toolbar->addAction(configure);
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
    auto *keys = KStandardAction::keyBindings(shortcuts_, &AppShortcuts::showDialog, this);
    keys->setObjectName(QStringLiteral("configureShortcuts"));
    menu->insertAction(quit, keys);
    for (auto *item :
         {compareImages_, trashAction_, undoTrashAction_, openWith, reveal, copyLocation, refresh,
          quit, informationAction, filmstripAction, clearPositions, configure, keys})
        shortcuts_->add(item, item->objectName());
    // Space belongs to the preview window only; the file-manager activator remains separate.
    shortcuts_->add(toggle, QStringLiteral("togglePreview"), false);
    shortcuts_->load();
    toggle->setShortcut(QKeySequence(Qt::Key_Space));
    qApp->installEventFilter(this);
}

bool PreviewWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    bool interactive = false;
    for (auto *control = widget; control && control != this; control = control->parentWidget())
    {
        if (qobject_cast<QLineEdit *>(control) || qobject_cast<QAbstractSpinBox *>(control) ||
            qobject_cast<QAbstractSlider *>(control) || qobject_cast<QAbstractButton *>(control) ||
            qobject_cast<QComboBox *>(control) || qobject_cast<QAbstractItemView *>(control) ||
            qobject_cast<QPlainTextEdit *>(control))
        {
            interactive = true;
            break;
        }
    }
    if (isVisible() && widget && widget->window() == this && interactive &&
        event->type() == QEvent::ShortcutOverride)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier &&
            (key->key() == Qt::Key_Space || key->key() == Qt::Key_Left ||
             key->key() == Qt::Key_Right))
        {
            key->accept();
            return true;
        }
    }

    if (isVisible() && widget && widget->window() == this &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress))
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (findChild<QAction *>(QStringLiteral("refreshPreview"))
                ->shortcuts()
                .contains(QKeySequence(key->keyCombination())))
        {
            // Embedded viewers also register Refresh. Keep one window-level
            // operation so the active provider and its monitor refresh together.
            key->accept();
            if (event->type() == QEvent::KeyPress)
                findChild<QAction *>(QStringLiteral("refreshPreview"))->trigger();
            return true;
        }
    }
    if (isVisible() && widget && widget->window() == this && qobject_cast<QVideoWidget *>(widget) &&
        event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
    {
        togglePlayback();
        return true;
    }
    if (isVisible() && paths_.size() > 1 && widget && !interactive && widget->window() == this &&
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
    clearPositions();
    ++selectionSession_;
    if (comparison_)
        comparison_->close();
    fileMonitor_->clear();
    fileNotice_->hide();
    dragFile_->setFile({});
    paths_ = paths;
    index_ = 0;
    filmstrip_->setPaths(paths);
    filmstrip_->setVisible(readPreferences().filmstrip && paths.size() > 1);
    filmstrip_->setCurrent(0);
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
    trashAction_->setEnabled(false);
    compareImages_->setEnabled(false);
    fileMonitor_->clear();
    animationPlayback_->setVisible(false);
    dragFile_->setFile({});
    positionPending_ = false;
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
    delete remote_.data();
}

void PreviewWindow::HidePreview()
{
    information_->closeEditor();
    if (auto *keys = findChild<QDialog *>(QStringLiteral("applicationShortcutsDialog")))
        keys->reject();
    ++selectionSession_;
    if (comparison_)
        comparison_->close();
    if (supportedTypes_)
        supportedTypes_->close();
    if (preferences_)
        preferences_->close();
    QSettings().setValue(QStringLiteral("window/geometry"), saveGeometry());
    hide();
    filmstrip_->clear();
    clearContent();
    clearPositions();
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
    connect(
        preferences_, &PreferencesDialog::applied, this,
        [this]
        {
            const auto settings = readPreferences();
            if (!settings.rememberPositions)
                clearPositions();
            findChild<QAction *>(QStringLiteral("showFilmstrip"))->setChecked(settings.filmstrip);
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

void PreviewWindow::showComparison()
{
    if (!comparison_)
    {
        comparison_ = new ImageComparison(paths_, index_, this);
        comparison_->setAttribute(Qt::WA_DeleteOnClose);
    }
    comparison_->show();
    comparison_->raise();
    comparison_->activateWindow();
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
        if (comparison_)
            comparison_->close();
        rememberPosition();
        index_ = (index_ + offset + paths_.size()) % paths_.size();
        loadCurrent();
    }
}

void PreviewWindow::clearPositions()
{
    imageStates_.clear();
    positions_.clear();
}

void PreviewWindow::rememberPosition()
{
    if (!readPreferences().rememberPositions || positionPending_)
        return;
    auto *widget = content_->currentWidget();
    if (auto *image = dynamic_cast<ImageView *>(widget))
        imageStates_.insert(currentUrl_, image->viewState());
    auto &position = positions_[currentUrl_];
    if (auto *markdown = qobject_cast<MarkdownView *>(widget))
        position.markdown = markdown->position();
    else if (auto *table = qobject_cast<TableView *>(widget))
        position.table = table->position();
    else if (auto *text = qobject_cast<KTextEditor::View *>(widget))
        position.text = textPosition(text);
    else if (auto *media = dynamic_cast<MediaView *>(widget))
        position.media = media->position();
    else if (part_)
        position.page = documentPage(part_);
}

void PreviewWindow::message(const QString &text)
{
    const auto mime = QMimeDatabase().mimeTypeForUrl(currentUrl_);
    auto *surface = new PreviewSurface(mime, content_);
    surface->setAccessibleName(tr("Preview information"));
    auto *layout = new QVBoxLayout(surface);
    auto *label = new QLabel(text, surface);
    label->setTextFormat(Qt::PlainText);
    label->setAccessibleName(text);
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
            if (id == QLatin1String("okularpart"))
            {
                const auto saved = positions_.value(currentUrl_).page.value_or(1U);
                positionPending_ = true;
                const auto request = generation_;
                auto *viewer = part_.data();
                connect(
                    viewer, &KParts::ReadOnlyPart::completed, viewer,
                    [this, viewer, request, saved]
                    {
                        QTimer::singleShot(0, viewer,
                                           [this, viewer, request, saved]
                                           {
                                               if (request != generation_)
                                                   return;
                                               restoreDocumentPage(viewer, saved);
                                               positionPending_ = false;
                                           });
                    },
                    Qt::SingleShotConnection);
            }
            showContent(part_->widget());
            if (part_->openUrl(url))
            {
                return true;
            }
            delete part_.data();
            positionPending_ = false;
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
    fileNotice_->hide();
    filmstrip_->setCurrent(index_);
    const quint64 request = generation_;
    const QString input = paths_.at(index_);
    const QUrl supplied(input);
    currentUrl_ = supplied.scheme().isEmpty() ? QUrl::fromLocalFile(input) : supplied;
    updateTriageActions();
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
    if (!remote_)
        currentUrl_ = url;
    if (!readPreferences().rememberPositions)
        clearPositions();
    const auto request = generation_;
    if (!url.isLocalFile())
    {
        loadRemote(url);
        return;
    }
    const QString path = url.toLocalFile();
    if (!remote_ && !fileMonitor_->setPath(path))
    {
        fileNotice_->setText(tr("Automatic refresh is unavailable for this location."));
        fileNotice_->setMessageType(KMessageWidget::Warning);
        fileNotice_->show();
    }
    const QFileInfo file(path);
    updateTriageActions();
    setWindowTitle(tr("%1 — KGlanceQT").arg(file.fileName()));
    const auto mime = QMimeDatabase().mimeTypeForFile(file);
    compareImages_->setEnabled(!remote_ && file.isFile() && file.isReadable() &&
                               mime.name().startsWith(QLatin1String("image/")));
    if (!remote_ && file.exists() && file.isReadable())
        dragFile_->setFile(url, QIcon::fromTheme(mime.iconName()));
    if (information_->isVisible())
        information_->setFile(currentUrl_);
    info_->setAccessibleName(tr("Preview status"));
    info_->setText(tr("%1 / %2 · %3").arg(index_ + 1).arg(paths_.size()).arg(mime.comment()));
    info_->setToolTip(remote_ ? currentUrl_.toDisplayString() : path);
    if (!file.exists())
    {
        message(tr("This file is missing. It will reload if it reappears.\n%1").arg(path));
    }
    else if (!file.isReadable())
    {
        message(tr("This file cannot be read. Check its access permissions.\n%1").arg(path));
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
        connect(
            watcher, &QFutureWatcher<ImageResult>::finished, this,
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
                    if (result.animation)
                    {
                        auto updatePlayback = [this](bool playing)
                        {
                            animationPlayback_->setText(playing ? tr("Pause animation")
                                                                : tr("Play animation"));
                            animationPlayback_->setIcon(
                                QIcon::fromTheme(playing ? QStringLiteral("media-playback-pause")
                                                         : QStringLiteral("media-playback-start")));
                        };
                        connect(image, &ImageView::animationPlayingChanged, animationPlayback_,
                                updatePlayback);
                        image->setAnimation(*result.animation, readPreferences().autoplayAnimation);
                        animationPlayback_->setVisible(true);
                    }
                    showContent(image);
                    if (!result.notice.isEmpty())
                        info_->setText(info_->text() + QStringLiteral(" · ") + result.notice);
                    if (imageStates_.contains(currentUrl_))
                    {
                        positionPending_ = true;
                        const auto state = imageStates_.value(currentUrl_);
                        QTimer::singleShot(0, image,
                                           [this, image, state]
                                           {
                                               image->restoreViewState(state);
                                               positionPending_ = false;
                                           });
                    }
                }
            });
        const auto settings = readPreferences();
        watcher->setFuture(QtConcurrent::run(
            loadImagePreview, path, workerStop_.get_token(), settings.imageMP,
            AnimationLimits{settings.animationFrames, settings.animationMiB * 1024LL * 1024}));
    }
    else if (mime.name().startsWith(QLatin1String("audio/")) ||
             mime.name().startsWith(QLatin1String("video/")))
    {
        showMedia(url, mime.name());
    }
#ifdef KGLANCEQT_WITH_SQLITE
    else if (mime.inherits(QStringLiteral("application/vnd.sqlite3")) ||
             mime.inherits(QStringLiteral("application/x-sqlite3")) ||
             QStringList{QStringLiteral("sqlite"), QStringLiteral("sqlite3"), QStringLiteral("db")}
                 .contains(file.suffix().toLower()))
    {
        if (remote_)
            message(tr("SQLite previews require a local standalone database; remote copies may "
                       "omit journals."));
        else if (!readPreferences().sqliteEnabled)
            message(tr("SQLite previews are disabled in Configure KGlanceQT…."));
        else
            showContent(new SQLiteView(path, content_));
    }
#endif
    else if (mime.inherits(QStringLiteral("text/csv")) ||
             QStringList{QStringLiteral("csv"), QStringLiteral("tsv")}.contains(
                 file.suffix().toLower()))
    {
        auto *table =
            new TableView(path,
                          file.suffix().compare(QStringLiteral("tsv"), Qt::CaseInsensitive) == 0
                              ? QLatin1Char('\t')
                              : QLatin1Char(','),
                          content_);
        if (const auto saved = positions_.value(currentUrl_).table)
            table->restorePosition(*saved);
        showContent(table);
    }
    else if (mime.inherits(QStringLiteral("text/markdown")) ||
             QStringList{QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mkd")}
                 .contains(file.suffix().toLower()))
    {
        auto *view = new MarkdownView(path, content_);
        if (const auto saved = positions_.value(currentUrl_).markdown)
            view->restorePosition(*saved);
        showContent(view);
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
        document->setModifiedOnDiskWarning(false);
        auto *view = document->createView(content_);
        view->setStatusBarEnabled(false);
        part_ = document;
        showContent(view);
        if (!document->openUrl(url))
        {
            message(tr("Could not open this text file."));
        }
        else if (const auto saved = positions_.value(currentUrl_).text)
        {
            positionPending_ = true;
            QTimer::singleShot(0, view,
                               [this, view, saved]
                               {
                                   restoreTextPosition(view, *saved);
                                   positionPending_ = false;
                               });
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
    else if (!showPart(url, mime.name()))
    {
        showFallback(url, mime.name());
    }
}

void PreviewWindow::refreshCurrent(const QString &path, FileMonitor::Change change)
{
    if (!isVisible() || !currentUrl_.isLocalFile() ||
        QDir::cleanPath(currentUrl_.toLocalFile()) != path)
        return;
    const auto url = currentUrl_;
    filmstrip_->invalidateCurrent();
    // QFileSystemModel already updates live directory listings without losing
    // expansion and selection. Only reload folders when their identity changes.
    if (change == FileMonitor::Change::Modified && QFileInfo(path).isDir())
    {
        if (information_->isVisible())
            information_->setFile(currentUrl_);
        return;
    }
    rememberPosition();
    clearContent();
    fileNotice_->setMessageType(KMessageWidget::Information);
    switch (change)
    {
    case FileMonitor::Change::Modified:
        fileNotice_->setText(tr("File changed on disk. Preview refreshed."));
        break;
    case FileMonitor::Change::Removed:
        fileNotice_->setText(tr("File was removed or moved. Waiting for it to reappear."));
        fileNotice_->setMessageType(KMessageWidget::Warning);
        break;
    case FileMonitor::Change::Replaced:
        fileNotice_->setText(tr("File was replaced on disk. Preview refreshed."));
        break;
    case FileMonitor::Change::Restored:
        fileNotice_->setText(tr("File is available again. Preview refreshed."));
        break;
    case FileMonitor::Change::Unavailable:
        fileNotice_->setText(tr("File is no longer accessible. Check its access permissions."));
        fileNotice_->setMessageType(KMessageWidget::Warning);
        break;
    }
    fileNotice_->show();
    loadLocal(url);
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
    if (const auto saved = positions_.value(currentUrl_).media)
        view->restorePosition(*saved);
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

void PreviewWindow::loadRemote(const QUrl &url)
{
    auto *download = new RemotePreview(this, readPreferences().remoteMiB * 1024LL * 1024);
    remote_ = download;
    const auto request = generation_;
    const int limit = readPreferences().remoteMiB;
    setWindowTitle(tr("%1 — KGlanceQT").arg(url.fileName()));
    info_->setText(tr("%1 / %2 · Remote preview").arg(index_ + 1).arg(paths_.size()));
    info_->setToolTip(url.toDisplayString());
    if (information_->isVisible())
        information_->setFile(url);
    auto *body = new QWidget(content_);
    auto *layout = new QVBoxLayout(body);
    auto *location = new QLabel(tr("Downloading %1").arg(url.toDisplayString()), body);
    location->setTextFormat(Qt::PlainText);
    location->setWordWrap(true);
    layout->addWidget(location);
    auto *progress = new QProgressBar(body);
    progress->setRange(0, 0);
    layout->addWidget(progress);
    auto *status = new QLabel(tr("Connecting…"), body);
    status->setTextFormat(Qt::PlainText);
    layout->addWidget(status);
    auto *cancel = new QPushButton(tr("Cancel transfer"), body);
    cancel->setObjectName(QStringLiteral("cancelRemoteTransfer"));
    layout->addWidget(cancel);
    connect(cancel, &QPushButton::clicked, download, &RemotePreview::cancel);
    connect(
        download, &RemotePreview::progress, body,
        [progress, status, limit](qint64 received, qint64 total)
        {
            if (total > 0)
            {
                progress->setRange(0, 100);
                progress->setValue(int(received * 100 / total));
            }
            status->setText(
                QObject::tr("%1 KiB received (maximum %2 MiB)").arg(received / 1024).arg(limit));
        });
    connect(download, &RemotePreview::failed, this,
            [this, request](const QString &error)
            {
                if (request == generation_)
                    message(error);
            });
    connect(download, &RemotePreview::ready, this,
            [this, request](const QUrl &local)
            {
                if (request == generation_)
                    loadLocal(local);
            });
    showContent(body);
    download->start(url);
}
