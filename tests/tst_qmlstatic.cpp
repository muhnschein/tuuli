// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Static checks over the QML sources that host Qt would accept silently:
//  * Sailfish.WebView is imported only where SCOPE.md §5 allows.
//  * Every `model.<role>` a delegate binds exists on that delegate's model.
//  * Every `<Singleton>.<member>` reference resolves to a property, method or signal.
#include "Core.h"

#include <QDirIterator>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

using Tuuli::BookmarkModel;
using Tuuli::EngineMessages;
using Tuuli::HistoryModel;
using Tuuli::Settings;
using Tuuli::Storage;
using Tuuli::TabModel;

namespace {

const char *const QmlDir = TUULI_SOURCE_DIR "/qml";

QStringList qmlFiles()
{
    QStringList files;
    QDirIterator it(QLatin1String(QmlDir), QStringList{QStringLiteral("*.qml")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(QDir(QLatin1String(QmlDir)).relativeFilePath(it.next()));
    }
    files.sort();
    return files;
}

QString readFile(const QString &relative)
{
    QFile file(QLatin1String(QmlDir) + QLatin1Char('/') + relative);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QSet<QString> metaMembers(const QMetaObject *meta)
{
    QSet<QString> members;
    for (int i = 0; i < meta->propertyCount(); ++i) {
        members.insert(QString::fromLatin1(meta->property(i).name()));
    }
    for (int i = 0; i < meta->methodCount(); ++i) {
        members.insert(QString::fromLatin1(meta->method(i).name()));
    }
    return members;
}

QSet<QString> roleSet(const QAbstractItemModel &model)
{
    QSet<QString> roles;
    for (const QByteArray &role : model.roleNames()) {
        roles.insert(QString::fromLatin1(role));
    }
    return roles;
}

} // namespace

class tst_qmlstatic : public QObject
{
    Q_OBJECT

private slots:
    void filesExist();
    void webViewImportOnlyInBrowserPage();
    void delegateRolesExist();
    void singletonMembersExist();
};

void tst_qmlstatic::filesExist()
{
    const QStringList files = qmlFiles();
    QVERIFY(files.contains(QStringLiteral("harbour-tuuli.qml")));
    QVERIFY(files.contains(QStringLiteral("pages/BrowserPage.qml")));
    QVERIFY(files.count() >= 10);
}

void tst_qmlstatic::webViewImportOnlyInBrowserPage()
{
    const QRegularExpression webView(QStringLiteral("^\\s*import\\s+Sailfish\\.WebView\\b"),
                                     QRegularExpression::MultilineOption);
    const QRegularExpression webEngine(QStringLiteral("^\\s*import\\s+Sailfish\\.WebEngine\\b"),
                                       QRegularExpression::MultilineOption);
    const QStringList engineAllowed{QStringLiteral("pages/BrowserPage.qml"),
                                    QStringLiteral("pages/SettingsPage.qml")};

    bool browserPageImportsWebView = false;
    for (const QString &file : qmlFiles()) {
        const QString source = readFile(file);
        if (webView.match(source).hasMatch()) {
            QVERIFY2(file == QStringLiteral("pages/BrowserPage.qml"),
                     qPrintable(QStringLiteral("Sailfish.WebView imported in %1").arg(file)));
            browserPageImportsWebView = true;
        }
        if (webEngine.match(source).hasMatch()) {
            QVERIFY2(engineAllowed.contains(file),
                     qPrintable(QStringLiteral("Sailfish.WebEngine imported in %1").arg(file)));
        }
    }
    QVERIFY(browserPageImportsWebView);
}

void tst_qmlstatic::delegateRolesExist()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    TabModel tabs(nullptr);
    HistoryModel history(storage);
    BookmarkModel bookmarks(storage);

    // Which model backs the `model.` references in each file.
    const QHash<QString, QSet<QString>> expected{
        {QStringLiteral("pages/BrowserPage.qml"), roleSet(tabs)},
        {QStringLiteral("pages/TabsPage.qml"), roleSet(tabs)},
        {QStringLiteral("components/TabDelegate.qml"), roleSet(tabs)},
        {QStringLiteral("pages/HistoryPage.qml"), roleSet(history)},
        {QStringLiteral("components/HistoryDelegate.qml"), roleSet(history)},
        {QStringLiteral("pages/BookmarksPage.qml"), roleSet(bookmarks)},
        {QStringLiteral("components/BookmarkDelegate.qml"), roleSet(bookmarks)},
    };

    const QRegularExpression reference(QStringLiteral("\\bmodel\\.([A-Za-z_][A-Za-z0-9_]*)"));
    int checked = 0;
    for (const QString &file : qmlFiles()) {
        const QString source = readFile(file);
        QRegularExpressionMatchIterator it = reference.globalMatch(source);
        while (it.hasNext()) {
            const QString role = it.next().captured(1);
            QVERIFY2(expected.contains(file),
                     qPrintable(QStringLiteral("%1 binds model.%2 but is not mapped to a model")
                                    .arg(file, role)));
            QVERIFY2(
                expected.value(file).contains(role),
                qPrintable(
                    QStringLiteral("%1 binds model.%2 which its model lacks").arg(file, role)));
            ++checked;
        }
    }
    QVERIFY(checked > 10);
}

void tst_qmlstatic::singletonMembersExist()
{
    const QHash<QString, QSet<QString>> members{
        {QStringLiteral("TabModel"), metaMembers(&TabModel::staticMetaObject)},
        {QStringLiteral("HistoryModel"), metaMembers(&HistoryModel::staticMetaObject)},
        {QStringLiteral("BookmarkModel"), metaMembers(&BookmarkModel::staticMetaObject)},
        {QStringLiteral("Settings"), metaMembers(&Settings::staticMetaObject)},
        {QStringLiteral("EngineMessages"), metaMembers(&EngineMessages::staticMetaObject)},
    };
    const QRegularExpression reference(
        QStringLiteral("\\b(TabModel|HistoryModel|BookmarkModel|Settings|EngineMessages)\\.([A-Za-"
                       "z_][A-Za-z0-9_]*)"));

    int checked = 0;
    for (const QString &file : qmlFiles()) {
        const QString source = readFile(file);
        QRegularExpressionMatchIterator it = reference.globalMatch(source);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString singleton = match.captured(1);
            const QString member = match.captured(2);
            QVERIFY2(members.value(singleton).contains(member),
                     qPrintable(QStringLiteral("%1 uses %2.%3 which does not exist")
                                    .arg(file, singleton, member)));
            ++checked;
        }
    }
    QVERIFY(checked > 20);
}

QTEST_GUILESS_MAIN(tst_qmlstatic)
#include "tst_qmlstatic.moc"
