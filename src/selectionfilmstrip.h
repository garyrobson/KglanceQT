// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QListView>
#include <QPointer>
#include <QTimer>

class KJob;
class FilmstripModel;

class SelectionFilmstrip final : public QListView
{
    Q_OBJECT
  public:
    explicit SelectionFilmstrip(QWidget *parent = nullptr);
    ~SelectionFilmstrip() override;
    void setPaths(const QStringList &paths);
    void setCurrent(int row);
    void clear();
    void invalidateCurrent();

  Q_SIGNALS:
    void fileSelected(int row);

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    void cancel();
    void requestThumbnails();
    FilmstripModel *files_;
    QTimer debounce_;
    QPointer<KJob> job_;
    quint64 generation_ = 0;
};
