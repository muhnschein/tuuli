/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_INPUTMETHODPROXY_H
#define TUULI_INPUTMETHODPROXY_H

/*
 * State object behind the hidden QML TextInput that Maliit attaches to
 * (spec 6.3).  The engine reports editable focus here; the QML proxy binds
 * its hints/text to it and reports committed edits back through
 * textEdited(), which are turned into engine key/composition events.
 */

#include "engine/engine.h"
#include "textdiff.h"

#include <QObject>
#include <QRectF>
#include <QString>

namespace Tuuli {

class InputMethodProxy : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(int cursorPosition READ cursorPosition NOTIFY selectionChanged)
    Q_PROPERTY(int anchorPosition READ anchorPosition NOTIFY selectionChanged)
    Q_PROPERTY(int inputMethodHints READ inputMethodHintsValue NOTIFY inputTypeChanged)
    Q_PROPERTY(int enterKeyType READ enterKeyTypeValue NOTIFY inputTypeChanged)
    Q_PROPERTY(bool passwordMode READ passwordMode NOTIFY inputTypeChanged)
    Q_PROPERTY(bool multiline READ multiline NOTIFY inputTypeChanged)
    Q_PROPERTY(QRectF cursorRect READ cursorRect NOTIFY cursorRectChanged)
    Q_PROPERTY(int inputType READ inputTypeValue NOTIFY inputTypeChanged)

public:
    explicit InputMethodProxy(QObject* parent = nullptr);

    bool active() const { return m_active; }
    QString text() const { return m_text; }
    int cursorPosition() const { return m_cursor; }
    int anchorPosition() const { return m_anchor; }
    Qt::InputMethodHints inputMethodHints() const { return hintsFor(m_type); }
    int inputMethodHintsValue() const { return static_cast<int>(inputMethodHints()); }
    Qt::EnterKeyType enterKeyType() const { return enterKeyTypeFor(m_type, m_multiline); }
    int enterKeyTypeValue() const { return static_cast<int>(enterKeyType()); }
    bool passwordMode() const { return m_type == InputType::Password; }
    bool multiline() const { return m_multiline; }
    QRectF cursorRect() const { return m_cursorRect; }
    InputType inputType() const { return m_type; }
    int inputTypeValue() const { return static_cast<int>(m_type); }

    static Qt::InputMethodHints hintsFor(InputType type);
    static Qt::EnterKeyType enterKeyTypeFor(InputType type, bool multiline);
    static QString w3cKeyName(int qtKey, const QString& text);

    /* Engine side (GUI thread). */
    void showFromEngine(InputType type, const QString& text, bool multiline, const QRectF& cssRect);
    void hideFromEngine();
    void selectionFromEngine(const QString& text, int cursor, int anchor);

    /* QML side. */
    Q_INVOKABLE void textEdited(const QString& newText);
    Q_INVOKABLE void sendKey(int qtKey, const QString& text, int modifiers = 0);
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void submit();

    /* Planned engine actions since the last call; tests use this. */
    QVector<ImeAction> takePlannedActions();

signals:
    void activeChanged();
    void textChanged();
    void selectionChanged();
    void inputTypeChanged();
    void cursorRectChanged();

    /* To the engine. */
    void keyRequested(bool down, const QString& key, int modifiers);
    void compositionRequested(int state, const QString& text);
    void dismissRequested();

private:
    void emitActions(const QVector<ImeAction>& actions);

    bool m_active = false;
    QString m_text;
    int m_cursor = 0;
    int m_anchor = 0;
    InputType m_type = InputType::None;
    bool m_multiline = false;
    QRectF m_cursorRect;
    QVector<ImeAction> m_planned;
};

} // namespace Tuuli

#endif
