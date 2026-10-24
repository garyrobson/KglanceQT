// SPDX-License-Identifier: AGPL-3.0-only
#include "remotepreview.h"
#include <KIO/TransferJob>
#include <QFileInfo>
#include <QRegularExpression>
RemotePreview::RemotePreview(QObject *parent, qint64 maxBytes, int timeoutMs)
    : QObject(parent), limit_(qBound<qint64>(1LL, maxBytes, 128LL * 1024 * 1024))
{
    timer_.setSingleShot(true);
    timer_.setInterval(qBound(1, timeoutMs, 60000));
    connect(&timer_, &QTimer::timeout, this,
            [this] { fail(tr("Remote preview timed out after waiting for the transfer.")); });
}
RemotePreview::~RemotePreview()
{
    if (job_)
        job_->kill();
    file_.close();
}
void RemotePreview::start(const QUrl &url)
{
    if (started_ || finished_)
        return;
    started_ = true;
    if (!directory_.isValid())
    {
        fail(tr("Could not create private temporary storage."));
        return;
    }
    QString name = QFileInfo(url.path()).fileName().left(180);
    name.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}._ -]")), QStringLiteral("_"));
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("preview");
    file_.setFileName(directory_.filePath(name));
    if (!file_.open(QIODevice::WriteOnly))
    {
        fail(file_.errorString());
        return;
    }
    file_.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    auto *job = KIO::get(url, KIO::NoReload, KIO::HideProgressInfo);
    job_ = job;
    connect(
        job, &KJob::totalSize, this,
        [this](KJob *, qulonglong size)
        {
            if (finished_)
                return;
            if (size > qulonglong(limit_))
            {
                fail(
                    tr("Remote file exceeds the %1 MiB transfer limit.").arg(limit_ / 1024 / 1024));
                return;
            }
            total_ = size;
            Q_EMIT progress(received_, total_);
        });
    connect(job, &KIO::TransferJob::data, this,
            [this](KIO::Job *, const QByteArray &bytes)
            {
                if (finished_)
                    return;
                if (bytes.size() > limit_ - received_)
                {
                    fail(
                        tr("Remote transfer exceeded the %1 MiB limit.").arg(limit_ / 1024 / 1024));
                    return;
                }
                if (file_.write(bytes) != bytes.size())
                {
                    fail(tr("Could not save the temporary preview: %1").arg(file_.errorString()));
                    return;
                }
                received_ += bytes.size();
                Q_EMIT progress(received_, total_);
            });
    connect(job, &KJob::result, this,
            [this, job]
            {
                if (finished_)
                    return;
                if (job->error())
                {
                    fail(job->errorString());
                    return;
                }
                if (!file_.flush())
                {
                    fail(file_.errorString());
                    return;
                }
                file_.close();
                timer_.stop();
                finished_ = true;
                Q_EMIT ready(QUrl::fromLocalFile(file_.fileName()));
            });
    timer_.start();
}
void RemotePreview::cancel()
{
    if (!finished_)
        fail(tr("Remote preview cancelled."));
}
void RemotePreview::fail(const QString &error)
{
    if (finished_)
        return;
    finished_ = true;
    timer_.stop();
    if (job_)
        job_->kill();
    file_.close();
    if (!file_.fileName().isEmpty())
        file_.remove();
    Q_EMIT failed(error);
}
