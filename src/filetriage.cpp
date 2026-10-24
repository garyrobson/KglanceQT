// SPDX-License-Identifier: AGPL-3.0-only

#include "filetriage.h"

#include <KIO/CopyJob>
#include <KIO/FileUndoManager>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

namespace
{
class UndoUi final : public KIO::FileUndoManager::UiInterface
{
  public:
    explicit UndoUi(std::function<void(const QString &)> error) : error_(std::move(error))
    {
        setShowProgressInfo(false);
    }
    void jobError(KIO::Job *job) override { error_(job->errorString()); }

  private:
    std::function<void(const QString &)> error_;
};
} // namespace

FileTriage::FileTriage(QObject *parent) : QObject(parent)
{
    auto *manager = KIO::FileUndoManager::self();
    manager->setUiInterface(new UndoUi(
        [guard = QPointer<FileTriage>(this)](const QString &message)
        {
            if (guard)
            {
                guard->undoFailed_ = true;
                Q_EMIT guard->error(message);
            }
        }));
    connect(manager, &KIO::FileUndoManager::undoAvailable, this, &FileTriage::stateChanged);
    connect(manager, &KIO::FileUndoManager::undoJobFinished, this,
            [this, manager]
            {
                if (!undoing_)
                    return;
                const auto record = *undoing_;
                undoing_.reset();
                busy_ = false;
                if (!records_.isEmpty() && records_.last().command == record.command &&
                    manager->currentCommandSerialNumber() != record.command)
                    records_.removeLast();
                const QFileInfo file(record.url.toLocalFile());
                if (!undoFailed_ && (file.exists() || file.isSymLink()))
                    Q_EMIT restored(record.url, record.command);
                else if (!undoFailed_)
                    Q_EMIT error(tr("Undo did not restore the original location. "
                                    "Check the Trash in your file manager."));
                Q_EMIT stateChanged();
            });
}

bool FileTriage::busy() const { return busy_; }

bool FileTriage::canUndo() const
{
    auto *manager = KIO::FileUndoManager::self();
    return !busy_ && !records_.isEmpty() && manager->isUndoAvailable() &&
           manager->currentCommandSerialNumber() == records_.last().command;
}

void FileTriage::trash(const QUrl &supplied)
{
    if (busy_)
        return;
    if (!supplied.isLocalFile())
    {
        Q_EMIT error(tr("Only local files can be moved to Trash from this preview."));
        return;
    }
    const QFileInfo file(supplied.toLocalFile());
    if (!file.exists() && !file.isSymLink())
    {
        Q_EMIT error(tr("This file no longer exists."));
        return;
    }
    const auto url = QUrl::fromLocalFile(file.absoluteFilePath());
    auto *manager = KIO::FileUndoManager::self();
    const auto before = manager->currentCommandSerialNumber();
    busy_ = true;
    Q_EMIT stateChanged();
    auto *job = KIO::trash(url, KIO::HideProgressInfo);
    // Keep failures in the preview's operation message. Never fall back to deletion.
    job->setUiDelegate(nullptr);
    job->setUiDelegateExtension(nullptr);
    manager->recordCopyJob(job);
    connect(job, &KJob::result, this,
            [this, job, url, before]
            {
                if (job->error())
                {
                    busy_ = false;
                    Q_EMIT error(job->errorString());
                    Q_EMIT stateChanged();
                    return;
                }
                // Let the native undo recorder finish its result handlers first.
                QTimer::singleShot(
                    0, this,
                    [this, url, before]
                    {
                        auto *manager = KIO::FileUndoManager::self();
                        const auto command = manager->currentCommandSerialNumber();
                        const bool recorded = command != before && manager->isUndoAvailable();
                        if (recorded)
                        {
                            records_.append({url, command});
                            if (records_.size() > 32)
                                records_.removeFirst();
                        }
                        busy_ = false;
                        Q_EMIT trashed(url, recorded ? command : 0);
                        if (!recorded)
                            Q_EMIT error(tr("The file was moved to Trash, but Undo is unavailable. "
                                            "Restore it through your file manager."));
                        Q_EMIT stateChanged();
                    });
            });
}

void FileTriage::undo()
{
    if (!canUndo())
        return;
    const auto record = records_.last();
    const QFileInfo file(record.url.toLocalFile());
    if (file.exists() || file.isSymLink())
    {
        Q_EMIT error(tr(
            "Cannot restore this file because its original location is occupied. "
            "Move the new file elsewhere, or restore the trashed file through your file manager."));
        return;
    }
    undoing_ = record;
    undoFailed_ = false;
    busy_ = true;
    Q_EMIT stateChanged();
    auto *manager = KIO::FileUndoManager::self();
    manager->uiInterface()->setParentWidget(qobject_cast<QWidget *>(parent()));
    manager->undo();
}
