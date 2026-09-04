/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "inputmethodproxy.h"

namespace Tuuli {

InputMethodProxy::InputMethodProxy(QObject* parent)
    : QObject(parent)
{
}

Qt::InputMethodHints InputMethodProxy::hintsFor(InputType type)
{
    switch (type) {
    case InputType::Url:
        return Qt::ImhUrlCharactersOnly | Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText;
    case InputType::Email:
        return Qt::ImhEmailCharactersOnly | Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText;
    case InputType::Number:
        return Qt::ImhFormattedNumbersOnly;
    case InputType::Tel:
        return Qt::ImhDialableCharactersOnly;
    case InputType::Password:
        return Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText;
    case InputType::Search:
        return Qt::ImhNoAutoUppercase;
    case InputType::Date:
    case InputType::Time:
    case InputType::DateTime:
    case InputType::Month:
    case InputType::Week:
        return Qt::ImhPreferNumbers;
    case InputType::Color:
    case InputType::Text:
    case InputType::None:
        break;
    }
    return Qt::ImhNone;
}

Qt::EnterKeyType InputMethodProxy::enterKeyTypeFor(InputType type, bool multiline)
{
    if (multiline)
        return Qt::EnterKeyDefault;
    switch (type) {
    case InputType::Search: return Qt::EnterKeySearch;
    case InputType::Url: return Qt::EnterKeyGo;
    case InputType::Password: return Qt::EnterKeyDone;
    default: return Qt::EnterKeyDefault;
    }
}

QString InputMethodProxy::w3cKeyName(int qtKey, const QString& text)
{
    switch (qtKey) {
    case Qt::Key_Return:
    case Qt::Key_Enter: return QStringLiteral("Enter");
    case Qt::Key_Backspace: return QStringLiteral("Backspace");
    case Qt::Key_Delete: return QStringLiteral("Delete");
    case Qt::Key_Tab: return QStringLiteral("Tab");
    case Qt::Key_Backtab: return QStringLiteral("Tab");
    case Qt::Key_Escape: return QStringLiteral("Escape");
    case Qt::Key_Left: return QStringLiteral("ArrowLeft");
    case Qt::Key_Right: return QStringLiteral("ArrowRight");
    case Qt::Key_Up: return QStringLiteral("ArrowUp");
    case Qt::Key_Down: return QStringLiteral("ArrowDown");
    case Qt::Key_Home: return QStringLiteral("Home");
    case Qt::Key_End: return QStringLiteral("End");
    case Qt::Key_PageUp: return QStringLiteral("PageUp");
    case Qt::Key_PageDown: return QStringLiteral("PageDown");
    case Qt::Key_Space: return QStringLiteral(" ");
    case Qt::Key_Shift: return QStringLiteral("Shift");
    case Qt::Key_Control: return QStringLiteral("Control");
    case Qt::Key_Alt: return QStringLiteral("Alt");
    case Qt::Key_Meta: return QStringLiteral("Meta");
    default:
        break;
    }
    if (!text.isEmpty())
        return text;
    return QStringLiteral("Unidentified");
}

void InputMethodProxy::showFromEngine(InputType type, const QString& text, bool multiline,
                                      const QRectF& cssRect)
{
    const bool typeChanged = (type != m_type) || (multiline != m_multiline);
    m_type = type;
    m_multiline = multiline;
    if (typeChanged)
        emit inputTypeChanged();
    if (m_text != text) {
        m_text = text;
        emit textChanged();
    }
    if (m_cursor != m_text.size() || m_anchor != m_text.size()) {
        m_cursor = m_anchor = m_text.size();
        emit selectionChanged();
    }
    if (m_cursorRect != cssRect) {
        m_cursorRect = cssRect;
        emit cursorRectChanged();
    }
    if (!m_active) {
        m_active = true;
        emit activeChanged();
    }
}

void InputMethodProxy::hideFromEngine()
{
    if (!m_active)
        return;
    m_active = false;
    emit activeChanged();
}

void InputMethodProxy::selectionFromEngine(const QString& text, int cursor, int anchor)
{
    if (m_text != text) {
        m_text = text;
        emit textChanged();
    }
    cursor = qBound(0, cursor, m_text.size());
    anchor = qBound(0, anchor < 0 ? cursor : anchor, m_text.size());
    if (cursor != m_cursor || anchor != m_anchor) {
        m_cursor = cursor;
        m_anchor = anchor;
        emit selectionChanged();
    }
}

void InputMethodProxy::textEdited(const QString& newText)
{
    if (!m_active || newText == m_text)
        return;
    const TextEdit e = diffText(m_text, newText);
    const QVector<ImeAction> actions = planImeEdit(m_text, m_cursor, m_anchor, newText);
    // Optimistically mirror the edit; the engine's selection update corrects
    // us if it disagrees.
    m_text = newText;
    m_cursor = m_anchor = qBound(0, e.position + e.inserted.size(), m_text.size());
    emitActions(actions);
    emit textChanged();
    emit selectionChanged();
}

void InputMethodProxy::emitActions(const QVector<ImeAction>& actions)
{
    m_planned += actions;
    for (const ImeAction& a : actions) {
        if (a.kind == ImeAction::Key) {
            for (int i = 0; i < a.repeat; ++i) {
                emit keyRequested(true, a.key, 0);
                emit keyRequested(false, a.key, 0);
            }
        } else {
            emit compositionRequested(static_cast<int>(CompositionState::End), a.text);
        }
    }
}

void InputMethodProxy::sendKey(int qtKey, const QString& text, int modifiers)
{
    const QString key = w3cKeyName(qtKey, text);
    ImeAction a;
    a.kind = ImeAction::Key;
    a.key = key;
    m_planned.append(a);
    emit keyRequested(true, key, modifiers);
    emit keyRequested(false, key, modifiers);
}

void InputMethodProxy::dismiss()
{
    if (!m_active)
        return;
    emit dismissRequested();
    hideFromEngine();
}

void InputMethodProxy::submit()
{
    sendKey(Qt::Key_Return, QString());
}

QVector<ImeAction> InputMethodProxy::takePlannedActions()
{
    QVector<ImeAction> out;
    out.swap(m_planned);
    return out;
}

} // namespace Tuuli
