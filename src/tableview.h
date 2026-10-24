// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QStringList>
#include <QWidget>
#include <stop_token>
struct TableResult
{
    QList<QStringList> rows;
    int columns = 0;
    QString notice;
    QString error;
};
TableResult readTable(const QString &path, QChar delimiter, std::stop_token stop = {},
                      qint64 maxBytes = 16 * 1024 * 1024, int maxRows = 10000, int maxColumns = 256,
                      int maxCells = 100000);
class TableView final : public QWidget
{
    Q_OBJECT
  public:
    explicit TableView(const QString &path, QChar delimiter, QWidget *parent = nullptr);
    ~TableView() override;

  private:
    std::stop_source stop_;
};
