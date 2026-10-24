// SPDX-License-Identifier: AGPL-3.0-only

#include "supportedtypesdialog.h"

#include <KParts/PartLoader>
#include <QDialogButtonBox>
#include <QImageReader>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <utility>

namespace
{
bool hasViewer(const QString &mime, const QString &plugin)
{
    for (const auto &metadata : KParts::PartLoader::partsForMimeType(mime))
        if (metadata.pluginId() == plugin)
            return true;
    return false;
}
} // namespace

SupportedTypesDialog::SupportedTypesDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("supportedTypesDialog"));
    setWindowTitle(tr("Supported Preview Types"));
    resize(640, 540);
    auto *layout = new QVBoxLayout(this);
    auto *details = new QTextBrowser(this);
    details->setObjectName(QStringLiteral("supportedTypesDetails"));
    details->setAccessibleName(tr("Supported preview types"));
    details->setOpenLinks(false);
    details->setOpenExternalLinks(false);
    QString html = QStringLiteral("<h2>%1</h2><p>%2</p>")
                       .arg(tr("What can I preview?").toHtmlEscaped(),
                            tr("Preview support depends on the file contents and the viewers, "
                               "image plugins, and media codecs installed on this computer.")
                                .toHtmlEscaped());
    const auto section = [&html](const QString &title, const QString &text)
    {
        html +=
            QStringLiteral("<h3>%1</h3><p>%2</p>").arg(title.toHtmlEscaped(), text.toHtmlEscaped());
    };
    section(tr("Text and source code"),
            tr("Plain text, logs, JSON, XML, and source files such as C++, Python, and shell "
               "scripts. Files recognized as text open with syntax highlighting where available."));
    section(tr("CSV and TSV"),
            tr("Searchable, sortable read-only tables with selectable cells and TSV copy. All rows "
               "are data. Up to 16 MiB, 10,000 rows, 256 columns and 100,000 cells; partial "
               "results are clearly marked."));
#ifdef KGLANCEQT_WITH_SQLITE
    section(tr("SQLite databases"),
            tr("Optional read-only previews of local standalone SQLite databases. Choose an "
               "ordinary table, search, sort and copy cells. Live WAL/journal databases, views, "
               "generated columns and virtual tables are excluded. Limits: 512 MiB input, five "
               "seconds, 128 tables, 256 columns, 100,000 cells and 4 KiB text cells. Configure "
               "the row limit in preferences."));
#endif
    section(tr("Markdown"),
            tr(".md, .markdown, and .mkd files, and files recognized as Markdown. Rendered preview "
               "and source views, with tables and local images. Large documents open as source."));
    QStringList formats;
    for (const auto &format : QImageReader::supportedImageFormats())
        formats.append(QString::fromLatin1(format).toUpper());
    formats.removeDuplicates();
    formats.sort();
    section(tr("Images available on this computer"),
            (formats.isEmpty() ? tr("No image formats were detected.")
                               : formats.join(QStringLiteral(", ")) + QStringLiteral(".")) +
                QStringLiteral(" ") +
                tr("Animated images play when the installed Qt plugin "
                   "supports animation. Use the toolbar to pause/resume. "
                   "Animations exceeding the frame, memory, or decoding-time "
                   "limit show their first frame with an explanation."));
    section(tr("Audio and video"),
            tr("Common containers include MP3, FLAC, Ogg, WAV, MP4, WebM, and MKV. Playback "
               "depends on the installed media backend and codecs; not every file in a container "
               "format will be playable."));
    section(
        tr("Archives"),
        tr("ZIP, 7z, TAR, and gzip/bzip2/xz/zstd-compressed TAR archives. Preview lists archive "
           "contents without extracting them."));
    QStringList documents;
    for (const auto &[mime, label] : {std::pair{"application/pdf", "PDF"},
                                      {"application/postscript", "PostScript"},
                                      {"application/epub+zip", "EPUB"}})
        if (hasViewer(QString::fromLatin1(mime), QStringLiteral("okularpart")))
            documents.append(QString::fromLatin1(label));
    QStringList office;
    for (const auto &[mime, label] :
         {std::pair{"application/vnd.oasis.opendocument.text", "ODT"},
          {"application/vnd.oasis.opendocument.spreadsheet", "ODS"},
          {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", "DOCX"},
          {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "XLSX"}})
        if (hasViewer(QString::fromLatin1(mime), QStringLiteral("okularpart")))
            office.append(QString::fromLatin1(label));
    section(tr("Office documents and spreadsheets"),
            office.isEmpty() ? tr("No compatible native Office document viewer was detected for "
                                  "ODT, ODS, DOCX or XLSX. Installed KDE thumbnailers may provide "
                                  "previews; use Open in application for the full file.")
                             : tr("Installed native document viewer detected for: %1. Other Office "
                                  "formats may use installed KDE thumbnails.")
                                   .arg(office.join(QStringLiteral(", "))));
    section(tr("Documents"),
            documents.isEmpty()
                ? tr("No supported Okular document viewer was detected. Install Okular to add "
                     "PDF and other document previews.")
                : tr("Installed Okular viewer detected for: %1. Other document types may be "
                     "available through installed Okular plugins.")
                      .arg(documents.join(QStringLiteral(", "))));
    const bool fonts = hasViewer(QStringLiteral("font/ttf"), QStringLiteral("kfontviewpart")) ||
                       hasViewer(QStringLiteral("font/otf"), QStringLiteral("kfontviewpart"));
    section(
        tr("Fonts"),
        fonts ? tr("An installed font viewer provides TrueType/OpenType previews where supported.")
              : tr("Font previews require the KDE font viewer component; none was detected "
                   "for TrueType/OpenType files."));
    section(tr("Folders and other files"),
            tr("Folders show a read-only file listing. Other files may show a thumbnail from an "
               "installed KDE thumbnailer. If no preview is available, use Open in application "
               "or Open With…"));
    section(tr("Preview limits"),
            tr("Local files and remote KIO URLs are previewed. Remote files use cancellable "
               "temporary downloads of up to 128 MiB with a 60-second transfer deadline. Installed "
               "KIO workers determine available protocols. Text, image, and archive limits can be "
               "adjusted in Configure KGlanceQT…. Remote Markdown images are not downloaded."));
    details->setHtml(html);
    layout->addWidget(details);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}
