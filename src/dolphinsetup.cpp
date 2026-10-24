// SPDX-License-Identifier: AGPL-3.0-only

#include "dolphinsetup.h"

#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QSaveFile>

ShortcutResult configureDolphin(const QString &path, const QString &shortcut)
{
    const auto key = QKeySequence::fromString(shortcut, QKeySequence::PortableText);
    if (key.isEmpty())
    {
        return {QStringLiteral("Invalid shortcut."), {}};
    }
    QFile input(path);
    const bool existed = input.exists();
    if (existed && !input.open(QIODevice::ReadOnly))
    {
        return {input.errorString(), {}};
    }
    const QByteArray original =
        existed
            ? input.readAll()
            : QByteArray("<!DOCTYPE gui SYSTEM 'kpartgui.dtd'><gui name='dolphin' version='1'/>");
    input.close();
    QDomDocument document;
    const auto parsed = document.setContent(original);
    if (!parsed)
    {
        return {QStringLiteral("Cannot parse Dolphin configuration: %1").arg(parsed.errorMessage),
                {}};
    }
    auto root = document.documentElement();
    if (root.tagName() != QLatin1String("gui") ||
        root.attribute(QStringLiteral("name")) != QLatin1String("dolphin"))
    {
        return {QStringLiteral("This is not a Dolphin configuration; refusing to overwrite it."),
                {}};
    }
    const QString current = QStringLiteral("servicemenu_kglanceqt.desktop::previewWithKGlanceQT");
    const QString legacy = QStringLiteral("servicemenu_kglance-rust.desktop::previewWithRust");
    const auto actions = root.elementsByTagName(QStringLiteral("Action"));
    QList<QDomNode> obsolete;
    for (int i = 0; i < actions.size(); ++i)
    {
        const auto action = actions.at(i).toElement();
        const auto name = action.attribute(QStringLiteral("name"));
        if (name == current || name == legacy)
        {
            obsolete.append(action);
        }
        else if (QKeySequence::fromString(action.attribute(QStringLiteral("shortcut")),
                                          QKeySequence::PortableText) == key)
        {
            return {QStringLiteral("Shortcut %1 is already assigned to %2.").arg(shortcut, name),
                    {}};
        }
    }
    for (const auto &action : obsolete)
    {
        auto parent = action.parentNode();
        parent.removeChild(action);
    }
    QDomElement group;
    const auto groups = root.elementsByTagName(QStringLiteral("ActionProperties"));
    for (int i = 0; i < groups.size(); ++i)
    {
        auto candidate = groups.at(i).toElement();
        if (candidate.attribute(QStringLiteral("scheme"), QStringLiteral("Default")) ==
            QLatin1String("Default"))
        {
            group = candidate;
            break;
        }
    }
    if (group.isNull())
    {
        group = document.createElement(QStringLiteral("ActionProperties"));
        group.setAttribute(QStringLiteral("scheme"), QStringLiteral("Default"));
        root.appendChild(group);
    }
    auto action = document.createElement(QStringLiteral("Action"));
    action.setAttribute(QStringLiteral("name"), current);
    action.setAttribute(QStringLiteral("shortcut"), key.toString(QKeySequence::PortableText));
    group.appendChild(action);
    const auto bytes = document.toByteArray(1);
    if (existed && original == bytes)
    {
        return {};
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        return {QStringLiteral("Cannot create Dolphin's configuration directory."), {}};
    }
    QString backup;
    if (existed)
    {
        backup = path + QStringLiteral(".kglanceqt-%1.backup")
                            .arg(QDateTime::currentDateTimeUtc().toString(
                                QStringLiteral("yyyyMMdd-HHmmss-zzz")));
        if (!QFile::copy(path, backup))
        {
            return {QStringLiteral("Cannot back up Dolphin's configuration; no changes made."), {}};
        }
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
    {
        return {output.errorString(), backup};
    }
    if (output.write(bytes) != bytes.size() || !output.commit())
    {
        return {output.errorString(), backup};
    }
    return {{}, backup};
}
