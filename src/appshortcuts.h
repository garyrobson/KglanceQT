// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QObject>
class KActionCollection;
class QAction;
class QWidget;
class QMenu;
class KShortcutsDialog;
class AppShortcuts final : public QObject
{
    Q_OBJECT
  public:
    explicit AppShortcuts(QWidget *window);
    void add(QAction *action, const QString &name, bool configurable = true);
    void load();
    void showDialog();

  private:
    QWidget *window_;
    KActionCollection *collection_;
};
