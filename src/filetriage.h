// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QObject>
#include <QUrl>
#include <QVector>
#include <optional>

class FileTriage final : public QObject
{
    Q_OBJECT
  public:
    explicit FileTriage(QObject *parent = nullptr);
    bool busy() const;
    bool canUndo() const;
    void trash(const QUrl &url);
    void undo();

  Q_SIGNALS:
    void stateChanged();
    void trashed(const QUrl &url, quint64 command);
    void restored(const QUrl &url, quint64 command);
    void error(const QString &message);

  private:
    struct Record
    {
        QUrl url;
        quint64 command = 0;
    };
    bool busy_ = false;
    bool undoFailed_ = false;
    QVector<Record> records_;
    std::optional<Record> undoing_;
};
