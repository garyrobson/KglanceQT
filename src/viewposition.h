// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <KTextEditor/Cursor>
#include <QString>
#include <QtCore/qnamespace.h>
#include <optional>

class QObject;

namespace KTextEditor
{
class View;
}

struct TextPosition
{
    KTextEditor::Cursor cursor;
    int firstLine = 0;
    int horizontal = 0;
};

struct MarkdownPosition
{
    bool source = false;
    int vertical = 0;
    int horizontal = 0;
    TextPosition text;
};

struct MediaPosition
{
    qint64 milliseconds = 0;
    bool playing = false;
};

struct TablePosition
{
    QString query;
    int sortColumn = -1;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
    int vertical = 0, horizontal = 0;
    int currentRow = -1, currentColumn = -1;
};

struct PreviewPosition
{
    std::optional<TextPosition> text;
    std::optional<MarkdownPosition> markdown;
    std::optional<MediaPosition> media;
    std::optional<uint> page;
    std::optional<TablePosition> table;
};

TextPosition textPosition(KTextEditor::View *view);
void restoreTextPosition(KTextEditor::View *view, const TextPosition &position);
std::optional<uint> documentPage(QObject *part);
bool restoreDocumentPage(QObject *part, uint page);
