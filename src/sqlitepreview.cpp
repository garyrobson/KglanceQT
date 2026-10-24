// SPDX-License-Identifier: AGPL-3.0-only
#include "sqlitepreview.h"
#include "preferences.h"
#include "tableview.h"
#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QVBoxLayout>
#include <QtConcurrent>
SQLiteView::SQLiteView(const QString &path, QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *choice = new QComboBox(this);
    choice->setAccessibleName(tr("Database table"));
    layout->addWidget(choice);
    auto *status = new QLabel(tr("Reading database schema…"), this);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *watcher = new QFutureWatcher<SQLiteResult>(this);
    connect(watcher, &QFutureWatcher<SQLiteResult>::finished, this,
            [=, this]
            {
                const auto result = watcher->result();
                watcher->deleteLater();
                status->setText(result.error.isEmpty() ? result.notice : result.error);
                if (!result.error.isEmpty())
                    return;
                connect(choice, &QComboBox::currentTextChanged, this,
                        [=, this](const QString &table)
                        {
                            while (layout->count() > 2)
                            {
                                auto *item = layout->takeAt(2);
                                delete item->widget();
                                delete item;
                            }
                            const int rows = readPreferences().sqliteRows;
                            auto *view = new TableView(
                                [path, table, rows](std::stop_token stop) -> TableResult
                                { return readSQLite(path, table, stop, rows); }, this);
                            layout->addWidget(view);
                            setFocusProxy(view);
                        });
                choice->addItems(result.tables);
                if (result.tables.isEmpty())
                    status->setText(tr("No supported ordinary tables were found."));
            });
    watcher->setFuture(QtConcurrent::run(readSQLite, path, QString{}, stop_.get_token(), 1));
}
SQLiteView::~SQLiteView() { stop_.request_stop(); }
