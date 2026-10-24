// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QDialog>
#include <QUrl>
#include <stop_token>
class KRatingWidget;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class MetadataEditor final : public QDialog
{
    Q_OBJECT
  public:
    explicit MetadataEditor(const QUrl &url, QWidget *parent = nullptr);
    ~MetadataEditor() override;
  Q_SIGNALS:
    void saved(const QUrl &url);

  private:
    void save();
    QUrl url_;
    KRatingWidget *rating_;
    QPlainTextEdit *tags_;
    QPlainTextEdit *comment_;
    QLabel *notice_;
    QPushButton *apply_;
    quint64 device_ = 0, inode_ = 0;
    std::stop_source stop_;
};
