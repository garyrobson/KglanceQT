// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QToolButton>
#include <QUrl>

class FileDragButton final : public QToolButton
{
    Q_OBJECT
  public:
    explicit FileDragButton(QWidget *parent = nullptr);
    void setFile(const QUrl &url, const QIcon &icon = {});

  protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

  private:
    QUrl url_;
    QPoint pressPosition_;
    bool pressed_ = false;
};
