// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Host stand-in for libsailfishapp's public header so src/main.cpp compiles under
// -Werror and clang-tidy without the device SDK. Never installed.
#pragma once

#include <QString>
#include <QUrl>

class QGuiApplication;
class QQuickView;

namespace SailfishApp {

QGuiApplication *application(int &argc, char **argv);
QQuickView *createView();
QUrl pathTo(const QString &filename);
QUrl pathToMainQml();

} // namespace SailfishApp
