/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "input/textdiff.h"

#include <QtTest>

using namespace Tuuli;

static ImeAction key(const QString& k, int repeat = 1)
{
    ImeAction a;
    a.kind = ImeAction::Key;
    a.key = k;
    a.repeat = repeat;
    return a;
}

static ImeAction commit(const QString& text)
{
    ImeAction a;
    a.kind = ImeAction::Composition;
    a.text = text;
    return a;
}

class tst_TextDiff : public QObject
{
    Q_OBJECT
private slots:
    void appendAtEnd()
    {
        const TextEdit e = diffText(QStringLiteral("hell"), QStringLiteral("hello"));
        QCOMPARE(e.position, 4);
        QCOMPARE(e.removedLength, 0);
        QCOMPARE(e.inserted, QStringLiteral("o"));
    }

    void deleteAtEnd()
    {
        const TextEdit e = diffText(QStringLiteral("hello"), QStringLiteral("hell"));
        QCOMPARE(e.position, 4);
        QCOMPARE(e.removedLength, 1);
        QVERIFY(e.inserted.isEmpty());
    }

    void replaceInMiddle()
    {
        const TextEdit e = diffText(QStringLiteral("abcdef"), QStringLiteral("abXYef"));
        QCOMPARE(e.position, 2);
        QCOMPARE(e.removedLength, 2);
        QCOMPARE(e.inserted, QStringLiteral("XY"));
    }

    void identicalIsNoop()
    {
        QVERIFY(diffText(QStringLiteral("same"), QStringLiteral("same")).isNoop());
    }

    void emptyToText()
    {
        const TextEdit e = diffText(QString(), QStringLiteral("abc"));
        QCOMPARE(e.position, 0);
        QCOMPARE(e.removedLength, 0);
        QCOMPARE(e.inserted, QStringLiteral("abc"));
    }

    void doesNotSplitSurrogatePairs()
    {
        const QString smile = QString::fromUtf8("\xF0\x9F\x98\x80"); // U+1F600
        const QString wink = QString::fromUtf8("\xF0\x9F\x98\x89");  // U+1F609
        const TextEdit e = diffText(QStringLiteral("a") + smile, QStringLiteral("a") + wink);
        QCOMPARE(e.position, 1);
        QCOMPARE(e.removedLength, 2);
        QCOMPARE(e.inserted, wink);
    }

    void planTypingAtCaret()
    {
        const QVector<ImeAction> plan = planImeEdit(QStringLiteral("hell"), 4, 4, QStringLiteral("hello"));
        QCOMPARE(plan.size(), 1);
        QVERIFY(plan.first() == commit(QStringLiteral("o")));
    }

    void planBackspaceAtCaret()
    {
        const QVector<ImeAction> plan = planImeEdit(QStringLiteral("hello"), 5, 5, QStringLiteral("hell"));
        QCOMPARE(plan.size(), 1);
        QVERIFY(plan.first() == key(QStringLiteral("Backspace"), 1));
    }

    void planEditBehindCaretMovesLeft()
    {
        // Caret at end, user edits position 2: move left 4, delete 1, insert.
        const QVector<ImeAction> plan = planImeEdit(QStringLiteral("abcdef"), 6, 6, QStringLiteral("abXdef"));
        QCOMPARE(plan.size(), 3);
        QVERIFY(plan.at(0) == key(QStringLiteral("ArrowLeft"), 3));
        QVERIFY(plan.at(1) == key(QStringLiteral("Backspace"), 1));
        QVERIFY(plan.at(2) == commit(QStringLiteral("X")));
    }

    void planEditAheadOfCaretMovesRight()
    {
        const QVector<ImeAction> plan = planImeEdit(QStringLiteral("abcdef"), 0, 0, QStringLiteral("abcdefg"));
        QCOMPARE(plan.size(), 2);
        QVERIFY(plan.at(0) == key(QStringLiteral("ArrowRight"), 6));
        QVERIFY(plan.at(1) == commit(QStringLiteral("g")));
    }

    void planReplacingSelectionIsJustAComposition()
    {
        // "abcdef" with "cd" selected, user types "X".
        const QVector<ImeAction> plan = planImeEdit(QStringLiteral("abcdef"), 4, 2, QStringLiteral("abXef"));
        QCOMPARE(plan.size(), 1);
        QVERIFY(plan.first() == commit(QStringLiteral("X")));
    }

    void planDeletingSelectionIsOneBackspace()
    {
        const QVector<ImeAction> plan = planImeEdit(QStringLiteral("abcdef"), 2, 4, QStringLiteral("abef"));
        QCOMPARE(plan.size(), 1);
        QVERIFY(plan.first() == key(QStringLiteral("Backspace"), 1));
    }

    void planNoopIsEmpty()
    {
        QVERIFY(planImeEdit(QStringLiteral("x"), 1, 1, QStringLiteral("x")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(tst_TextDiff)
#include "tst_textdiff.moc"
