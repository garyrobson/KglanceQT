// SPDX-License-Identifier: AGPL-3.0-only

#include "markdownview.h"
#include "providers.h"

#include <KIO/OpenUrlJob>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace
{
constexpr qsizetype renderLimit = 256 * 1024;

struct MarkdownText
{
    QString text;
    QString error;
    bool renderable = false;
};

MarkdownText readMarkdown(const QString &path, qint64 limit, std::stop_token stop)
{
    QFile file(path);
    if (stop.stop_requested())
        return {};
    if (!file.open(QIODevice::ReadOnly))
        return {{}, file.errorString()};
    const auto bytes = file.read(limit + 1);
    if (bytes.size() > limit)
        return {{}, QObject::tr("Markdown exceeds the configured text preview limit.")};
    if (file.error() != QFile::NoError)
        return {{}, file.errorString()};
    if (stop.stop_requested())
        return {};
    // Keep GUI document layout small; file reads and image decoding happen in workers.
    const bool renderable = bytes.size() <= renderLimit && bytes.count('\n') <= 4000;
    return {QString::fromUtf8(bytes), {}, renderable};
}

class MarkdownBrowser final : public QTextBrowser
{
  public:
    MarkdownBrowser(const QString &path, std::stop_token stop, QLabel *notice, QWidget *parent)
        : QTextBrowser(parent), base_(QUrl::fromLocalFile(path)), stop_(stop), notice_(notice)
    {
        setObjectName(QStringLiteral("markdownPreview"));
        setOpenLinks(false);
        setOpenExternalLinks(false);
        document()->setBaseUrl(base_);
        document()->setDocumentMargin(24);
        const auto fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        document()->setDefaultStyleSheet(
            QStringLiteral("pre, code { font-family: '%1'; }").arg(fixed));
        connect(this, &QTextBrowser::anchorClicked, this,
                [this](const QUrl &link)
                {
                    const auto resolved = base_.resolved(link);
                    auto documentUrl = base_;
                    auto target = resolved;
                    documentUrl.setFragment({});
                    target.setFragment({});
                    if (target == documentUrl && resolved.hasFragment())
                        scrollToAnchor(resolved.fragment());
                    else if (resolved.isLocalFile() ||
                             resolved.scheme() == QLatin1String("https") ||
                             resolved.scheme() == QLatin1String("http") ||
                             resolved.scheme() == QLatin1String("mailto"))
                        (new KIO::OpenUrlJob(resolved))->start();
                });
    }

  protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        QImage placeholder(20, 20, QImage::Format_ARGB32_Premultiplied);
        placeholder.fill(Qt::transparent);
        const auto resolved = base_.resolved(name);
        if (type != QTextDocument::ImageResource || !resolved.isLocalFile())
            return placeholder;
        if (requested_.contains(resolved))
            return placeholder;
        if (requested_.size() >= 16)
        {
            notice_->setText(tr("Showing at most 16 local images."));
            notice_->show();
            return placeholder;
        }
        requested_.insert(resolved);
        auto *watcher = new QFutureWatcher<ImageResult>(this);
        connect(watcher, &QFutureWatcher<ImageResult>::finished, this,
                [this, watcher, name]
                {
                    auto result = watcher->result();
                    watcher->deleteLater();
                    if (stop_.stop_requested())
                        return;
                    if (result.image.isNull())
                    {
                        notice_->setText(tr("An image could not be shown: %1").arg(result.error));
                        notice_->show();
                        return;
                    }
                    document()->addResource(QTextDocument::ImageResource, name, result.image);
                    document()->markContentsDirty(0, document()->characterCount());
                    viewport()->update();
                });
        watcher->setFuture(QtConcurrent::run(
            [path = resolved.toLocalFile(), stop = stop_]
            {
                auto result = loadImage(path, stop, 4);
                if (result.image.width() > 1024 || result.image.height() > 1024)
                    result.image = result.image.scaled(1024, 1024, Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation);
                return result;
            }));
        return placeholder;
    }

  private:
    QUrl base_;
    std::stop_token stop_;
    QLabel *notice_;
    QSet<QUrl> requested_;
};

void addHeadingAnchors(QTextDocument *document)
{
    QHash<QString, int> slugs;
    for (auto block = document->begin(); block.isValid(); block = block.next())
    {
        if (!block.blockFormat().headingLevel() || block.text().isEmpty())
            continue;
        auto slug = block.text().toLower();
        slug.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_\\-\\s]")));
        slug.replace(QRegularExpression(QStringLiteral("\\s")), QStringLiteral("-"));
        const int occurrence = slugs[slug]++;
        if (occurrence)
            slug += QStringLiteral("-%1").arg(occurrence);
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        QTextCharFormat anchor;
        anchor.setAnchor(true);
        anchor.setAnchorNames({slug});
        cursor.mergeCharFormat(anchor);
    }
}
} // namespace

MarkdownView::MarkdownView(const QString &path, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("markdownView"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *tabs = new QTabBar(this);
    tabs->setObjectName(QStringLiteral("markdownMode"));
    tabs->addTab(tr("Preview"));
    tabs->addTab(tr("Source"));
    layout->addWidget(tabs);
    auto *notice = new QLabel(tr("Loading Markdown…"), this);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *search = new QToolBar(this);
    auto *query = new QLineEdit(search);
    query->setObjectName(QStringLiteral("markdownSearch"));
    query->setPlaceholderText(tr("Find in preview…"));
    query->setClearButtonEnabled(true);
    search->addWidget(query);
    auto *previous =
        search->addAction(QIcon::fromTheme(QStringLiteral("go-up")), tr("Previous match"));
    auto *next = search->addAction(QIcon::fromTheme(QStringLiteral("go-down")), tr("Next match"));
    auto *close =
        search->addAction(QIcon::fromTheme(QStringLiteral("dialog-close")), tr("Close search"));
    layout->addWidget(search);
    search->hide();
    auto *pages = new QStackedWidget(this);
    layout->addWidget(pages, 1);
    auto *browser = new MarkdownBrowser(path, stop_.get_token(), notice, pages);
    pages->addWidget(browser);
    auto *sourcePage = new QWidget(pages);
    auto *sourceLayout = new QVBoxLayout(sourcePage);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    pages->addWidget(sourcePage);
    auto *find = new QShortcut(QKeySequence::Find, this);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QShortcut::activated, this,
            [search, query]
            {
                search->show();
                query->setFocus();
            });
    connect(close, &QAction::triggered, this,
            [search, browser]
            {
                search->hide();
                browser->setFocus();
            });
    auto findText = [browser, query](bool backwards)
    {
        const auto flags = backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
        if (!browser->find(query->text(), flags))
        {
            auto cursor = browser->textCursor();
            cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
            browser->setTextCursor(cursor);
            browser->find(query->text(), flags);
        }
    };
    connect(query, &QLineEdit::returnPressed, this, [findText] { findText(false); });
    connect(next, &QAction::triggered, this, [findText] { findText(false); });
    connect(previous, &QAction::triggered, this, [findText] { findText(true); });
    auto *zoomIn = new QShortcut(QKeySequence::ZoomIn, browser);
    auto *zoomOut = new QShortcut(QKeySequence::ZoomOut, browser);
    connect(zoomIn, &QShortcut::activated, browser, [browser] { browser->zoomIn(); });
    connect(zoomOut, &QShortcut::activated, browser, [browser] { browser->zoomOut(); });
    connect(tabs, &QTabBar::currentChanged, this,
            [pages, find, search](int index)
            {
                pages->setCurrentIndex(index);
                find->setEnabled(index == 0);
                if (index)
                    search->hide();
                pages->currentWidget()->setFocus();
            });
    auto *watcher = new QFutureWatcher<MarkdownText>(this);
    connect(
        watcher, &QFutureWatcher<MarkdownText>::finished, this,
        [this, watcher, sourceLayout, tabs, browser, notice]
        {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (stop_.stop_requested())
                return;
            if (!result.error.isEmpty())
            {
                notice->setText(result.error);
                tabs->setEnabled(false);
                return;
            }
            auto *document = KTextEditor::Editor::instance()->createDocument(this);
            document->setText(result.text);
            document->setHighlightingMode(QStringLiteral("Markdown"));
            document->setReadWrite(false);
            auto *source = document->createView(sourceLayout->parentWidget());
            source->setStatusBarEnabled(false);
            sourceLayout->addWidget(source);
            if (result.renderable)
            {
                notice->clear();
                notice->hide();
                auto features =
                    QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub);
                features |= QTextDocument::MarkdownNoHTML;
                browser->document()->setMarkdown(result.text, features);
                addHeadingAnchors(browser->document());
                tabs->setCurrentIndex(
                    QSettings().value(QStringLiteral("markdown/source"), false).toBool() ? 1 : 0);
            }
            else
            {
                notice->setText(
                    tr("Rendered preview is limited to 256 KiB and 4,000 lines. Showing source."));
                tabs->setTabEnabled(0, false);
                tabs->setCurrentIndex(1);
            }
            if (!tabs->currentIndex())
                browser->setFocus();
        });
    const auto limit =
        qBound(1, QSettings().value(QStringLiteral("limits/textMiB"), 16).toInt(), 16) * 1024LL *
        1024;
    watcher->setFuture(QtConcurrent::run(readMarkdown, path, limit, stop_.get_token()));
}

MarkdownView::~MarkdownView() { stop_.request_stop(); }
