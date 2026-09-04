/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_ENGINEFACTORY_H
#define TUULI_ENGINEFACTORY_H

#include "engine.h"

namespace Tuuli {

/* The engine this build was configured for (servo or mock).  The
 * TUULI_ENGINE=mock environment variable forces the mock in a servo
 * build, for UI iteration on a device without a working engine. */
Engine* createDefaultEngine(QObject* parent = nullptr);

} // namespace Tuuli

#endif
