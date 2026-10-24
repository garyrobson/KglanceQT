// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "imageview.h"

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
    void clearContent();
    void navigate(int offset);
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
    QStringList paths_;
    QUrl currentUrl_;
    QHash<QUrl, ImageViewState> imageStates_;
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
