// SPDX-License-Identifier: AGPL-3.0-only

#include "selectionfilmstrip.h"

#include <KFileItem>
#include <KIO/PreviewJob>
#include <QCache>
#include <QIcon>
#include <QScrollBar>
#include <QStringListModel>
#include <QUrl>

namespace
{
QUrl fileUrl(const QString &path)
{
    const QUrl url(path);
    return url.scheme().isEmpty() ? QUrl::fromLocalFile(path) : url;
}
} // namespace

class FilmstripModel final : public QStringListModel
{
  public:
    explicit FilmstripModel(QObject *parent) : QStringListModel(parent) {}

    QVariant data(const QModelIndex &index, int role) const override
    {
        const auto path = QStringListModel::data(index, Qt::DisplayRole).toString();
        if (role == Qt::DisplayRole)
            return fileUrl(path).fileName();
        if (role == Qt::ToolTipRole || role == Qt::AccessibleTextRole)
            return path;
        if (role == Qt::DecorationRole)
        {
            if (const auto *icon = thumbnails.object(index.row()))
                return *icon;
            return QIcon::fromTheme(QStringLiteral("unknown"));
        }
        return QStringListModel::data(index, role);
    }

    QCache<int, QIcon> thumbnails{32};
};

SelectionFilmstrip::SelectionFilmstrip(QWidget *parent)
    : QListView(parent), files_(new FilmstripModel(this))
{
    setObjectName(QStringLiteral("selectionFilmstrip"));
    setAccessibleName(tr("Selected files"));
    setModel(files_);
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setUniformItemSizes(true);
    setWordWrap(false);
    setTextElideMode(Qt::ElideMiddle);
    setGridSize(QSize(112, 96));
    setIconSize(QSize(80, 64));
    setFixedHeight(120);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(this, &QListView::activated, this,
            [this](const QModelIndex &index) { Q_EMIT fileSelected(index.row()); });
    debounce_.setSingleShot(true);
    debounce_.setInterval(100);
    connect(&debounce_, &QTimer::timeout, this, &SelectionFilmstrip::requestThumbnails);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { debounce_.start(); });
    connect(this, &QListView::clicked, this,
            [this](const QModelIndex &index) { Q_EMIT fileSelected(index.row()); });
}

SelectionFilmstrip::~SelectionFilmstrip() { cancel(); }

void SelectionFilmstrip::cancel()
{
    ++generation_;
    debounce_.stop();
    if (job_)
    {
        job_->kill();
        job_.clear();
    }
}

void SelectionFilmstrip::setPaths(const QStringList &paths)
{
    cancel();
    files_->thumbnails.clear();
    files_->setStringList(paths);
    if (isVisible())
        debounce_.start();
}

void SelectionFilmstrip::clear() { setPaths({}); }

void SelectionFilmstrip::invalidateCurrent()
{
    cancel();
    const int current = currentIndex().row();
    if (current < 0 || current >= files_->rowCount())
        return;
    const auto url = fileUrl(files_->stringList().at(current));
    // Only inspect cached entries; do not scan an arbitrarily large selection.
    for (int row : files_->thumbnails.keys())
    {
        if (fileUrl(files_->stringList().at(row)) == url)
        {
            files_->thumbnails.remove(row);
            const auto item = files_->index(row);
            Q_EMIT files_->dataChanged(item, item, {Qt::DecorationRole});
        }
    }
    if (isVisible())
        debounce_.start();
}

void SelectionFilmstrip::setCurrent(int row)
{
    const auto item = files_->index(row);
    setCurrentIndex(item);
    scrollTo(item, QAbstractItemView::EnsureVisible);
    if (isVisible())
        debounce_.start();
}

void SelectionFilmstrip::showEvent(QShowEvent *event)
{
    QListView::showEvent(event);
    debounce_.start();
}

void SelectionFilmstrip::hideEvent(QHideEvent *event)
{
    cancel();
    files_->thumbnails.clear();
    QListView::hideEvent(event);
}

void SelectionFilmstrip::resizeEvent(QResizeEvent *event)
{
    QListView::resizeEvent(event);
    if (isVisible())
        debounce_.start();
}

void SelectionFilmstrip::requestThumbnails()
{
    cancel();
    if (!isVisible() || files_->rowCount() < 2)
        return;
    // Only request a small neighbourhood of the viewport, even for huge selections.
    auto center = indexAt(viewport()->rect().center());
    if (!center.isValid())
        center = currentIndex();
    const int first = qMax(0, center.row() - 8);
    const int end = qMin(files_->rowCount(), first + 17);
    KFileItemList items;
    QHash<QUrl, QList<int>> rows;
    for (int row = first; row < end; ++row)
    {
        if (files_->thumbnails.contains(row))
            continue;
        const auto url = fileUrl(files_->stringList().at(row));
        // Do not download remote files or resolve virtual locations for thumbnails.
        if (!url.isLocalFile())
            continue;
        if (!rows.contains(url))
            items.append(KFileItem(url, KFileItem::SkipMimeTypeFromContent));
        rows[url].append(row);
    }
    if (items.isEmpty())
        return;
    const auto request = generation_;
    auto *job = KIO::filePreview(items, iconSize());
    job_ = job;
    job->setIgnoreMaximumSize(false);
    job->setScaleType(KIO::PreviewJob::Scaled);
    connect(job, &KIO::PreviewJob::gotPreview, this,
            [this, request, rows](const KFileItem &item, const QPixmap &preview)
            {
                if (request != generation_ || !isVisible())
                    return;
                const auto icon = QIcon(
                    preview.scaled(iconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                for (int row : rows.value(item.url()))
                {
                    files_->thumbnails.insert(row, new QIcon(icon));
                    const auto index = files_->index(row);
                    Q_EMIT files_->dataChanged(index, index, {Qt::DecorationRole});
                }
            });
    // Missing thumbnailers and failed files keep their filename and fallback icon.
}
