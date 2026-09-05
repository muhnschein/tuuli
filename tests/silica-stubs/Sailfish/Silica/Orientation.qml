import QtQuick 2.0

// Silica's orientation mask, as a QML enum (the host Qt supports them;
// the device's Qt 5.6 never sees the stubs).
QtObject {
    enum Mask { None = 0, Portrait = 1, Landscape = 2, PortraitInverted = 4, LandscapeInverted = 8, All = 15 }
}
