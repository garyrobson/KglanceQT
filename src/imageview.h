// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QGraphicsView>

class ImageView final : public QGraphicsView
{
  public:
    explicit ImageView(const QImage &image, QWidget *parent = nullptr);
    void fit();

  protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    QGraphicsScene scene_;
    bool fitted_ = true;
};
