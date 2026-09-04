/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "textdiff.h"

namespace Tuuli {

TextEdit diffText(const QString& oldText, const QString& newText)
{
    TextEdit e;
    const int oldLen = oldText.size();
    const int newLen = newText.size();

    int prefix = 0;
    while (prefix < oldLen && prefix < newLen && oldText.at(prefix) == newText.at(prefix))
        ++prefix;
    // Never split a surrogate pair.
    if (prefix > 0 && prefix < oldLen && oldText.at(prefix - 1).isHighSurrogate())
        --prefix;

    int suffix = 0;
    while (suffix < oldLen - prefix && suffix < newLen - prefix
           && oldText.at(oldLen - 1 - suffix) == newText.at(newLen - 1 - suffix))
        ++suffix;
    if (suffix > 0 && oldLen - suffix < oldLen && oldText.at(oldLen - suffix).isLowSurrogate())
        --suffix;

    e.position = prefix;
    e.removedLength = oldLen - prefix - suffix;
    e.inserted = newText.mid(prefix, newLen - prefix - suffix);
    return e;
}

static void pushKey(QVector<ImeAction>& out, const QString& key, int repeat)
{
    if (repeat <= 0)
        return;
    ImeAction a;
    a.kind = ImeAction::Key;
    a.key = key;
    a.repeat = repeat;
    out.append(a);
}

QVector<ImeAction> planImeEdit(const QString& engineText, int engineCursor, int engineAnchor,
                               const QString& newText)
{
    QVector<ImeAction> out;
    const TextEdit e = diffText(engineText, newText);
    if (e.isNoop())
        return out;

    const int len = engineText.size();
    const int cursor = qBound(0, engineCursor, len);
    const int anchor = qBound(0, engineAnchor < 0 ? cursor : engineAnchor, len);
    const int selStart = qMin(cursor, anchor);
    const int selEnd = qMax(cursor, anchor);
    const bool hasSelection = selStart != selEnd;

    const bool replacesSelection = hasSelection && e.position == selStart
                                   && e.removedLength == selEnd - selStart;

    if (replacesSelection) {
        if (e.inserted.isEmpty()) {
            pushKey(out, QStringLiteral("Backspace"), 1);
        } else {
            ImeAction c;
            c.kind = ImeAction::Composition;
            c.text = e.inserted;
            out.append(c);
        }
        return out;
    }

    // Collapse any selection to its end (ArrowRight semantics), then walk the
    // caret to the end of the removed span.
    int caret = cursor;
    if (hasSelection) {
        pushKey(out, QStringLiteral("ArrowRight"), 1);
        caret = selEnd;
    }
    const int target = e.position + e.removedLength;
    if (target > caret)
        pushKey(out, QStringLiteral("ArrowRight"), target - caret);
    else if (target < caret)
        pushKey(out, QStringLiteral("ArrowLeft"), caret - target);

    pushKey(out, QStringLiteral("Backspace"), e.removedLength);

    if (!e.inserted.isEmpty()) {
        ImeAction c;
        c.kind = ImeAction::Composition;
        c.text = e.inserted;
        out.append(c);
    }
    return out;
}

} // namespace Tuuli
