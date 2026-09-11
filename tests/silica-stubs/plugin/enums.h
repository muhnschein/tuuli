// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Enum holders mirroring Silica's values. QML property names cannot start with an
// upper-case letter, so `Orientation.Portrait` and friends must come from C++.
#pragma once

#include <QObject>

class Orientation : public QObject
{
    Q_OBJECT

public:
    enum Value
    {
        None = 0,
        Portrait = 1,
        Landscape = 2,
        PortraitInverted = 4,
        LandscapeInverted = 8,
        PortraitMask = 5,
        LandscapeMask = 10,
        All = 15
    };
    Q_ENUM(Value)
};

class PageStatus : public QObject
{
    Q_OBJECT

public:
    enum Value
    {
        Inactive = 0,
        Activating = 1,
        Active = 2,
        Deactivating = 3
    };
    Q_ENUM(Value)
};

class TruncationMode : public QObject
{
    Q_OBJECT

public:
    enum Value
    {
        None = 0,
        Elide = 1,
        Fade = 2
    };
    Q_ENUM(Value)
};
