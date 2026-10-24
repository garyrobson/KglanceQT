// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QDockWidget>
#include <QUrl>

class QScrollArea;

class InformationPanel final : public QDockWidget
{
    Q_OBJECT
  public:
    explicit InformationPanel(QWidget *parent = nullptr);
    void setFile(const QUrl &url);
    void clear();

  private:
    QScrollArea *scroll_;
};
