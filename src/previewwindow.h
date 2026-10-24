// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "filemonitor.h"
#include "imageview.h"
#include "viewposition.h"

#include <QHash>
#include <QMainWindow>
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
    QStackedWidget *content_;
    QLabel *info_;
    InformationPanel *information_;
    SelectionFilmstrip *filmstrip_;
    FileDragButton *dragFile_;
    QAction *animationPlayback_;
    FileMonitor *fileMonitor_;
    KMessageWidget *fileNotice_;
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
