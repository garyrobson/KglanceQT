// SPDX-License-Identifier: AGPL-3.0-only

#include "viewposition.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QMetaMethod>
#include <QScrollBar>

TextPosition textPosition(KTextEditor::View *view)
{
    return {view->cursorPosition(), view->firstDisplayedLine(),
            view->horizontalScrollBar()->value()};
}

void restoreTextPosition(KTextEditor::View *view, const TextPosition &position)
{
    const auto *document = view->document();
    const int lastLine = qMax(0, document->lines() - 1);
    const int line = qBound(0, position.cursor.line(), lastLine);
    view->setCursorPosition(
        KTextEditor::Cursor(line, qBound(0, position.cursor.column(), document->lineLength(line))));
    view->setScrollPosition(KTextEditor::Cursor(qBound(0, position.firstLine, lastLine), 0));
    view->setHorizontalScrollPosition(position.horizontal);
}

namespace
{
bool publicPageMethods(QObject *part)
{
    if (!part)
        return false;
    // Okular exposes these as public Q_SCRIPTABLE slots. Detect the contract
    // through Qt rather than depending on Okular's uninstalled Part header.
    for (const auto *signature : {"currentPage()", "pages()", "goToPage(uint)"})
    {
        const int index = part->metaObject()->indexOfMethod(signature);
        if (index < 0)
            return false;
        const auto method = part->metaObject()->method(index);
        if (method.access() != QMetaMethod::Public ||
            !(method.attributes() & QMetaMethod::Scriptable))
            return false;
    }
    return true;
}
} // namespace

std::optional<uint> documentPage(QObject *part)
{
    if (!publicPageMethods(part))
        return std::nullopt;
    uint page = 0;
    if (!QMetaObject::invokeMethod(part, "currentPage", Qt::DirectConnection,
                                   Q_RETURN_ARG(uint, page)) ||
        page == 0)
        return std::nullopt;
    return page;
}

bool restoreDocumentPage(QObject *part, uint page)
{
    if (!publicPageMethods(part))
        return false;
    uint count = 0;
    if (!QMetaObject::invokeMethod(part, "pages", Qt::DirectConnection,
                                   Q_RETURN_ARG(uint, count)) ||
        count == 0)
        return false;
    return QMetaObject::invokeMethod(part, "goToPage", Qt::DirectConnection,
                                     Q_ARG(uint, qBound(1U, page, count)));
}
