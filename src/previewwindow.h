// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "filemonitor.h"
#include "imageview.h"
#include "viewposition.h"

#include <QHash>
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <stop_token>

class QLabel;
class QMediaPlayer;
class QStackedWidget;
class KJob;
class InformationPanel;
class PreferencesDialog;
class SupportedTypesDialog;
class SelectionFilmstrip;
class FileDragButton;
class QAction;
class KMessageWidget;
class ImageComparison;
class FileTriage;
class QMenu;
class AppShortcuts;
class RemotePreview;
namespace KParts
{
class ReadOnlyPart;
}

class PreviewWindow final : public QMainWindow
{
    Q_OBJECT
  public:
    explicit PreviewWindow(bool standalone);
    void ShowPreview(const QStringList &paths, const QString &activationToken);
    void HidePreview();
    bool IsVisible() const;
    void Quit();

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    void loadCurrent();
    void loadLocal(const QUrl &url);
    void loadRemote(const QUrl &url);
    void refreshCurrent(const QString &path, FileMonitor::Change change);
    void clearContent();
    void navigate(int offset);
    void rememberPosition();
    void clearPositions();
    void message(const QString &text);
    void showContent(QWidget *widget);
    bool showPart(const QUrl &url, const QString &mime);
    void showFallback(const QUrl &url, const QString &mime);
    void showMedia(const QUrl &url, const QString &mime);
    void togglePlayback();
    void showPreferences();
    void showSupportedTypes();
    void showComparison();
    void initializeTriage(QMenu *menu);
    void updateTriageActions();
    void trashCurrent();
    void selectionTrashed(const QUrl &url, quint64 command);
    void selectionRestored(const QUrl &url, quint64 command);
    void updateSelectionView();
    QStackedWidget *content_;
    QLabel *info_;
    InformationPanel *information_;
    SelectionFilmstrip *filmstrip_;
    FileDragButton *dragFile_;
    QAction *animationPlayback_;
    FileMonitor *fileMonitor_;
    KMessageWidget *fileNotice_;
    QAction *compareImages_;
    QPointer<ImageComparison> comparison_;
    QPointer<RemotePreview> remote_;
    AppShortcuts *shortcuts_;
    FileTriage *triage_;
    KMessageWidget *operationNotice_;
    QAction *trashAction_;
    QAction *undoTrashAction_;
    struct RemovedSelection
    {
        quint64 session = 0;
        QList<QPair<int, QString>> items;
    };
    QMap<quint64, RemovedSelection> removedSelections_;
    quint64 selectionSession_ = 0;
    quint64 trashSession_ = 0;
    QString trashInput_;
    QStringList paths_;
    QUrl currentUrl_;
    QHash<QUrl, ImageViewState> imageStates_;
    QHash<QUrl, PreviewPosition> positions_;
    bool positionPending_ = false;
    int index_ = 0;
    quint64 generation_ = 0;
    std::stop_source workerStop_;
    bool standalone_;
    QPointer<KParts::ReadOnlyPart> part_;
    QPointer<QMediaPlayer> player_;
    QPointer<KJob> thumbnailJob_;
    QPointer<PreferencesDialog> preferences_;
    QPointer<SupportedTypesDialog> supportedTypes_;
};
