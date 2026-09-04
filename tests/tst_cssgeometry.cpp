/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "input/cssgeometry.h"

#include <QtTest>

using namespace Tuuli;

class tst_CssGeometry : public QObject
{
    Q_OBJECT
private slots:
    void pointConversionRoundTrips()
    {
        const QPointF device(1080, 2260);
        const QPointF css = Css::deviceToCss(device, 2.0);
        QCOMPARE(css, QPointF(540, 1130));
        QCOMPARE(Css::cssToDevice(css, 2.0), device);
    }

    void fractionalDprIsPreserved()
    {
        const QPointF css = Css::deviceToCss(QPointF(1080, 0), 2.5);
        QCOMPARE(css.x(), 432.0);
    }

    void badDprFallsBackToOne()
    {
        QCOMPARE(Css::sanitizeDpr(0), 1.0);
        QCOMPARE(Css::sanitizeDpr(-2), 1.0);
        QCOMPARE(Css::deviceToCss(QPointF(10, 10), 0), QPointF(10, 10));
    }

    void rectConversionRounds()
    {
        const QRect r = Css::cssToDevice(QRectF(0.4, 0.6, 10.26, 20.74), 1.0);
        QCOMPARE(r, QRect(0, 1, 10, 21));
        QCOMPARE(Css::deviceToCss(QRect(0, 0, 1080, 2260), 2.0), QRectF(0, 0, 540, 1130));
    }

    void derivedDprUsesQtWhenAboveOne()
    {
        QCOMPARE(Css::deriveDevicePixelRatio(2.0, 394), 2.0);
        QCOMPARE(Css::deriveDevicePixelRatio(1.5, 394), 1.5);
    }

    void derivedDprFromPanelDensity_data()
    {
        QTest::addColumn<qreal>("dpi");
        QTest::addColumn<qreal>("expected");
        QTest::newRow("jolla-phone-2026 ~394ppi") << 394.0 << 2.5;
        QTest::newRow("xperia-10 ~457ppi") << 457.0 << 2.75;
        QTest::newRow("160") << 160.0 << 1.0;
        QTest::newRow("tiny") << 40.0 << 1.0;
        QTest::newRow("huge") << 1000.0 << 4.0;
        QTest::newRow("unknown") << 0.0 << 1.0;
    }

    void derivedDprFromPanelDensity()
    {
        QFETCH(qreal, dpi);
        QFETCH(qreal, expected);
        QCOMPARE(Css::deriveDevicePixelRatio(1.0, dpi), expected);
    }

    void viewportLayoutWithKeyboard()
    {
        // Spec 6.3: surface is not resized; only the visible rect shrinks.
        const Css::ViewportLayout l = Css::layoutViewport(QSize(1080, 2260), 800, 0, 2.0);
        QCOMPARE(l.visibleDevice, QRect(0, 0, 1080, 1460));
        QCOMPARE(l.visibleCss, QRectF(0, 0, 540, 730));
        QVERIFY(l.obscured);
    }

    void viewportLayoutClampsInsets()
    {
        const Css::ViewportLayout l = Css::layoutViewport(QSize(100, 100), 500, 500, 1.0);
        QCOMPARE(l.visibleDevice, QRect(0, 100, 100, 0));
        const Css::ViewportLayout none = Css::layoutViewport(QSize(100, 100), 0, 0, 1.0);
        QVERIFY(!none.obscured);
        QCOMPARE(none.visibleDevice, QRect(0, 0, 100, 100));
    }

    void scrollDeltaWhenVisibleIsZero()
    {
        QCOMPARE(Css::scrollDeltaToReveal(QRectF(10, 10, 50, 20), QRectF(0, 0, 540, 730), 8), QPointF(0, 0));
    }

    void scrollDeltaBelowKeyboard()
    {
        // Element at y=900 in a 730-high visible area: scroll down by 900+20+8-730.
        QCOMPARE(Css::scrollDeltaToReveal(QRectF(10, 900, 50, 20), QRectF(0, 0, 540, 730), 8), QPointF(0, 198));
    }

    void scrollDeltaAboveViewport()
    {
        QCOMPARE(Css::scrollDeltaToReveal(QRectF(10, -50, 50, 20), QRectF(0, 0, 540, 730), 8), QPointF(0, -58));
    }

    void scrollDeltaTallElementAlignsTop()
    {
        QCOMPARE(Css::scrollDeltaToReveal(QRectF(0, 100, 50, 2000), QRectF(0, 0, 540, 730), 0), QPointF(0, 100));
    }

    void scrollDeltaHorizontal()
    {
        QCOMPARE(Css::scrollDeltaToReveal(QRectF(600, 10, 50, 20), QRectF(0, 0, 540, 730), 0), QPointF(110, 0));
    }
};

QTEST_GUILESS_MAIN(tst_CssGeometry)
#include "tst_cssgeometry.moc"
