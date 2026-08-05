#pragma once

#include <QString>

struct HotPixelTemplateSettings {
    QString camera0Mask;
    QString camera0Excess;
    QString camera1Mask;
    QString camera1Excess;
    int width = 0;
    int height = 0;
};

bool loadHotPixelTemplateSettings(const QString& path, HotPixelTemplateSettings* settings);
