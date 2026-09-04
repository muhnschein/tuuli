/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_TEXTDIFF_H
#define TUULI_TEXTDIFF_H

#include <QString>
#include <QVector>

namespace Tuuli {

/* Minimal single-span edit turning oldText into newText. */
struct TextEdit {
    int position = 0;
    int removedLength = 0;
    QString inserted;
    bool isNoop() const { return removedLength == 0 && inserted.isEmpty(); }
};

TextEdit diffText(const QString& oldText, const QString& newText);

/* What the IME proxy asks the engine to do to apply an edit (spec 6.3 step 3).
 * The engine has no "set selection" entry point, so caret movement is
 * expressed with arrow keys, deletion with Backspace, insertion as a
 * committed composition. */
struct ImeAction {
    enum Kind { Key, Composition };
    Kind kind = Key;
    QString key;        // W3C KeyboardEvent.key, e.g. "ArrowLeft"
    int repeat = 1;
    QString text;       // for Composition (committed)
    bool operator==(const ImeAction& o) const
    {
        return kind == o.kind && key == o.key && repeat == o.repeat && text == o.text;
    }
};

QVector<ImeAction> planImeEdit(const QString& engineText, int engineCursor, int engineAnchor,
                               const QString& newText);

} // namespace Tuuli

#endif
