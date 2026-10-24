// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QStringList>
struct TableResult
{
    QList<QStringList> rows;
    int columns = 0;
    QStringList headers;
    QString notice;
    QString error;
};
