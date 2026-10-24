// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
class KJob;
class RemotePreview final : public QObject
{
    Q_OBJECT
  public:
    explicit RemotePreview(QObject *parent = nullptr, qint64 maxBytes = 128LL * 1024 * 1024,
                           int timeoutMs = 60000);
    ~RemotePreview() override;
    void start(const QUrl &url);
    void cancel();
  Q_SIGNALS:
    void ready(const QUrl &local);
    void failed(const QString &error);
    void progress(qint64 received, qint64 total);

  private:
    void fail(const QString &error);
    QTemporaryDir directory_;
    QFile file_;
    QPointer<KJob> job_;
    QTimer timer_;
    qint64 limit_, received_ = 0, total_ = -1;
    bool finished_ = false, started_ = false;
};
