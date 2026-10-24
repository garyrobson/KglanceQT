// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QWidget>
#include <stop_token>

class MarkdownView final : public QWidget
{
    Q_OBJECT
  public:
    explicit MarkdownView(const QString &path, QWidget *parent = nullptr);
    ~MarkdownView() override;

  private:
    std::stop_source stop_;
};
