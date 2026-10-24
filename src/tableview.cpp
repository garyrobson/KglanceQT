// SPDX-License-Identifier: AGPL-3.0-only
#include "tableview.h"
#include "preferences.h"
#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStringDecoder>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>
namespace
{
class TableModel final : public QAbstractTableModel
{
  public:
    TableModel(TableResult result, QObject *parent)
        : QAbstractTableModel(parent), result_(std::move(result))
    {
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : result_.rows.size();
    }
    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : result_.columns;
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        return result_.rows.value(index.row()).value(index.column());
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole &&
            section < result_.headers.size())
            return result_.headers[section];
        return QAbstractTableModel::headerData(section, orientation, role);
    }

  private:
    TableResult result_;
};
class TableProxy final : public QSortFilterProxyModel
{
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        const auto a = left.data().toString(), b = right.data().toString();
        bool numberA = false, numberB = false;
        const auto x = a.toDouble(&numberA), y = b.toDouble(&numberB);
        numberA = numberA && !std::isnan(x);
        numberB = numberB && !std::isnan(y);
        if (numberA && numberB)
            return x < y;
        // Keep a consistent ordering for columns containing mixed types.
        if (numberA != numberB)
            return numberA;
        return QString::localeAwareCompare(a, b) < 0;
    }
};
} // namespace
TableResult readTable(const QString &path, QChar delimiter, std::stop_token stop, qint64 maxBytes,
                      int maxRows, int maxColumns, int maxCells)
{
    TableResult result;
    QFile file(path);
    if (!QFileInfo(path).isFile())
    {
        result.error = QObject::tr("Table previews require a regular file.");
        return result;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = file.errorString();
        return result;
    }
    maxBytes = qBound<qint64>(1LL, maxBytes, 16LL * 1024 * 1024);
    maxRows = qBound(1, maxRows, 10000);
    maxColumns = qBound(1, maxColumns, 256);
    maxCells = qBound(1, maxCells, 100000);
    QByteArray bytes;
    while (!file.atEnd() && bytes.size() <= maxBytes)
    {
        if (stop.stop_requested())
        {
            result.error = QObject::tr("Table loading cancelled.");
            return result;
        }
        const auto chunk = file.read(qMin<qint64>(65536, maxBytes + 1 - bytes.size()));
        if (chunk.isEmpty() && file.error() != QFile::NoError)
        {
            result.error = file.errorString();
            return result;
        }
        bytes += chunk;
    }
    const bool clipped = bytes.size() > maxBytes;
    if (clipped)
        bytes.truncate(maxBytes);
    const auto encoding = QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8);
    QStringDecoder decoder(encoding);
    const QString text = decoder(bytes);
    if (decoder.hasError() && !clipped)
    {
        result.error = QObject::tr("The table contains invalid text encoding.");
        return result;
    }
    QStringList row;
    QString field;
    bool quoted = false, closed = false, atStart = true;
    int column = 0, cells = 0;
    bool columnLimit = false;
    auto appendField = [&]
    {
        if (column < maxColumns)
            row.append(field);
        else
            columnLimit = true;
        ++column;
        field.clear();
        atStart = true;
        closed = false;
    };
    auto appendRow = [&]
    {
        appendField();
        if (cells + row.size() > maxCells || result.rows.size() >= maxRows)
            return false;
        cells += row.size();
        result.columns = qMax(result.columns, int(row.size()));
        result.rows.append(std::move(row));
        row.clear();
        column = 0;
        return true;
    };
    bool rowLimit = false;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        if ((i & 4095) == 0 && stop.stop_requested())
        {
            result.error = QObject::tr("Table loading cancelled.");
            return result;
        }
        const auto ch = text.at(i);
        if (quoted)
        {
            if (ch == QLatin1Char('"'))
            {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"'))
                {
                    if (column < maxColumns)
                        field += ch;
                    ++i;
                }
                else
                {
                    quoted = false;
                    closed = true;
                }
            }
            else if (column < maxColumns)
                field += ch;
        }
        else if (ch == delimiter)
            appendField();
        else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))
        {
            if (ch == QLatin1Char('\r') && i + 1 < text.size() &&
                text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            if (!appendRow())
            {
                rowLimit = true;
                break;
            }
            if (result.rows.size() >= maxRows && i + 1 < text.size())
            {
                rowLimit = true;
                break;
            }
        }
        else if (ch == QLatin1Char('"') && atStart)
        {
            quoted = true;
            atStart = false;
        }
        else
        {
            if (closed || ch == QLatin1Char('"'))
            {
                result.error =
                    QObject::tr("Malformed CSV/TSV quoting near character %1.").arg(i + 1);
                return result;
            }
            atStart = false;
            if (column < maxColumns)
                field += ch;
        }
        if (field.size() > 65536)
        {
            result.notice = QObject::tr(
                "Field limit reached (64 Ki characters); showing complete preceding rows.");
            rowLimit = true;
            break;
        }
    }
    if (!rowLimit && !clipped)
    {
        if (quoted)
        {
            result.error = QObject::tr("An opening quote has no closing quote.");
            return result;
        }
        if (!atStart || closed || column > 0 || !field.isEmpty())
            if (!appendRow())
                rowLimit = true;
    }
    QStringList limits;
    if (clipped)
        limits << QObject::tr("input byte limit reached: incomplete final row omitted");
    if (rowLimit)
        limits << QObject::tr("row/cell limit reached (up to %1 rows / %2 cells)")
                      .arg(maxRows)
                      .arg(maxCells);
    if (columnLimit)
        limits << QObject::tr("only the first %1 columns are shown").arg(maxColumns);
    if (!result.notice.isEmpty())
        limits.prepend(result.notice);
    result.notice = limits.join(QStringLiteral(" · "));
    return result;
}
TableView::TableView(const QString &path, QChar delimiter, QWidget *parent)
    : TableView([path, delimiter,
                 maxBytes = qint64(readPreferences().textMiB) * 1024 * 1024](std::stop_token stop)
                { return readTable(path, delimiter, stop, maxBytes, 10000, 256, 100000); },
                parent)
{
}
TableView::TableView(std::function<TableResult(std::stop_token)> loader, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *search = new QLineEdit(this);
    search_ = search;
    search->setAccessibleName(tr("Search table cells"));
    search->setObjectName(QStringLiteral("tableSearch"));
    search->setPlaceholderText(tr("Search all cells…"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    auto *view = new QTableView(this);
    table_ = view;
    setFocusProxy(view);
    view->setAccessibleName(tr("Table preview"));
    view->setObjectName(QStringLiteral("tableCells"));
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setSelectionBehavior(QAbstractItemView::SelectItems);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setWordWrap(false);
    layout->addWidget(view);
    auto *status = new QLabel(tr("Reading table…"), this);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *watcher = new QFutureWatcher<TableResult>(this);
    connect(
        watcher, &QFutureWatcher<TableResult>::finished, this,
        [this, watcher, view, status, search]
        {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (!result.error.isEmpty())
            {
                status->setText(result.error);
                return;
            }
            auto *model = new TableModel(result, view);
            auto *proxy = new TableProxy(view);
            proxy->setSourceModel(model);
            proxy->setFilterKeyColumn(-1);
            proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
            proxy->setFilterFixedString(search->text());
            view->setModel(proxy);
            view->setSortingEnabled(true);
            view->sortByColumn(-1, Qt::AscendingOrder);
            auto *timer = new QTimer(this);
            timer->setSingleShot(true);
            timer->setInterval(180);
            connect(search, &QLineEdit::textChanged, timer, qOverload<>(&QTimer::start));
            connect(timer, &QTimer::timeout, proxy,
                    [proxy, search] { proxy->setFilterFixedString(search->text()); });
            for (int col = 0; col < result.columns; ++col)
            {
                int width = 60;
                for (int row = 0; row < qMin(100, int(result.rows.size())); ++row)
                    width = qMax(width, view->fontMetrics().horizontalAdvance(
                                            result.rows[row].value(col).left(100)) +
                                            24);
                view->setColumnWidth(col, qMin(width, 400));
            }
            status->setText(
                tr("%1 rows · %2 columns · All rows are data; click a column heading to sort. %3")
                    .arg(result.rows.size())
                    .arg(result.columns)
                    .arg(result.notice));
            ready_ = true;
            if (pending_)
                restorePosition(*pending_);
        });
    auto *copy = new QShortcut(QKeySequence::Copy, view);
    copy->setContext(Qt::WidgetShortcut);
    connect(
        copy, &QShortcut::activated, view,
        [view, status]
        {
            auto selected = view->selectionModel() ? view->selectionModel()->selectedIndexes()
                                                   : QModelIndexList{};
            if (selected.isEmpty())
                return;
            std::sort(selected.begin(), selected.end(),
                      [](const QModelIndex &a, const QModelIndex &b)
                      { return a.row() == b.row() ? a.column() < b.column() : a.row() < b.row(); });
            QString output;
            bool first = true;
            int lastRow = selected.first().row(), lastColumn = selected.first().column() - 1;
            for (const auto &index : selected)
            {
                if (index.row() != lastRow)
                {
                    output += QLatin1Char('\n');
                    lastColumn = index.column() - 1;
                }
                else if (!first)
                    output += QString(qMax(1, index.column() - lastColumn), QLatin1Char('\t'));
                QString value = index.data().toString();
                if (value.contains(QLatin1Char('\t')) || value.contains(QLatin1Char('\n')) ||
                    value.contains(QLatin1Char('"')))
                {
                    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
                    value = QLatin1Char('"') + value + QLatin1Char('"');
                }
                output += value;
                first = false;
                if (output.size() > 4 * 1024 * 1024)
                {
                    status->setText(
                        tr("Selection exceeds the 4 Mi character copy limit. Select fewer cells."));
                    return;
                }
                lastRow = index.row();
                lastColumn = index.column();
            }
            QApplication::clipboard()->setText(output);
        });
    watcher->setFuture(QtConcurrent::run(std::move(loader), stop_.get_token()));
}
TableView::~TableView() { stop_.request_stop(); }

std::optional<TablePosition> TableView::position() const
{
    if (!ready_)
        return pending_;
    return TablePosition{search_->text(),
                         table_->horizontalHeader()->sortIndicatorSection(),
                         table_->horizontalHeader()->sortIndicatorOrder(),
                         table_->verticalScrollBar()->value(),
                         table_->horizontalScrollBar()->value(),
                         table_->currentIndex().row(),
                         table_->currentIndex().column()};
}
void TableView::restorePosition(const TablePosition &position)
{
    pending_ = position;
    if (!ready_)
        return;
    const auto saved = position;
    pending_.reset();
    const QSignalBlocker blocker(search_);
    search_->setText(saved.query.left(512));
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(table_->model());
    proxy->setFilterFixedString(search_->text());
    table_->sortByColumn(qBound(-1, saved.sortColumn, table_->model()->columnCount() - 1),
                         saved.sortOrder);
    if (saved.currentRow >= 0 && saved.currentColumn >= 0)
        table_->setCurrentIndex(
            table_->model()->index(qMin(saved.currentRow, table_->model()->rowCount() - 1),
                                   qMin(saved.currentColumn, table_->model()->columnCount() - 1)));
    QTimer::singleShot(0, this,
                       [this, saved]
                       {
                           table_->verticalScrollBar()->setValue(saved.vertical);
                           table_->horizontalScrollBar()->setValue(saved.horizontal);
                       });
}
