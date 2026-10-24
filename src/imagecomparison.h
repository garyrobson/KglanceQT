// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QDialog>
#include <QMap>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <array>
#include <stop_token>

class QComboBox;
class QCheckBox;
class QLabel;
class QStackedWidget;
class QTableWidget;
class ImageView;
class FileMonitor;

class ImageComparison final : public QDialog
{
    Q_OBJECT
  public:
    ImageComparison(const QStringList &paths, int current, QWidget *parent = nullptr);
    ~ImageComparison() override;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    struct Pane
    {
        QComboBox *source = nullptr;
        QLabel *summary = nullptr;
        QStackedWidget *content = nullptr;
        FileMonitor *monitor = nullptr;
        QPointer<ImageView> image;
        QMap<QString, QString> metadata;
        quint64 generation = 0;
        std::stop_source stop;
    };
    void loadPane(int side, bool preserve = false);
    void chooseImage(int side);
    void compareMetadata();
    void syncViews(int source);
    void queueSync(int source);
    void cancel();
    std::array<Pane, 2> panes_;
    QCheckBox *linked_;
    QTableWidget *metadata_;
    QLabel *differences_;
    bool syncing_ = false;
    QTimer syncTimer_{this};
    int syncSource_ = 0;
};
