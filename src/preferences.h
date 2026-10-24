// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <KPageDialog>
#include <functional>

struct PreviewPreferences
{
    bool autoplayAudio = true;
    bool autoplayVideo = true;
    bool autoplayAnimation = true;
    bool markdownSource = false;
    bool information = false;
    bool filmstrip = false;
    int volume = 100;
    int textMiB = 16;
    int imageMP = 64;
    int remoteMiB = 128;
    int animationFrames = 256;
    int animationMiB = 64;
    int archiveEntries = 10000;
};

PreviewPreferences readPreferences();
bool savePreferences(const PreviewPreferences &preferences);

class PreferencesDialog final : public KPageDialog
{
    Q_OBJECT
  public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    void accept() override;

  Q_SIGNALS:
    void applied();

  private:
    std::function<bool()> apply_;
};
