// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "viewposition.h"

#include <QWidget>
#include <stop_token>

class MarkdownView final : public QWidget
{
    Q_OBJECT
  public:
    explicit MarkdownView(const QString &path, QWidget *parent = nullptr);
    ~MarkdownView() override;
    std::optional<MarkdownPosition> position() const;
    void restorePosition(const MarkdownPosition &position);

  private:
    bool ready_ = false;
    std::optional<MarkdownPosition> pending_;
    std::stop_source stop_;
};
