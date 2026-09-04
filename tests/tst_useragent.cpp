/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "prefs/useragent.h"

#include <QtTest>

using namespace Tuuli;

class tst_UserAgent : public QObject
{
    Q_OBJECT
private slots:
    void mobileStringIsMobileAndIdentifiable()
    {
        const QString ua = UserAgent::mobile(QStringLiteral("0.5.0"), QStringLiteral("0.1.0"));
        QVERIFY(ua.startsWith(QStringLiteral("Mozilla/5.0 (Android; Mobile;")));
        QVERIFY(ua.contains(QStringLiteral("Servo/0.5.0")));
        QVERIFY(ua.contains(QStringLiteral("Tuuli/0.1.0")));
        QVERIFY(ua.contains(QStringLiteral("Firefox/")));
    }

    void desktopStringHasNoMobileToken()
    {
        const QString ua = UserAgent::desktop(QStringLiteral("0.5.0"), QStringLiteral("0.1.0"));
        QVERIFY(!ua.contains(QStringLiteral("Mobile")));
        QVERIFY(ua.contains(QStringLiteral("Linux aarch64")));
        QVERIFY(ua.contains(QStringLiteral("Tuuli/0.1.0")));
    }
};

QTEST_GUILESS_MAIN(tst_UserAgent)
#include "tst_useragent.moc"
