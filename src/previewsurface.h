// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QIcon>
#include <QWidget>

class QMimeType;

// A quiet background for files with no visual content, using the system MIME icon.
class PreviewSurface final : public QWidget
{
  public:
    explicit PreviewSurface(const QMimeType &mime, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QIcon icon_;
};
