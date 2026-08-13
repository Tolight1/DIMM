#include "AlignmentCameraCoordinator.h"

bool AlignmentCameraCoordinator::preparePreview(CameraManager* cameraManager,
                                                double previewRateHz,
                                                QString* reason)
{
    if (!cameraManager) {
        if (reason) {
            *reason = QStringLiteral("相机管理器未初始化。");
        }
        return false;
    }

    cameraManager->stopAll();
    for (int i = 0; i < 2; ++i) {
        if (!cameraManager->isOpen(i)) {
            continue;
        }
        if (!cameraManager->prepareFullFrame(i)) {
            if (reason) {
                *reason = QStringLiteral("相机%1切换全画幅失败。").arg(i + 1);
            }
            return false;
        }
        if (!cameraManager->setTriggerMode(i, TriggerMode::Continuous)) {
            if (reason) {
                *reason = QStringLiteral("相机%1切换连续取图失败。").arg(i + 1);
            }
            return false;
        }
        const double effectivePreviewRateHz = previewRateHz > 0.1 ? previewRateHz : 0.1;
        cameraManager->setFrameRate(i, effectivePreviewRateHz);
    }
    return true;
}

void AlignmentCameraCoordinator::restoreAfterAlignment(CameraManager* cameraManager,
                                                       int triggerMode)
{
    if (!cameraManager) {
        return;
    }

    cameraManager->stopAll();
    for (int i = 0; i < 2; ++i) {
        if (!cameraManager->isOpen(i)) {
            continue;
        }
        if (triggerMode == 0) {
            cameraManager->setTriggerMode(i, TriggerMode::Continuous);
        } else {
            cameraManager->configureExternalTrigger(i);
        }
    }
}
