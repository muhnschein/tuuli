/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "useragent.h"

namespace Tuuli {

QString UserAgent::firefoxCompatVersion()
{
    // Tracks Servo's own Firefox compat token; bump with each engine rebase.
    return QStringLiteral("128.0");
}

QString UserAgent::mobile(const QString& servoVersion, const QString& tuuliVersion)
{
    return QStringLiteral("Mozilla/5.0 (Android; Mobile; rv:%1) Servo/%2 Firefox/%1 Tuuli/%3")
        .arg(firefoxCompatVersion(), servoVersion, tuuliVersion);
}

QString UserAgent::desktop(const QString& servoVersion, const QString& tuuliVersion)
{
    return QStringLiteral("Mozilla/5.0 (X11; Linux aarch64; rv:%1) Servo/%2 Firefox/%1 Tuuli/%3")
        .arg(firefoxCompatVersion(), servoVersion, tuuliVersion);
}

} // namespace Tuuli
