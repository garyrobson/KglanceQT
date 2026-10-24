// SPDX-License-Identifier: AGPL-3.0-only
#include "metadataeditor.h"
#include <KFileMetaData/UserMetaData>
#include <KRatingWidget>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <sys/stat.h>
namespace
{
struct Metadata
{
    quint64 device = 0, inode = 0;
    int rating = 0;
    QStringList tags;
    QString comment, error;
};
Metadata readMetadata(const QString &path, std::stop_token stop)
{
    Metadata result;
    struct stat identity{};
    if (stop.stop_requested())
        return result;
    if (::stat(QFile::encodeName(path).constData(), &identity) != 0)
    {
        result.error = QObject::tr("The file is missing or inaccessible.");
        return result;
    }
    result.device = identity.st_dev;
    result.inode = identity.st_ino;
    KFileMetaData::UserMetaData metadata(path);
    if (!metadata.isSupported())
    {
        result.error = QObject::tr("This filesystem does not support KDE user metadata.");
        return result;
    }
    result.rating = metadata.rating();
    result.tags = metadata.tags();
    result.comment = metadata.userComment();
    return result;
}
QString writeMetadata(const QString &path, const Metadata &data, std::stop_token stop)
{
    if (stop.stop_requested())
        return QObject::tr("Save cancelled.");
    struct stat identity{};
    if (::stat(QFile::encodeName(path).constData(), &identity) != 0 ||
        quint64(identity.st_dev) != data.device || quint64(identity.st_ino) != data.inode)
        return QObject::tr("The file was replaced or removed. Reopen the editor before saving.");
    KFileMetaData::UserMetaData metadata(path);
    if (!metadata.isSupported())
        return QObject::tr("This filesystem does not support KDE user metadata.");
    // Finish this explicitly requested batch even if its dialog is closed.
    const auto rating = metadata.setRating(data.rating);
    const auto tags = metadata.setTags(data.tags);
    const auto comment = metadata.setUserComment(data.comment);
    auto description = [](KFileMetaData::UserMetaData::Error error)
    {
        using Error = KFileMetaData::UserMetaData;
        switch (error)
        {
        case Error::NoError:
            return QString{};
        case Error::NotSupported:
            return QObject::tr("filesystem does not support this attribute");
        case Error::NoSpace:
            return QObject::tr("no space available for metadata");
        case Error::MissingPermission:
            return QObject::tr("write permission denied");
        case Error::ValueTooBig:
            return QObject::tr("value exceeds the filesystem limit");
        case Error::NameToolong:
            return QObject::tr("attribute name exceeds the filesystem limit");
        default:
            return QObject::tr("filesystem write failed");
        }
    };
    QStringList failures;
    if (rating != KFileMetaData::UserMetaData::NoError)
        failures << QObject::tr("Rating: %1").arg(description(rating));
    if (tags != KFileMetaData::UserMetaData::NoError)
        failures << QObject::tr("Tags: %1").arg(description(tags));
    if (comment != KFileMetaData::UserMetaData::NoError)
        failures << QObject::tr("Comment: %1").arg(description(comment));
    if (!failures.isEmpty())
        return QObject::
            tr("Some metadata could not be saved. Other fields may already have been saved.\n%1")
                .arg(failures.join(QLatin1Char('\n')));
    return {};
}
} // namespace
MetadataEditor::MetadataEditor(const QUrl &url, QWidget *parent) : QDialog(parent), url_(url)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Edit Ratings, Tags and Comment"));
    resize(480, 450);
    auto *layout = new QVBoxLayout(this);
    auto *location = new QLabel(url.toDisplayString(), this);
    location->setTextFormat(Qt::PlainText);
    location->setWordWrap(true);
    layout->addWidget(location);
    auto *form = new QFormLayout;
    rating_ = new KRatingWidget(this);
    rating_->setHalfStepsEnabled(true);
    rating_->setAccessibleName(tr("File rating"));
    tags_ = new QPlainTextEdit(this);
    tags_->setObjectName(QStringLiteral("metadataTags"));
    tags_->setAccessibleName(tr("File tags, one per line"));
    comment_ = new QPlainTextEdit(this);
    comment_->setObjectName(QStringLiteral("metadataComment"));
    comment_->setAccessibleName(tr("File comment"));
    form->addRow(tr("Rating:"), rating_);
    form->addRow(tr("Tags (one per line):"), tags_);
    form->addRow(tr("Comment:"), comment_);
    layout->addLayout(form);
    notice_ = new QLabel(tr("Reading metadata…"), this);
    notice_->setObjectName(QStringLiteral("metadataStatus"));
    notice_->setTextFormat(Qt::PlainText);
    notice_->setWordWrap(true);
    layout->addWidget(notice_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(apply_, &QPushButton::clicked, this, &MetadataEditor::save);
    rating_->setEnabled(false);
    tags_->setEnabled(false);
    comment_->setEnabled(false);
    auto *watcher = new QFutureWatcher<Metadata>(this);
    connect(watcher, &QFutureWatcher<Metadata>::finished, this,
            [this, watcher]
            {
                const auto data = watcher->result();
                watcher->deleteLater();
                notice_->setText(data.error);
                if (!data.error.isEmpty())
                    return;
                device_ = data.device;
                inode_ = data.inode;
                rating_->setRating(data.rating);
                tags_->setPlainText(data.tags.join(QLatin1Char('\n')));
                comment_->setPlainText(data.comment);
                rating_->setEnabled(true);
                tags_->setEnabled(true);
                comment_->setEnabled(true);
                apply_->setEnabled(true);
            });
    watcher->setFuture(QtConcurrent::run(readMetadata, url.toLocalFile(), stop_.get_token()));
}
MetadataEditor::~MetadataEditor() { stop_.request_stop(); }
void MetadataEditor::save()
{
    const auto tags = tags_->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (tags.size() > 64 ||
        std::any_of(tags.begin(), tags.end(),
                    [](const QString &tag) { return tag.size() > 128; }) ||
        comment_->toPlainText().size() > 4000)
    {
        notice_->setText(tr("Use at most 64 tags of 128 characters each, and a comment of at most "
                            "4000 characters."));
        return;
    }
    if (std::any_of(tags.begin(), tags.end(),
                    [](const QString &tag) { return tag.contains(QLatin1Char(',')); }))
    {
        notice_->setText(
            tr("KDE stores tags separated by commas. Enter tags without commas, one per line."));
        return;
    }
    apply_->setEnabled(false);
    rating_->setEnabled(false);
    tags_->setEnabled(false);
    comment_->setEnabled(false);
    notice_->setText(tr("Saving…"));
    Metadata data{device_, inode_, rating_->rating(), tags, comment_->toPlainText(), {}};
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher]
            {
                const auto error = watcher->result();
                watcher->deleteLater();
                notice_->setText(
                    error.isEmpty() ? tr("Saved. These values are shared with Dolphin.") : error);
                apply_->setEnabled(true);
                rating_->setEnabled(true);
                tags_->setEnabled(true);
                comment_->setEnabled(true);
                if (error.isEmpty())
                    Q_EMIT saved(url_);
            });
    watcher->setFuture(
        QtConcurrent::run(writeMetadata, url_.toLocalFile(), data, stop_.get_token()));
}
