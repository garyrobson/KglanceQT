// SPDX-License-Identifier: AGPL-3.0-only

#include "informationpanel.h"
#include "metadataeditor.h"
#include <QPushButton>

#include <KFileItem>
#include <KIO/OpenUrlJob>
#include <KRatingWidget>
#include <QDateTime>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QMimeDatabase>
#include <QScrollArea>
#include <QVBoxLayout>
#include <baloo/filemetadatawidget.h>

InformationPanel::InformationPanel(QWidget *parent)
    : QDockWidget(tr("Information"), parent), scroll_(new QScrollArea(this))
{
    setObjectName(QStringLiteral("informationPanel"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable);
    scroll_->setWidgetResizable(true);
    scroll_->setMinimumWidth(260);
    setWidget(scroll_);
    clear();
}

void InformationPanel::clear()
{
    // Destroy the provider as well as its widgets to cancel outstanding requests.
    delete scroll_->takeWidget();
    scroll_->setWidget(new QWidget(scroll_));
}

void InformationPanel::setFile(const QUrl &url)
{
    clear();
    if (!url.isLocalFile())
    {
        auto *layout = new QVBoxLayout(scroll_->widget());
        auto *location = new QLabel(url.toDisplayString(), scroll_->widget());
        location->setTextFormat(Qt::PlainText);
        location->setWordWrap(true);
        location->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(location);
        auto *notice = new QLabel(tr("Remote preview. Metadata editing requires a local file."),
                                  scroll_->widget());
        notice->setWordWrap(true);
        layout->addWidget(notice);
        layout->addStretch();
        return;
    }
    const QFileInfo file(url.toLocalFile());
    const auto mime = QMimeDatabase().mimeTypeForFile(file);
    auto *body = scroll_->widget();
    auto *layout = new QVBoxLayout(body);
    auto *name = new QLabel(file.fileName(), body);
    name->setObjectName(QStringLiteral("informationName"));
    name->setTextFormat(Qt::PlainText);
    name->setWordWrap(true);
    name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto font = name->font();
    font.setBold(true);
    name->setFont(font);
    layout->addWidget(name);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout->addLayout(form);
    auto row = [form, body](const QString &key, const QString &value)
    {
        auto *label = new QLabel(value, body);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(key, label);
    };
    row(tr("Location:"), file.absoluteFilePath());
    const bool detailed = file.isFile() && file.isReadable() && file.size() <= 256LL * 1024 * 1024;
    if (!detailed)
    {
        row(tr("Type:"), mime.comment());
        if (file.isFile())
            row(tr("Size:"), QLocale().formattedDataSize(file.size()));
        if (file.lastModified().isValid())
            row(tr("Modified:"), QLocale().toString(file.lastModified(), QLocale::ShortFormat));
        if (file.birthTime().isValid())
            row(tr("Created:"), QLocale().toString(file.birthTime(), QLocale::ShortFormat));
    }
    // Dolphin's public widget uses an asynchronous metadata provider. Avoid its
    // synchronous directory counting path, and bound files sent for extraction.
    if (detailed)
    {
        auto *metadata = new Baloo::FileMetaDataWidget(body);
        metadata->setObjectName(QStringLiteral("fileMetadata"));
        metadata->setReadOnly(true);
        layout->addWidget(metadata);
        connect(metadata, &Baloo::FileMetaDataWidget::urlActivated, this,
                [](const QUrl &link) { (new KIO::OpenUrlJob(link))->start(); });
        connect(metadata, &Baloo::FileMetaDataWidget::metaDataRequestFinished, metadata,
                [metadata]
                {
                    // Some BalooWidgets versions recompute readOnly after fetching.
                    // Rebuild via the public API before allowing user interaction.
                    if (!metadata->isReadOnly())
                    {
                        metadata->setReadOnly(true);
                        metadata->setConfigurationMode(Baloo::ConfigurationMode::Cancel);
                    }
                    for (auto *rating : metadata->findChildren<KRatingWidget *>())
                        rating->setEnabled(false);
                });
        metadata->setItems({KFileItem(url, mime.name(), 0)});
    }
    else if (file.size() > 256LL * 1024 * 1024)
    {
        auto *notice = new QLabel(tr("Detailed metadata is limited to files up to 256 MiB."), body);
        notice->setWordWrap(true);
        layout->addWidget(notice);
    }
    auto *edit = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")),
                                 tr("Edit Ratings, Tags and Comment…"), body);
    edit->setObjectName(QStringLiteral("editUserMetadata"));
    layout->addWidget(edit);
    connect(edit, &QPushButton::clicked, this,
            [this, url]
            {
                if (editor_)
                {
                    editor_->raise();
                    return;
                }
                editor_ = new MetadataEditor(url, window());
                editor_->show();
            });
    layout->addStretch();
}

void InformationPanel::closeEditor()
{
    if (editor_)
        editor_->close();
}
