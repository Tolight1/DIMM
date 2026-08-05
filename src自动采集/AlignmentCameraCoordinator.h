#pragma once

#include "CameraManager.h"

class AlignmentCameraCoordinator final {
public:
    static bool preparePreview(CameraManager* cameraManager,
                               double previewRateHz,
                               QString* reason);
    static void restoreAfterAlignment(CameraManager* cameraManager,
                                      int triggerMode);
};
