// SPDX-License-Identifier: AGPL-3.0-only
#include "appshortcuts.h"
#include <KActionCollection>
#include <KConfigGroup>
#include <KMessageBox>
#include <KSharedConfig>
#include <KShortcutsDialog>
#include <QAction>
#include <QPointer>
#include <QWidget>
AppShortcuts::AppShortcuts(QWidget *window)
    : QObject(window), window_(window), collection_(new KActionCollection(this))
{
    setObjectName(QStringLiteral("applicationShortcuts"));
    collection_->setObjectName(QStringLiteral("shortcutActions"));
    collection_->addAssociatedWidget(window);
}
void AppShortcuts::add(QAction *action, const QString &name, bool configurable)
{
    action->setObjectName(name);
    action->setShortcutContext(Qt::WindowShortcut);
    KActionCollection::setDefaultShortcuts(action, action->shortcuts());
    collection_->addAction(name, action);
    collection_->setShortcutsConfigurable(action, configurable);
}
void AppShortcuts::load()
{
    auto config =
        KSharedConfig::openConfig(QStringLiteral("kglanceqtshortcutsrc"), KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("Shortcuts"));
    collection_->readSettings(&group);
}
void AppShortcuts::showDialog()
{
    if (auto *existing =
            window_->findChild<KShortcutsDialog *>(QStringLiteral("applicationShortcutsDialog")))
    {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *dialog = new KShortcutsDialog(KShortcutsEditor::WidgetAction,
                                        KShortcutsEditor::LetterShortcutsDisallowed, window_);
    dialog->setObjectName(QStringLiteral("applicationShortcutsDialog"));

    dialog->setModal(true);
    dialog->addCollection(collection_, tr("KGlanceQT"));
    // Use KDE's native editor without writing to the application's QSettings file.
    const bool accepted = dialog->configure(false);
    if (accepted)
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("kglanceqtshortcutsrc"),
                                                KConfig::SimpleConfig);
        KConfigGroup group(config, QStringLiteral("Shortcuts"));
        collection_->writeSettings(&group);
        if (!config->sync())
            KMessageBox::error(
                window_, tr("Could not save keyboard shortcuts. Check that the configuration "
                            "directory is writable. The new bindings apply only to this session."));
    }
    dialog->deleteLater();
}
