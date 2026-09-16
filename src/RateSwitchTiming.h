#pragma once

#include <QtGlobal>

struct RateSwitchTiming {
    qint64 pauseMs = 0;
    qint64 hardwareApplyMs = 0;
    qint64 firstValidPairMs = 0;
    bool success = false;
};
