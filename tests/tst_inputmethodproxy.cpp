/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "input/inputmethodproxy.h"

#include <QtTest>

using namespace Tuuli;

class tst_InputMethodProxy : public QObject
{
    Q_OBJECT
private slots:
    void hintsPerInputType()
    {
        QVERIFY(InputMethodProxy::hintsFor(InputType::Url) & Qt::ImhUrlCharactersOnly);
        QVERIFY(InputMethodProxy::hintsFor(InputType::Url) & Qt::ImhNoAutoUppercase);
        QVERIFY(InputMethodProxy::hintsFor(InputType::Email) & Qt::ImhEmailCharactersOnly);
        QVERIFY(InputMethodProxy::hintsFor(InputType::Number) & Qt::ImhFormattedNumbersOnly);
        QVERIFY(InputMethodProxy::hintsFor(InputType::Tel) & Qt::ImhDialableCharactersOnly);
        QVERIFY(InputMethodProxy::hintsFor(InputType::Password) & Qt::ImhHiddenText);
        QVERIFY(InputMethodProxy::hintsFor(InputType::Password) & Qt::ImhSensitiveData);
        QCOMPARE(InputMethodProxy::hintsFor(InputType::Text), Qt::ImhNone);
    }

    void enterKeyTypes()
    {
        QCOMPARE(InputMethodProxy::enterKeyTypeFor(InputType::Search, false), Qt::EnterKeySearch);
        QCOMPARE(InputMethodProxy::enterKeyTypeFor(InputType::Url, false), Qt::EnterKeyGo);
        QCOMPARE(InputMethodProxy::enterKeyTypeFor(InputType::Search, true), Qt::EnterKeyDefault);
    }

    void w3cKeyNames()
    {
        QCOMPARE(InputMethodProxy::w3cKeyName(Qt::Key_Return, QStringLiteral("\r")), QStringLiteral("Enter"));
        QCOMPARE(InputMethodProxy::w3cKeyName(Qt::Key_Backspace, QString()), QStringLiteral("Backspace"));
        QCOMPARE(InputMethodProxy::w3cKeyName(Qt::Key_Left, QString()), QStringLiteral("ArrowLeft"));
        QCOMPARE(InputMethodProxy::w3cKeyName(Qt::Key_A, QStringLiteral("a")), QStringLiteral("a"));
        QCOMPARE(InputMethodProxy::w3cKeyName(Qt::Key_F35, QString()), QStringLiteral("Unidentified"));
    }

    void showFromEngineActivatesWithState()
    {
        InputMethodProxy p;
        QSignalSpy active(&p, &InputMethodProxy::activeChanged);
        QSignalSpy type(&p, &InputMethodProxy::inputTypeChanged);
        p.showFromEngine(InputType::Email, QStringLiteral("me@"), false, QRectF(1, 2, 3, 4));
        QVERIFY(p.active());
        QCOMPARE(active.size(), 1);
        QCOMPARE(type.size(), 1);
        QCOMPARE(p.text(), QStringLiteral("me@"));
        QCOMPARE(p.cursorPosition(), 3);
        QCOMPARE(p.cursorRect(), QRectF(1, 2, 3, 4));
        QVERIFY(p.inputMethodHints() & Qt::ImhEmailCharactersOnly);
        QVERIFY(!p.passwordMode());
        p.showFromEngine(InputType::Password, QString(), false, QRectF());
        QVERIFY(p.passwordMode());
        p.hideFromEngine();
        QVERIFY(!p.active());
    }

    void editsBecomeEngineActions()
    {
        InputMethodProxy p;
        QSignalSpy keys(&p, &InputMethodProxy::keyRequested);
        QSignalSpy comps(&p, &InputMethodProxy::compositionRequested);
        p.showFromEngine(InputType::Text, QStringLiteral("hel"), false, QRectF());
        p.textEdited(QStringLiteral("hello"));
        QCOMPARE(comps.size(), 1);
        QCOMPARE(comps.first().at(1).toString(), QStringLiteral("lo"));
        QCOMPARE(p.text(), QStringLiteral("hello"));
        QCOMPARE(p.cursorPosition(), 5);

        p.textEdited(QStringLiteral("hell"));
        // Backspace: one down + one up.
        QCOMPARE(keys.size(), 2);
        QCOMPARE(keys.at(0).at(0).toBool(), true);
        QCOMPARE(keys.at(0).at(1).toString(), QStringLiteral("Backspace"));
        QCOMPARE(keys.at(1).at(0).toBool(), false);
        QCOMPARE(p.cursorPosition(), 4);
    }

    void engineSelectionUpdatesProxy()
    {
        InputMethodProxy p;
        p.showFromEngine(InputType::Text, QStringLiteral("abc"), false, QRectF());
        QSignalSpy sel(&p, &InputMethodProxy::selectionChanged);
        p.selectionFromEngine(QStringLiteral("abcd"), 1, 3);
        QCOMPARE(p.text(), QStringLiteral("abcd"));
        QCOMPARE(p.cursorPosition(), 1);
        QCOMPARE(p.anchorPosition(), 3);
        QCOMPARE(sel.size(), 1);
        p.selectionFromEngine(QStringLiteral("abcd"), 99, -1);
        QCOMPARE(p.cursorPosition(), 4);
        QCOMPARE(p.anchorPosition(), 4);
    }

    void editsWhileInactiveAreDropped()
    {
        InputMethodProxy p;
        QSignalSpy comps(&p, &InputMethodProxy::compositionRequested);
        p.textEdited(QStringLiteral("x"));
        QCOMPARE(comps.size(), 0);
    }

    void dismissAndSubmit()
    {
        InputMethodProxy p;
        QSignalSpy dismiss(&p, &InputMethodProxy::dismissRequested);
        QSignalSpy keys(&p, &InputMethodProxy::keyRequested);
        p.showFromEngine(InputType::Search, QString(), false, QRectF());
        p.submit();
        QCOMPARE(keys.size(), 2);
        QCOMPARE(keys.first().at(1).toString(), QStringLiteral("Enter"));
        p.dismiss();
        QCOMPARE(dismiss.size(), 1);
        QVERIFY(!p.active());
        p.dismiss();
        QCOMPARE(dismiss.size(), 1);
    }
};

QTEST_GUILESS_MAIN(tst_InputMethodProxy)
#include "tst_inputmethodproxy.moc"
