#pragma once

#include "PulseGeneratorManager.h"
#include "SettingsDialog.h"

#include <QDateTime>
#include <QString>
#include <QVector>

struct RoiAxisRange;

inline constexpr int kFixedRoiSize = 64;
inline constexpr int kSimulationFrameSize = 5120;
inline constexpr int kSimulationTargetFps = 200;
inline constexpr int kSimulationFrameIntervalMs = 1000 / kSimulationTargetFps;
inline constexpr int kSimulationPreviewIntervalMs = 30000;
inline constexpr int kAlignmentPreviewIntervalMs = 1000;
inline constexpr int kAlignmentCandidateDetectionRefreshMs = 3000;
inline constexpr int kMeasurementUiIntervalMs = 100;
inline constexpr int kRoiEdgeUpdateMarginPx = 8;
inline constexpr qint64 kLostCentroidRelocalizeTimeoutMs = 1500;
inline constexpr qint64 kLiveRelocalizationMaxDurationMs = 15000;
inline constexpr double kFullFrameLocalizationPulseHz = 2.0;
inline constexpr double kAlignmentDefaultPolarisPolarDistanceArcmin = 37.6;
inline constexpr const char* kHardwareTriggerLine = "Line0";
inline constexpr const char* kRoiUpdateGateLine = "Line2";
inline constexpr double kPi = 3.14159265358979323846;

double medianOfSamples(QVector<double> samples);
double deterministicUnitNoise(int frameIndex, int salt);
double decimalYearFromUtc(const QDateTime& utcDateTime);
qint64 safeRoiIncrement(qint64 increment);
qint64 alignRoiValue(qint64 value, const RoiAxisRange& range);
QString toggleButtonStyle(bool active);
QString uiStatusColor(UiStatusLevel level);
QString cameraStatusText(bool online);
UiStatusLevel cameraStatusLevel(bool online);
QString statusLabelStyle(const QString& color);
QString statusLabelStyle(UiStatusLevel level);
bool pulseConfigsMatch(const PulseGeneratorManager::Config& lhs,
                       const PulseGeneratorManager::Config& rhs);
