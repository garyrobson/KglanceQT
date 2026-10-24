// SPDX-License-Identifier: AGPL-3.0-only

#include "preferences.h"
#include "dolphinsetup.h"
#include "imageanimation.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QFile>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

PreviewPreferences readPreferences()
{
    QSettings settings;
    PreviewPreferences p;
    p.autoplayAudio = settings.value(QStringLiteral("media/autoplayAudio"), true).toBool();
    p.autoplayVideo = settings.value(QStringLiteral("media/autoplayVideo"), true).toBool();
    p.autoplayAnimation = settings.value(QStringLiteral("images/autoplayAnimation"), true).toBool();
    p.volume = qBound(0, settings.value(QStringLiteral("media/volume"), 100).toInt(), 100);
    p.markdownSource = settings.value(QStringLiteral("markdown/source"), false).toBool();
    p.information = settings.value(QStringLiteral("window/information"), false).toBool();
    p.filmstrip = settings.value(QStringLiteral("window/filmstrip"), false).toBool();
    p.textMiB = qBound(1, settings.value(QStringLiteral("limits/textMiB"), 16).toInt(), 16);
    p.remoteMiB = qBound(1, settings.value(QStringLiteral("limits/remoteMiB"), 128).toInt(), 128);
    p.imageMP = qBound(1, settings.value(QStringLiteral("limits/imageMP"), 64).toInt(), 64);
    p.animationFrames = qBound(
        1, settings.value(QStringLiteral("limits/animationFrames"), MaxAnimationFrames).toInt(),
        MaxAnimationFrames);
    p.animationMiB =
        qBound(1, settings.value(QStringLiteral("limits/animationMiB"), 64).toInt(), 64);
    p.archiveEntries =
        qBound(1, settings.value(QStringLiteral("limits/archiveEntries"), 10000).toInt(), 10000);
    return p;
}

bool savePreferences(const PreviewPreferences &p)
{
    QSettings settings;
    settings.setValue(QStringLiteral("media/autoplayAudio"), p.autoplayAudio);
    settings.setValue(QStringLiteral("media/autoplayVideo"), p.autoplayVideo);
    settings.setValue(QStringLiteral("images/autoplayAnimation"), p.autoplayAnimation);
    settings.setValue(QStringLiteral("media/volume"), qBound(0, p.volume, 100));
    settings.setValue(QStringLiteral("markdown/source"), p.markdownSource);
    settings.setValue(QStringLiteral("window/information"), p.information);
    settings.setValue(QStringLiteral("window/filmstrip"), p.filmstrip);
    settings.setValue(QStringLiteral("limits/textMiB"), qBound(1, p.textMiB, 16));
    settings.setValue(QStringLiteral("limits/remoteMiB"), qBound(1, p.remoteMiB, 128));
    settings.setValue(QStringLiteral("limits/imageMP"), qBound(1, p.imageMP, 64));
    settings.setValue(QStringLiteral("limits/animationFrames"),
                      qBound(1, p.animationFrames, MaxAnimationFrames));
    settings.setValue(QStringLiteral("limits/animationMiB"), qBound(1, p.animationMiB, 64));
    settings.setValue(QStringLiteral("limits/archiveEntries"), qBound(1, p.archiveEntries, 10000));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

PreferencesDialog::PreferencesDialog(QWidget *parent) : KPageDialog(parent)
{
    setObjectName(QStringLiteral("preferencesDialog"));
    setWindowTitle(tr("Configure KGlanceQT"));
    setFaceType(KPageDialog::List);
    setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel |
                       QDialogButtonBox::RestoreDefaults);
    setModal(true);
    resize(640, 420);
    auto page = [this](const QString &title, const QString &icon)
    {
        auto *widget = new QWidget(this);
        auto *item = addPage(widget, title);
        item->setIcon(QIcon::fromTheme(icon));
        return widget;
    };
    auto *general = page(tr("Previews"), QStringLiteral("preview"));
    auto *form = new QFormLayout(general);
    auto *audio = new QCheckBox(tr("Play audio automatically"), general);
    audio->setObjectName(QStringLiteral("autoplayAudio"));
    auto *video = new QCheckBox(tr("Play videos automatically"), general);
    video->setObjectName(QStringLiteral("autoplayVideo"));
    form->addRow(audio);
    form->addRow(video);
    auto *animation = new QCheckBox(tr("Play animated images automatically"), general);
    animation->setObjectName(QStringLiteral("autoplayAnimation"));
    form->addRow(animation);
    auto *volume = new QSpinBox(general);
    volume->setObjectName(QStringLiteral("defaultVolume"));
    volume->setRange(0, 100);
    volume->setSuffix(tr(" %"));
    form->addRow(tr("Default volume:"), volume);
    auto *markdown = new QComboBox(general);
    markdown->setObjectName(QStringLiteral("defaultMarkdownView"));
    markdown->addItems({tr("Rendered preview"), tr("Source")});
    form->addRow(tr("Markdown opens as:"), markdown);
    auto *information = new QCheckBox(tr("Show the information panel"), general);
    information->setObjectName(QStringLiteral("defaultInformation"));
    form->addRow(information);
    auto *filmstrip = new QCheckBox(tr("Show the selection filmstrip"), general);
    filmstrip->setObjectName(QStringLiteral("defaultFilmstrip"));
    form->addRow(filmstrip);
    auto *note = new QLabel(tr("Preview defaults apply to the next file opened. Volume and the "
                               "information panel and selection filmstrip update immediately."),
                            general);
    note->setWordWrap(true);
    form->addRow(note);

    auto *limits = page(tr("Preview limits"), QStringLiteral("preferences-system-performance"));
    auto *limitForm = new QFormLayout(limits);
    auto spin = [limits, limitForm](const QString &name, const QString &label, int maximum,
                                    const QString &suffix)
    {
        auto *control = new QSpinBox(limits);
        control->setObjectName(name);
        control->setRange(1, maximum);
        control->setSuffix(suffix);
        limitForm->addRow(label, control);
        return control;
    };
    auto *text =
        spin(QStringLiteral("textLimit"), tr("Text, Markdown and tables:"), 16, tr(" MiB"));
    auto *remote = spin(QStringLiteral("remoteLimit"), tr("Remote downloads:"), 128, tr(" MiB"));
    auto *image = spin(QStringLiteral("imageLimit"), tr("Decoded images:"), 64, tr(" MP"));
    auto *animationFrames = spin(QStringLiteral("animationFrameLimit"), tr("Animation frames:"),
                                 MaxAnimationFrames, {});
    auto *animationMemory =
        spin(QStringLiteral("animationMemoryLimit"), tr("Animation cache:"), 64, tr(" MiB"));
    auto *archive = spin(QStringLiteral("archiveLimit"), tr("Archive entries:"), 10000, {});
    auto *limitNote = new QLabel(
        tr("Markdown rendering is limited to 256 KiB and 4,000 lines; larger documents open as "
           "source. Embedded images are limited to 16 images of up to 4 MP each. Animation "
           "decoding is limited to five seconds; animations exceeding a limit show a still frame."),
        limits);
    limitNote->setWordWrap(true);
    limitForm->addRow(limitNote);

    auto *integration = page(tr("Dolphin shortcut"), QStringLiteral("input-keyboard"));
    auto *shortcutForm = new QFormLayout(integration);
    const auto dolphinPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                             QStringLiteral("/kxmlgui5/dolphin/dolphinui.rc");
    QKeySequence current(Qt::Key_Space);
    QFile file(dolphinPath);
    if (file.size() <= 1024 * 1024 && file.open(QIODevice::ReadOnly))
    {
        QDomDocument document;
        if (document.setContent(file.readAll()))
        {
            const auto actions = document.elementsByTagName(QStringLiteral("Action"));
            for (int i = 0; i < actions.count(); ++i)
            {
                const auto action = actions.at(i).toElement();
                if (action.attribute(QStringLiteral("name")) ==
                    QLatin1String("servicemenu_kglanceqt.desktop::previewWithKGlanceQT"))
                    current = QKeySequence::fromString(action.attribute(QStringLiteral("shortcut")),
                                                       QKeySequence::PortableText);
            }
        }
    }
    auto *shortcut = new QKeySequenceEdit(current, integration);
    shortcut->setObjectName(QStringLiteral("dolphinShortcut"));
    shortcut->setMaximumSequenceLength(1);
    shortcutForm->addRow(tr("Preview selected files:"), shortcut);
    auto *applyShortcut = new QPushButton(tr("Apply shortcut to Dolphin"), integration);
    shortcutForm->addRow(applyShortcut);
    auto *shortcutNote = new QLabel(
        tr("Space is the default. This configures Dolphin's preview action; it does not register a "
           "global shortcut. Restart Dolphin when convenient after applying a change."),
        integration);
    shortcutNote->setWordWrap(true);
    shortcutForm->addRow(shortcutNote);
    auto *status = new QLabel(integration);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    shortcutForm->addRow(status);
    connect(applyShortcut, &QPushButton::clicked, this,
            [dolphinPath, shortcut, status]
            {
                const auto result = configureDolphin(
                    dolphinPath, shortcut->keySequence().toString(QKeySequence::PortableText));
                if (!result.error.isEmpty())
                    status->setText(result.error);
                else
                    status->setText(tr("Shortcut configured. Restart Dolphin when convenient.") +
                                    (result.backup.isEmpty()
                                         ? QString{}
                                         : tr("\nBackup: %1").arg(result.backup)));
            });
    auto populate = [=](const PreviewPreferences &p)
    {
        audio->setChecked(p.autoplayAudio);
        video->setChecked(p.autoplayVideo);
        animation->setChecked(p.autoplayAnimation);
        volume->setValue(p.volume);
        markdown->setCurrentIndex(p.markdownSource ? 1 : 0);
        information->setChecked(p.information);
        filmstrip->setChecked(p.filmstrip);
        text->setValue(p.textMiB);
        remote->setValue(p.remoteMiB);
        image->setValue(p.imageMP);
        animationFrames->setValue(p.animationFrames);
        animationMemory->setValue(p.animationMiB);
        archive->setValue(p.archiveEntries);
    };
    populate(readPreferences());
    apply_ = [=, this]
    {
        PreviewPreferences p;
        p.autoplayAudio = audio->isChecked();
        p.autoplayVideo = video->isChecked();
        p.autoplayAnimation = animation->isChecked();
        p.volume = volume->value();
        p.markdownSource = markdown->currentIndex() == 1;
        p.information = information->isChecked();
        p.filmstrip = filmstrip->isChecked();
        p.textMiB = text->value();
        p.remoteMiB = remote->value();
        p.imageMP = image->value();
        p.animationFrames = animationFrames->value();
        p.animationMiB = animationMemory->value();
        p.archiveEntries = archive->value();
        if (!savePreferences(p))
        {
            note->setText(tr(
                "Could not save preferences. Check that the configuration directory is writable."));
            return false;
        }
        Q_EMIT applied();
        return true;
    };
    connect(button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { apply_(); });
    button(QDialogButtonBox::Apply)->setEnabled(true);
    connect(button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [populate] { populate(PreviewPreferences{}); });
}

void PreferencesDialog::accept()
{
    if (apply_())
        KPageDialog::accept();
}
