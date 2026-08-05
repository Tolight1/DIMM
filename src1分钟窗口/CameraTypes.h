#pragma once

inline constexpr int kCameraCount = 2;

enum class CameraIndex : int {
    Camera1 = 0,
    Camera2 = 1
};

inline bool isValidCameraIndex(int cameraIndex)
{
    return cameraIndex >= 0 && cameraIndex < kCameraCount;
}
