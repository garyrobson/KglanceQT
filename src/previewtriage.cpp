// SPDX-License-Identifier: AGPL-3.0-only

#include "filetriage.h"
#include "preferences.h"
#include "previewwindow.h"
#include "selectionfilmstrip.h"

#include <KMessageWidget>
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>

namespace
{
QUrl selectionUrl(const QString &input)
{
    const QUrl supplied(input);
    const auto url = supplied.scheme().isEmpty() ? QUrl::fromLocalFile(input) : supplied;
    return url.isLocalFile() ? QUrl::fromLocalFile(
                                   QDir::cleanPath(QFileInfo(url.toLocalFile()).absoluteFilePath()))
                             : url;
}
} // namespace

void PreviewWindow::initializeTriage(QMenu *menu)
{
    trashAction_ = menu->addAction(QIcon::fromTheme(QStringLiteral("user-trash")),
                                   tr("Move to Trash"), this, &PreviewWindow::trashCurrent);
    trashAction_->setObjectName(QStringLiteral("trashCurrentFile"));
    trashAction_->setEnabled(false);
    undoTrashAction_ = menu->addAction(
        QIcon::fromTheme(QStringLiteral("edit-undo")), tr("Undo Move to Trash"), this,
        [this]
        {
            operationNotice_->setMessageType(KMessageWidget::Information);
            operationNotice_->setText(tr("Restoring the last trashed file…"));
            operationNotice_->show();
            triage_->undo();
        });
    undoTrashAction_->setObjectName(QStringLiteral("undoTrash"));
    undoTrashAction_->setShortcut(QKeySequence::Undo);
    addAction(undoTrashAction_);
    operationNotice_->addAction(undoTrashAction_);
    connect(triage_, &FileTriage::stateChanged, this, &PreviewWindow::updateTriageActions);
    connect(triage_, &FileTriage::trashed, this, &PreviewWindow::selectionTrashed);
    connect(triage_, &FileTriage::restored, this, &PreviewWindow::selectionRestored);
    connect(triage_, &FileTriage::error, this,
            [this](const QString &error)
            {
                operationNotice_->setMessageType(KMessageWidget::Error);
                operationNotice_->setText(error);
                operationNotice_->show();
            });
    updateTriageActions();
}

void PreviewWindow::updateTriageActions()
{
    const QFileInfo file(currentUrl_.toLocalFile());
    trashAction_->setEnabled(isVisible() && !paths_.isEmpty() && currentUrl_.isLocalFile() &&
                             (file.exists() || file.isSymLink()) && !triage_->busy());
    undoTrashAction_->setEnabled(triage_->canUndo());
    const bool available = isVisible() && !paths_.isEmpty() && currentUrl_.isValid();
    for (const auto *name :
         {"openInApplication", "openWith", "showInFileManager", "copyLocation", "refreshPreview"})
        if (auto *action = findChild<QAction *>(QString::fromLatin1(name)))
            action->setEnabled(available);
}

void PreviewWindow::trashCurrent()
{
    if (triage_->busy() || paths_.isEmpty() || !currentUrl_.isLocalFile())
        return;
    trashSession_ = selectionSession_;
    trashInput_ = paths_.at(index_);
    operationNotice_->setMessageType(KMessageWidget::Information);
    operationNotice_->setText(tr("Moving %1 to Trash…").arg(currentUrl_.fileName()));
    operationNotice_->show();
    triage_->trash(currentUrl_);
}

void PreviewWindow::selectionTrashed(const QUrl &url, quint64 command)
{
    RemovedSelection removal;
    removal.session = trashSession_;
    if (removal.session == selectionSession_ && isVisible())
    {
        const int oldIndex = index_;
        int before = 0;
        for (int row = 0; row < paths_.size(); ++row)
            if (paths_.at(row) == trashInput_ || selectionUrl(paths_.at(row)) == url)
            {
                removal.items.append({row, paths_.at(row)});
                before += row < oldIndex;
            }
        rememberPosition();
        for (auto it = removal.items.crbegin(); it != removal.items.crend(); ++it)
            paths_.removeAt(it->first);
        index_ = paths_.isEmpty() ? 0 : (oldIndex - before) % paths_.size();
        updateSelectionView();
    }
    if (command)
    {
        removedSelections_.insert(command, removal);
        while (removedSelections_.size() > 32)
            removedSelections_.erase(removedSelections_.begin());
    }
    operationNotice_->setMessageType(KMessageWidget::Information);
    operationNotice_->setText(tr("Moved %1 to Trash.").arg(url.fileName()));
    operationNotice_->show();
    updateTriageActions();
}

void PreviewWindow::selectionRestored(const QUrl &url, quint64 command)
{
    const auto removal = removedSelections_.take(command);
    if (removal.session == selectionSession_ && isVisible() && !removal.items.isEmpty())
    {
        rememberPosition();
        for (const auto &[row, input] : removal.items)
            paths_.insert(qBound(0, row, int(paths_.size())), input);
        index_ = qBound(0, removal.items.first().first, int(paths_.size()) - 1);
        updateSelectionView();
    }
    operationNotice_->setMessageType(KMessageWidget::Information);
    operationNotice_->setText(tr("Restored %1 from Trash.").arg(url.fileName()));
    operationNotice_->show();
    updateTriageActions();
}

void PreviewWindow::updateSelectionView()
{
    filmstrip_->setPaths(paths_);
    filmstrip_->setVisible(readPreferences().filmstrip && paths_.size() > 1);
    if (!paths_.isEmpty())
        loadCurrent();
    else
    {
        clearContent();
        currentUrl_.clear();
        fileNotice_->hide();
        setWindowTitle(tr("Selection empty — KGlanceQT"));
        info_->setText(tr("No files left in this selection."));
        message(tr("All files in this selection have been moved to Trash.\n"
                   "Use Undo Move to Trash to restore the last file."));
    }
    updateTriageActions();
}
