/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_USERAGENT_H
#define TUULI_USERAGENT_H

#include <QString>

namespace Tuuli {

/* UA strings (spec 5.4, 7.2 desktop-mode toggle).  The mobile string keeps
 * Servo's own mobile convention so upstream compat work applies to us, and
 * appends a Tuuli token so sites and bug reports can tell us apart. */
class UserAgent
{
public:
    static QString mobile(const QString& servoVersion, const QString& tuuliVersion);
    static QString desktop(const QString& servoVersion, const QString& tuuliVersion);
    static QString firefoxCompatVersion();
};

} // namespace Tuuli

#endif
