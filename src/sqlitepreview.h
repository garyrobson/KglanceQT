// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "tableresult.h"
#include <QWidget>
#include <stop_token>
struct SQLiteResult : TableResult
{
    QStringList tables;
};
SQLiteResult readSQLite(const QString &path, const QString &table = {}, std::stop_token stop = {},
                        int maxRows = 1000);
class SQLiteView final : public QWidget
{
    Q_OBJECT
  public:
    explicit SQLiteView(const QString &path, QWidget *parent = nullptr);
    ~SQLiteView() override;

  private:
    std::stop_source stop_;
};
