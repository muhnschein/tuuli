/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "enginefactory.h"
#include "mockengine.h"
#ifdef TUULI_ENGINE_SERVO
#include "servoengine.h"
#endif

namespace Tuuli {

Engine* createDefaultEngine(QObject* parent)
{
    const QByteArray forced = qgetenv("TUULI_ENGINE");
#ifdef TUULI_ENGINE_SERVO
    if (forced != "mock")
        return new ServoEngine(parent);
#endif
    return new MockEngine(parent);
}

} // namespace Tuuli
