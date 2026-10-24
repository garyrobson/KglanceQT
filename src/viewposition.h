// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <KTextEditor/Cursor>
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

struct PreviewPosition
{
    std::optional<TextPosition> text;
    std::optional<MarkdownPosition> markdown;
    std::optional<MediaPosition> media;
    std::optional<uint> page;
};

TextPosition textPosition(KTextEditor::View *view);
void restoreTextPosition(KTextEditor::View *view, const TextPosition &position);
std::optional<uint> documentPage(QObject *part);
bool restoreDocumentPage(QObject *part, uint page);
