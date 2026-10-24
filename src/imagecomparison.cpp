// SPDX-License-Identifier: AGPL-3.0-only

#include "imagecomparison.h"
#include "filemonitor.h"
#include "imageview.h"
#include "preferences.h"
#include "providers.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColorSpace>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace
{
struct ComparisonImage
{
    ImageResult result;
    QMap<QString, QString> metadata;
};

QUrl imageUrl(const QString &path)
{
    const QUrl url(path);
    return url.scheme().isEmpty() ? QUrl::fromLocalFile(path) : url;
}

ComparisonImage readComparison(const QString &path, std::stop_token stop, int maxMP)
{
    ComparisonImage result{loadImage(path, stop, qMin(maxMP, 32)), {}};
    if (result.result.image.isNull() || stop.stop_requested())
        return result;
    const auto &image = result.result.image;
    const QFileInfo file(path);
    auto &data = result.metadata;
    data.insert(QObject::tr("Dimensions"),
                QStringLiteral("%1 × %2").arg(image.width()).arg(image.height()));
    data.insert(QObject::tr("File size"), QObject::tr("%1 (%2 bytes)")
                                              .arg(QLocale().formattedDataSize(file.size()))
                                              .arg(file.size()));
    data.insert(QObject::tr("Modified"), file.lastModified().toString(Qt::ISODateWithMs));
    data.insert(QObject::tr("Format"),
                QString::fromLatin1(QImageReader::imageFormat(path)).toUpper());
    data.insert(QObject::tr("Depth"), QObject::tr("%1 bits/pixel").arg(image.depth()));
    data.insert(QObject::tr("Alpha channel"),
                image.hasAlphaChannel() ? QObject::tr("Yes") : QObject::tr("No"));
    const auto space = image.colorSpace();
    data.insert(QObject::tr("Colour space"),
                space.isValid() ? (space.description().isEmpty() ? QObject::tr("Embedded profile")
                                                                 : space.description())
                                : QObject::tr("Unspecified"));
    data.insert(QObject::tr("Resolution"), QObject::tr("%1 × %2 dpi")
                                               .arg(qRound(image.dotsPerMeterX() * 0.0254))
                                               .arg(qRound(image.dotsPerMeterY() * 0.0254)));
    const auto keys = image.textKeys();
    for (int i = 0; i < qMin(32, int(keys.size())); ++i)
        data.insert(QObject::tr("Embedded: %1").arg(keys.at(i).left(128)),
                    image.text(keys.at(i)).left(4096));
    return result;
}
} // namespace

ImageComparison::ImageComparison(const QStringList &paths, int current, QWidget *parent)
    : QDialog(parent), linked_(new QCheckBox(tr("Link zoom and pan"), this)),
      metadata_(new QTableWidget(this)), differences_(new QLabel(this))
{
    setObjectName(QStringLiteral("imageComparison"));
    setWindowTitle(tr("Compare Images — KGlanceQT"));
    syncTimer_.setSingleShot(true);
    connect(&syncTimer_, &QTimer::timeout, this, [this] { syncViews(syncSource_); });
    resize(1100, 740);
    auto *layout = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    linked_->setObjectName(QStringLiteral("linkedImageViews"));
    controls->addWidget(linked_);
    auto *fit = new QPushButton(QIcon::fromTheme(QStringLiteral("zoom-fit-best")),
                                tr("Fit both images"), this);
    fit->setObjectName(QStringLiteral("fitComparedImages"));
    fit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    controls->addWidget(fit);
    controls->addStretch();
    layout->addLayout(controls);
    auto *vertical = new QSplitter(Qt::Vertical, this);
    auto *images = new QSplitter(Qt::Horizontal, vertical);
    for (int side = 0; side < 2; ++side)
    {
        auto &pane = panes_[side];
        auto *body = new QWidget(images);
        auto *paneLayout = new QVBoxLayout(body);
        auto *selection = new QHBoxLayout;
        pane.source = new QComboBox(body);
        pane.source->setObjectName(QStringLiteral("comparisonSource%1").arg(side));
        pane.source->setAccessibleName(side ? tr("Right image") : tr("Left image"));
        pane.source->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        pane.source->setMinimumContentsLength(12);
        pane.source->addItems(paths);
        selection->addWidget(pane.source, 1);
        auto *choose = new QPushButton(tr("Choose image…"), body);
        selection->addWidget(choose);
        paneLayout->addLayout(selection);
        pane.summary = new QLabel(body);
        pane.summary->setTextFormat(Qt::PlainText);
        pane.summary->setWordWrap(true);
        paneLayout->addWidget(pane.summary);
        pane.content = new QStackedWidget(body);
        pane.content->setMinimumSize(200, 180);
        paneLayout->addWidget(pane.content, 1);
        pane.monitor = new FileMonitor(body);
        if (!paths.isEmpty())
            pane.source->setCurrentIndex((qBound(0, current, int(paths.size()) - 1) + side) %
                                         paths.size());
        connect(pane.source, &QComboBox::currentIndexChanged, this,
                [this, side] { loadPane(side); });
        connect(choose, &QPushButton::clicked, this, [this, side] { chooseImage(side); });
        connect(pane.monitor, &FileMonitor::changed, this, [this, side] { loadPane(side, true); });
    }
    metadata_->setObjectName(QStringLiteral("comparisonMetadata"));
    metadata_->setAccessibleName(tr("Image metadata differences"));
    metadata_->setColumnCount(3);
    metadata_->setHorizontalHeaderLabels({tr("Property"), tr("Left image"), tr("Right image")});
    metadata_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    metadata_->setWordWrap(false);
    metadata_->verticalHeader()->hide();
    metadata_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    metadata_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    metadata_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    vertical->addWidget(metadata_);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 1);
    layout->addWidget(vertical, 1);
    layout->addWidget(differences_);
    auto *note = new QLabel(tr("Comparison shows still frames, up to 32 MP per image. "
                               "Embedded metadata depends on the installed image plugin; "
                               "differing values are highlighted."),
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
    connect(linked_, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    queueSync(0);
            });
    connect(fit, &QPushButton::clicked, this,
            [this]
            {
                for (auto &pane : panes_)
                    if (pane.image)
                        pane.image->fit();
            });
    loadPane(0);
    loadPane(1);
}

ImageComparison::~ImageComparison()
{
    cancel();
    // QWidget deletes child controls after the Pane members are destroyed.
    // Their teardown signals must not invoke handlers that access those members.
    for (auto *child : findChildren<QObject *>())
        QObject::disconnect(child, nullptr, this, nullptr);
}

void ImageComparison::cancel()
{
    syncTimer_.stop();
    for (auto &pane : panes_)
    {
        ++pane.generation;
        pane.stop.request_stop();
        pane.monitor->clear();
    }
}

void ImageComparison::closeEvent(QCloseEvent *event)
{
    cancel();
    QDialog::closeEvent(event);
}

void ImageComparison::chooseImage(int side)
{
    QStringList patterns;
    for (const auto &format : QImageReader::supportedImageFormats())
        patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    const auto path = QFileDialog::getOpenFileName(this, tr("Choose image to compare"), {},
                                                   tr("Images (%1)").arg(patterns.join(' ')));
    if (path.isEmpty())
        return;
    auto *source = panes_[side].source;
    int index = source->findText(path);
    if (index < 0)
    {
        source->addItem(path);
        index = source->count() - 1;
    }
    source->setCurrentIndex(index);
}

void ImageComparison::loadPane(int side, bool preserve)
{
    auto &pane = panes_[side];
    std::optional<ImageViewState> state;
    if (preserve && pane.image)
        state = pane.image->viewState();
    ++pane.generation;
    const auto request = pane.generation;
    pane.stop.request_stop();
    pane.stop = std::stop_source{};
    pane.monitor->clear();
    while (pane.content->count())
        delete pane.content->widget(0);
    pane.metadata.clear();
    compareMetadata();
    auto *message = new QLabel(tr("Loading image…"), pane.content);
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    message->setAlignment(Qt::AlignCenter);
    pane.content->addWidget(message);
    const auto url = imageUrl(pane.source->currentText());
    pane.summary->setText(url.toDisplayString());
    if (!url.isLocalFile() || url.toLocalFile().isEmpty())
    {
        message->setText(tr("Choose a local image to compare."));
        return;
    }
    pane.monitor->setPath(url.toLocalFile());
    auto *watcher = new QFutureWatcher<ComparisonImage>(this);
    connect(watcher, &QFutureWatcher<ComparisonImage>::finished, this,
            [this, side, watcher, request, state]
            {
                const auto result = watcher->result();
                watcher->deleteLater();
                auto &pane = panes_[side];
                if (request != pane.generation)
                    return;
                while (pane.content->count())
                    delete pane.content->widget(0);
                if (result.result.image.isNull())
                {
                    auto *error = new QLabel(result.result.error, pane.content);
                    error->setTextFormat(Qt::PlainText);
                    error->setWordWrap(true);
                    error->setAlignment(Qt::AlignCenter);
                    pane.content->addWidget(error);
                    return;
                }
                pane.image = new ImageView(result.result.image, pane.content);
                pane.image->setObjectName(QStringLiteral("comparisonImage%1").arg(side));
                pane.image->setAccessibleName(side ? tr("Right comparison image")
                                                   : tr("Left comparison image"));
                pane.content->addWidget(pane.image);
                pane.content->setCurrentWidget(pane.image);
                pane.metadata = result.metadata;
                pane.summary->setText(tr("%1 × %2 pixels")
                                          .arg(result.result.image.width())
                                          .arg(result.result.image.height()));
                connect(pane.image, &ImageView::viewChanged, this,
                        [this, side] { queueSync(side); });
                if (state)
                    QTimer::singleShot(0, pane.image, [image = pane.image, state]
                                       { image->restoreViewState(*state); });
                compareMetadata();
                queueSync(0);
            });
    watcher->setFuture(QtConcurrent::run(readComparison, url.toLocalFile(), pane.stop.get_token(),
                                         readPreferences().imageMP));
}

void ImageComparison::queueSync(int source)
{
    if (!linked_->isChecked() || syncing_)
        return;
    syncSource_ = source;
    syncTimer_.start(0);
}

void ImageComparison::syncViews(int source)
{
    if (!linked_->isChecked() || syncing_ || !panes_[0].image || !panes_[1].image)
        return;
    syncing_ = true;
    auto *from = panes_[source].image.data();
    auto *to = panes_[1 - source].image.data();
    const QSignalBlocker blocker(to);
    auto state = from->viewState();
    const auto a = from->scene()->itemsBoundingRect();
    const auto b = to->scene()->itemsBoundingRect();
    if (a.width() > 0 && a.height() > 0)
        state.center = QPointF(state.center.x() * b.width() / a.width(),
                               state.center.y() * b.height() / a.height());
    to->restoreViewState(state);
    syncing_ = false;
}

void ImageComparison::compareMetadata()
{
    QSet<QString> properties;
    for (const auto &pane : panes_)
        for (auto it = pane.metadata.cbegin(); it != pane.metadata.cend(); ++it)
            properties.insert(it.key());
    auto keys = properties.values();
    keys.sort();
    metadata_->setRowCount(keys.size());
    int differences = 0;
    for (int row = 0; row < keys.size(); ++row)
    {
        const auto &key = keys.at(row);
        const auto left = panes_[0].metadata.value(key);
        const auto right = panes_[1].metadata.value(key);
        const bool different = left != right;
        differences += different;
        const QStringList values{key, left, right};
        for (int column = 0; column < 3; ++column)
        {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setToolTip(values.at(column));
            if (different)
            {
                auto font = item->font();
                font.setBold(true);
                item->setFont(font);
                item->setForeground(palette().brush(QPalette::Link));
            }
            metadata_->setItem(row, column, item);
        }
    }
    differences_->setText(tr("%1 metadata fields differ.").arg(differences));
}
