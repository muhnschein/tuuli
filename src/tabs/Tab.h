// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Modelled on sailfish-browser apps/storage/tab.h (Copyright (c) 2013 Jolla Ltd., MPL-2.0),
// reduced to the fields the platform WebView does not already keep per view.
#pragma once

#include <QString>

namespace Tuuli {

struct Tab
{
    int id = 0;
    QString url;
    QString title;
    QString favicon;
    bool isPrivate = false;

    bool isValid() const
    {
        return id > 0;
    }

    bool operator==(const Tab &other) const
    {
        return id == other.id && url == other.url && title == other.title &&
               favicon == other.favicon && isPrivate == other.isPrivate;
    }

    bool operator!=(const Tab &other) const
    {
        return !(*this == other);
    }
};

} // namespace Tuuli
