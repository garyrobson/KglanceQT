// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "previewwindow.h"
#include <QDBusAbstractAdaptor>

// Keep the IPC contract separate from QWidget's inherited public slots.
class PreviewService final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kglanceqt.Preview")
  public:
    explicit PreviewService(PreviewWindow *window) : QDBusAbstractAdaptor(window), window_(window)
    {
    }

  public Q_SLOTS:
    void ShowPreview(const QStringList &paths, const QString &activationToken)
    {
        window_->ShowPreview(paths, activationToken);
    }
    void HidePreview() { window_->HidePreview(); }
    bool IsVisible() const { return window_->IsVisible(); }
    void Quit() { window_->Quit(); }

  private:
    PreviewWindow *window_;
};
