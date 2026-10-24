// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <QString>

struct ShortcutResult
{
    QString error;
    QString backup;
};

ShortcutResult configureDolphin(const QString &path, const QString &shortcut);
