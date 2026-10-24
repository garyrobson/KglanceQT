// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "tableresult.h"
#include "viewposition.h"
#include <QStringList>
#include <functional>
#include <optional>
class QTableView;
class QLineEdit;
#include <QWidget>
#include <stop_token>
TableResult readTable(const QString &path, QChar delimiter, std::stop_token stop = {},
                      qint64 maxBytes = 16 * 1024 * 1024, int maxRows = 10000, int maxColumns = 256,
                      int maxCells = 100000);
class TableView final : public QWidget
{
    Q_OBJECT
  public:
    explicit TableView(const QString &path, QChar delimiter, QWidget *parent = nullptr);
    explicit TableView(std::function<TableResult(std::stop_token)> loader,
                       QWidget *parent = nullptr);
    ~TableView() override;
    std::optional<TablePosition> position() const;
    void restorePosition(const TablePosition &position);

  private:
    std::stop_source stop_;
    QTableView *table_;
    QLineEdit *search_;
    bool ready_ = false;
    std::optional<TablePosition> pending_;
};
