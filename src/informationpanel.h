// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QDockWidget>
#include <QPointer>
#include <QUrl>
class MetadataEditor;

class QScrollArea;

class InformationPanel final : public QDockWidget
{
    Q_OBJECT
  public:
    explicit InformationPanel(QWidget *parent = nullptr);
    void setFile(const QUrl &url);
    void clear();
    void closeEditor();

  private:
    QScrollArea *scroll_;
    QPointer<MetadataEditor> editor_;
};
