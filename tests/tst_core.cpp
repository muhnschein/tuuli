// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "Core.h"

#include <QTemporaryDir>
#include <QtTest>

using Tuuli::BookmarkModel;
using Tuuli::Core;
using Tuuli::HistoryModel;

class tst_core : public QObject
{
    Q_OBJECT

private slots:
    void wiresTabsToHistory();
    void privateTabsLeaveNoHistory();
    void wiresFaviconsAndActiveUrlToBookmarks();
    void restoresState();
};

void tst_core::wiresTabsToHistory()
{
    QTemporaryDir dir;
    Core core(dir.path(), dir.path() + QStringLiteral("/tuuli.conf"));
    QVERIFY(core.storage().isOpen());
    QVERIFY(core.engineMessages() != nullptr);
    QVERIFY(core.settings() != nullptr);

    const int id = core.tabs()->newTab(QStringLiteral("https://a.example/"));
    QCOMPARE(core.history()->count(), 0);
    core.tabs()->updateUrl(id, QStringLiteral("https://a.example/"));
    QCOMPARE(core.history()->count(), 1);
    core.tabs()->updateTitle(id, QStringLiteral("Alpha"));
    QCOMPARE(core.history()->data(core.history()->index(0, 0), HistoryModel::TitleRole).toString(),
             QStringLiteral("Alpha"));
}

void tst_core::privateTabsLeaveNoHistory()
{
    QTemporaryDir dir;
    Core core(dir.path(), dir.path() + QStringLiteral("/tuuli.conf"));
    const int id = core.tabs()->newTab(QStringLiteral("https://secret.example/"), true);
    core.tabs()->updateUrl(id, QStringLiteral("https://secret.example/"));
    core.tabs()->updateUrl(id, QStringLiteral("https://secret.example/page"));
    core.tabs()->updateTitle(id, QStringLiteral("Secret"));
    QCOMPARE(core.history()->count(), 0);
}

void tst_core::wiresFaviconsAndActiveUrlToBookmarks()
{
    QTemporaryDir dir;
    Core core(dir.path(), dir.path() + QStringLiteral("/tuuli.conf"));
    QVERIFY(core.bookmarks()->activeUrl().isEmpty());

    const int id = core.tabs()->newTab(QStringLiteral("https://a.example/"));
    QCOMPARE(core.bookmarks()->activeUrl(), QStringLiteral("https://a.example/"));
    core.bookmarks()->add(QStringLiteral("https://a.example/"), QStringLiteral("A"));
    QVERIFY(core.bookmarks()->activeUrlBookmarked());

    core.tabs()->updateFavicon(id, QStringLiteral("https://a.example/icon.png"));
    QCOMPARE(core.bookmarks()
                 ->data(core.bookmarks()->index(0, 0), BookmarkModel::FaviconRole)
                 .toString(),
             QStringLiteral("https://a.example/icon.png"));

    core.tabs()->updateUrl(id, QStringLiteral("https://a.example/other"));
    QVERIFY(!core.bookmarks()->activeUrlBookmarked());
}

void tst_core::restoresState()
{
    QTemporaryDir dir;
    const QString config = dir.path() + QStringLiteral("/tuuli.conf");
    {
        Core core(dir.path(), config);
        core.tabs()->newTab(QStringLiteral("https://a.example/"));
        core.settings()->setDesktopMode(true);
    }
    Core core(dir.path(), config);
    QCOMPARE(core.tabs()->count(), 1);
    QCOMPARE(core.bookmarks()->activeUrl(), QStringLiteral("https://a.example/"));
    QVERIFY(core.settings()->desktopMode());
}

QTEST_GUILESS_MAIN(tst_core)
#include "tst_core.moc"
