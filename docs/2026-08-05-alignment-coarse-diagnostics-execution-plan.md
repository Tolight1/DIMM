# 粗对准单星实验诊断与速度显示改进执行计划
 

> **执行对象：** 能力或性能较弱的代码 Agent  
> **重要要求：** 本任务只修改源码并执行静态 Python 检查，**禁止运行 CMake、MSBuild、Visual Studio Build、Qt Build 或生成可执行文件**。最终编译和实验由用户自己完成。

---

## 1. 任务目标

当前粗对准功能存在以下可观测性问题：

1. 粗对准要求至少两条可用星点轨迹才能计算北天极圆心。
2. 实验室只有一个星点时，即使内部已经建立轨迹并拟合速度，界面仍可能显示：
   - `轨迹 0`
   - `速度 0.000 px/s`
3. 当前界面显示的“轨迹”实际是 `usableTrackCount`，即通过全部筛选并可参与北天极求解的轨迹数量，并不是正在跟踪的活动轨迹数量。
4. 当前界面显示的速度是可用轨迹的中位速度。只要轨迹因为净位移、持续时间、RMS 或速度阈值被过滤，界面速度就会保持 0。
5. 当前状态统一显示“采样中，请等待”，无法区分：
   - 没有检测到星点；
   - 已检测到星点，但轨迹还未积累；
   - 已建立轨迹，但持续时间不足；
   - 随机抖动导致净位移不足；
   - 拟合速度过低；
   - 拟合 RMS 过大；
   - 已得到一条可用轨迹，但不足以求北天极；
   - 已得到两条轨迹，但方向接近平行。

本任务需要在**不改变粗对准基本求解数学、不降低生产环境求解门限、不新增线程、不改变相机采集流程**的前提下，增加详细诊断能力。

完成后，界面应能明确显示：

```text
候选 1 | 活动轨迹 1 | 已拟合 1 | 可用轨迹 0/2
拟合速度 0.018 px/s | 求解速度 0.000 px/s | RMS 0.0 px
最长轨迹: 61点 / 60.0s / 净位移0.7px / 速度0.018px/s / RMS1.2px
未通过: 净位移0.7px，小于要求2.0px
```

当单个星点形成稳定线性漂移并通过全部轨迹门限后，应显示：

```text
候选 1 | 活动轨迹 1 | 已拟合 1 | 可用轨迹 1/2
拟合速度 0.215 px/s | 求解速度 0.215 px/s
粗对准: 已获得1/2条可用轨迹，还需至少1条方向不同的轨迹定位北天极
```

此时仍然不能输出二维北天极圆心，这是正确行为。

---

## 2. 非目标

本任务**不做**以下修改：

- 不允许将 `minTracksForCenter` 从 2 改为 1。
- 不允许使用单条轨迹伪造北天极二维坐标。
- 不允许删除或放宽现有生产环境轨迹筛选条件。
- 不允许把随机抖动当作有效恒星漂移。
- 不允许改变 `AlignmentCoarseController` 的线程模型。
- 不允许改变 `FullFrameStarDetector` 的阈值算法。
- 不允许调用 `PolarisSolver`。
- 不允许修改自动北极星识别流程。
- 不允许增加新的 CMake 源文件。
- 不允许运行构建。
- 不允许为了“测试通过”而删除现有逻辑。
- 不允许整文件重写带有中文文本的 C++ 文件。

---

## 3. 必须遵守的执行规则

- [ ] 使用小范围补丁修改，优先使用 `apply_patch`。
- [ ] 不得使用 PowerShell `Set-Content` 重写整个 C++ 文件。
- [ ] 保持原文件 UTF-8 中文内容不损坏。
- [ ] 不改变现有类名、信号槽、线程生命周期。
- [ ] 不修改 `AlignmentCoarseController.cpp/.h`。
- [ ] 不修改 `CMakeLists.txt`。
- [ ] 不运行 CMake。
- [ ] 不运行 MSBuild。
- [ ] 不运行 Visual Studio 或 Qt 的 Build。
- [ ] 允许运行 Python 静态测试。
- [ ] 修改完成后执行替换字符检查。
- [ ] 未经用户要求，不提交、不推送、不创建 PR。

---

## 4. 修改文件清单

需要修改：

```text
src自动采集/AlignmentCoarseEstimator.h
src自动采集/AlignmentCoarseEstimator.cpp
src自动采集/CanvasWidgets.h
src自动采集/CanvasWidgets.cpp
src自动采集/DIMM.Alignment.cpp
```

需要新增静态测试：

```text
src自动采集/tests/test_alignment_coarse_diagnostics_static.py
```

如果 `src自动采集/tests` 目录不存在，则创建该目录。

不需要修改：

```text
src自动采集/AlignmentCoarseController.h
src自动采集/AlignmentCoarseController.cpp
src自动采集/DIMM.h
src自动采集/DIMM.cpp
src自动采集/DIMM.Ui.cpp
src自动采集/CMakeLists.txt
```

---

# Task 0：确认当前代码基线

## 0.1 检查必要符号

在修改前，确认以下符号存在：

```text
AlignmentCoarseEstimator.h:
- struct CoarseAlignmentConfig
- struct CoarseAlignmentTrackOverlay
- struct CoarseAlignmentEstimate
- struct AlignmentCoarseTracker::Track

AlignmentCoarseEstimator.cpp:
- fitTrackVelocity
- solveNorthCelestialPoleCenter
- AlignmentCoarseTracker::addFrame
- usableTracks
- medianSpeedPxSec

CanvasWidgets.h:
- struct CoarseDriftTrackOverlay
- struct CoarseDriftOverlay

CanvasWidgets.cpp:
- FullFrameCanvas::drawCoarseDriftOverlay

DIMM.Alignment.cpp:
- DIMM::buildCoarseAlignmentConfig
- DIMM::onCoarseAlignmentEstimateReady
- DIMM::updateCoarseAlignmentOverlay
```

如果任意符号不存在，停止继续修改，并报告实际文件结构与本计划不一致。不要猜测新的插入位置。

## 0.2 记录原有门限

确认原有默认值仍为：

```cpp
int minTrackPoints = 5;
double minTrackDurationSec = 15.0;
double minTrackDisplacementPx = 2.0;
double maxTrackFitRmsPx = 3.5;
int minTracksForCenter = 2;
```

本任务不得降低这些值。

---

# Task 1：扩展粗对准数据结构

## 文件

```text
src自动采集/AlignmentCoarseEstimator.h
```

## 1.1 将最小速度阈值从魔法数字变成配置字段

在 `CoarseAlignmentConfig` 中找到：

```cpp
double maxTrackFitRmsPx = 3.5;
double maxCenterResidualRmsPx = 80.0;
```

修改为：

```cpp
double maxTrackFitRmsPx = 3.5;
double minTrackSpeedPxSec = 0.005;
double maxCenterResidualRmsPx = 80.0;
```

目的：

- 让诊断文本能引用真实门限；
- 避免 `AlignmentCoarseEstimator.cpp` 中继续硬编码 `0.005`；
- 不改变默认行为。

---

## 1.2 扩展 `CoarseAlignmentTrackOverlay`

将当前结构：

```cpp
struct CoarseAlignmentTrackOverlay {
    int id = 0;
    QPointF startPx;
    QPointF endPx;
    QPointF velocityPxSec;
    double speedPxSec = 0.0;
    double durationSec = 0.0;
    double fitRmsPx = 0.0;
    bool usedForSolve = false;
};
```

替换为：

```cpp
struct CoarseAlignmentTrackOverlay {
    int id = 0;
    int pointCount = 0;
    QPointF startPx;
    QPointF endPx;
    QPointF velocityPxSec;
    double speedPxSec = 0.0;
    double durationSec = 0.0;
    double displacementPx = 0.0;
    double fitRmsPx = 0.0;
    bool velocityFitValid = false;
    bool usedForSolve = false;
    QString rejectionReason;
};
```

说明：

- `pointCount`：当前轨迹积累的采样点数量。
- `displacementPx`：首尾净位移，不是累计路程。
- `velocityFitValid`：是否成功完成时间—位置线性回归。
- `rejectionReason`：未成为可用轨迹的主要原因。
- 这些字段主要用于诊断和后续显示，不改变求解。

---

## 1.3 扩展 `CoarseAlignmentEstimate`

在：

```cpp
double medianSpeedPxSec = 0.0;
double medianPolarDistanceDegFromSpeed = 0.0;
```

之间加入：

```cpp
double medianFittedSpeedPxSec = 0.0;
```

结果应为：

```cpp
double medianSpeedPxSec = 0.0;
double medianFittedSpeedPxSec = 0.0;
double medianPolarDistanceDegFromSpeed = 0.0;
```

在：

```cpp
int activeTrackCount = 0;
int usableTrackCount = 0;
```

之间加入：

```cpp
int fittedTrackCount = 0;
```

并在 `usableTrackCount` 后加入：

```cpp
int requiredTrackCount = 0;
```

结果应为：

```cpp
int activeTrackCount = 0;
int fittedTrackCount = 0;
int usableTrackCount = 0;
int requiredTrackCount = 0;
```

在：

```cpp
QString statusText;
QVector<CoarseAlignmentTrackOverlay> tracks;
```

之间加入：

```cpp
QString diagnosticText;
```

结果应为：

```cpp
QString statusText;
QString diagnosticText;
QVector<CoarseAlignmentTrackOverlay> tracks;
```

字段语义：

- `medianFittedSpeedPxSec`：所有成功拟合速度的活动轨迹中位速度，不要求轨迹通过求解筛选。
- `medianSpeedPxSec`：保持原语义，只统计可用于求解的轨迹。
- `fittedTrackCount`：成功完成速度拟合的活动轨迹数量。
- `requiredTrackCount`：当前求解北天极需要的最小轨迹数量。
- `diagnosticText`：最长或最有代表性的轨迹详细状态。

---

## 1.4 扩展内部 `Track`

将当前：

```cpp
double fitRmsPx = 0.0;
qint64 lastTimestampMs = 0;
bool usedForSolve = false;
```

修改为：

```cpp
double fitRmsPx = 0.0;
qint64 lastTimestampMs = 0;
bool velocityFitValid = false;
bool usedForSolve = false;
QString rejectionReason;
```

---

## 1.5 本任务验收点

确认头文件中存在：

```text
minTrackSpeedPxSec
pointCount
displacementPx
velocityFitValid
rejectionReason
medianFittedSpeedPxSec
fittedTrackCount
requiredTrackCount
diagnosticText
```

---

# Task 2：改进速度拟合状态和轨迹拒绝诊断

## 文件

```text
src自动采集/AlignmentCoarseEstimator.cpp
```

---

## 2.1 修改 `fitTrackVelocity`

当前函数只返回 `bool`，但没有把拟合是否成功保存在轨迹中。

将整个 `fitTrackVelocity` 函数替换为以下版本：

```cpp
bool fitTrackVelocity(AlignmentCoarseTracker::Track* track)
{
    if (!track) {
        return false;
    }

    track->velocityFitValid = false;
    track->velocityPxSec = QPointF();
    track->speedPxSec = 0.0;
    track->durationSec = 0.0;
    track->displacementPx = 0.0;
    track->fitRmsPx = 0.0;

    if (track->points.size() < 2) {
        return false;
    }

    track->durationSec =
        static_cast<double>(track->points.last().timestampMs -
                            track->points.first().timestampMs) /
        1000.0;
    track->displacementPx =
        pointDistance(track->points.first().positionPx,
                      track->points.last().positionPx);

    const qint64 t0Ms = track->points.first().timestampMs;
    double meanT = 0.0;
    double meanX = 0.0;
    double meanY = 0.0;
    for (const auto& point : track->points) {
        const double t =
            static_cast<double>(point.timestampMs - t0Ms) / 1000.0;
        meanT += t;
        meanX += point.positionPx.x();
        meanY += point.positionPx.y();
    }

    const double n = static_cast<double>(track->points.size());
    meanT /= n;
    meanX /= n;
    meanY /= n;

    double denominator = 0.0;
    double vxNumerator = 0.0;
    double vyNumerator = 0.0;
    for (const auto& point : track->points) {
        const double t =
            static_cast<double>(point.timestampMs - t0Ms) / 1000.0;
        const double dt = t - meanT;
        denominator += dt * dt;
        vxNumerator += dt * (point.positionPx.x() - meanX);
        vyNumerator += dt * (point.positionPx.y() - meanY);
    }

    if (denominator <= 1e-9) {
        return false;
    }

    const double vx = vxNumerator / denominator;
    const double vy = vyNumerator / denominator;
    track->velocityPxSec = QPointF(vx, vy);
    track->speedPxSec = std::sqrt(vx * vx + vy * vy);

    double residualSum = 0.0;
    for (const auto& point : track->points) {
        const double t =
            static_cast<double>(point.timestampMs - t0Ms) / 1000.0;
        const double predictedX = meanX + vx * (t - meanT);
        const double predictedY = meanY + vy * (t - meanT);
        const double dx = point.positionPx.x() - predictedX;
        const double dy = point.positionPx.y() - predictedY;
        residualSum += dx * dx + dy * dy;
    }

    track->fitRmsPx = std::sqrt(residualSum / n);
    track->velocityFitValid = true;
    return true;
}
```

重要行为：

- 两点以上才拟合。
- 即使拟合失败，也保留持续时间和净位移。
- 每次重新拟合前清除旧结果，避免旧速度残留。
- 不改变原来的线性最小二乘方法。

---

## 2.2 新增轨迹拒绝原因函数

在匿名命名空间中、`fitTrackVelocity` 后、`solveNorthCelestialPoleCenter` 前新增：

```cpp
QString trackRejectionReason(
    const AlignmentCoarseTracker::Track& track,
    const CoarseAlignmentConfig& config)
{
    if (track.points.size() < config.minTrackPoints) {
        return QStringLiteral("采样点%1个，少于要求%2个")
            .arg(track.points.size())
            .arg(config.minTrackPoints);
    }

    if (!track.velocityFitValid) {
        return QStringLiteral("速度拟合失败，时间戳可能重复或采样时间跨度不足");
    }

    if (track.durationSec < config.minTrackDurationSec) {
        return QStringLiteral("持续时间%1s，少于要求%2s")
            .arg(track.durationSec, 0, 'f', 1)
            .arg(config.minTrackDurationSec, 0, 'f', 1);
    }

    if (track.displacementPx < config.minTrackDisplacementPx) {
        return QStringLiteral("净位移%1px，小于要求%2px")
            .arg(track.displacementPx, 0, 'f', 2)
            .arg(config.minTrackDisplacementPx, 0, 'f', 2);
    }

    if (track.fitRmsPx > config.maxTrackFitRmsPx) {
        return QStringLiteral("线性拟合RMS %1px，大于允许值%2px")
            .arg(track.fitRmsPx, 0, 'f', 2)
            .arg(config.maxTrackFitRmsPx, 0, 'f', 2);
    }

    if (track.speedPxSec <= config.minTrackSpeedPxSec) {
        return QStringLiteral("拟合速度%1px/s，不大于要求%2px/s")
            .arg(track.speedPxSec, 0, 'f', 4)
            .arg(config.minTrackSpeedPxSec, 0, 'f', 4);
    }

    return QString();
}
```

注意：

- 返回空字符串表示轨迹通过全部筛选。
- 不要修改筛选条件顺序。
- 不要把 `minTrackDisplacementPx` 改成累计路径长度。
- 随机抖动首尾回到原位时，被“净位移不足”拒绝是正确行为。

---

## 2.3 新增诊断轨迹选择函数

继续在匿名命名空间中新增：

```cpp
const AlignmentCoarseTracker::Track* selectDiagnosticTrack(
    const QVector<AlignmentCoarseTracker::Track>& tracks)
{
    const AlignmentCoarseTracker::Track* best = nullptr;

    for (const auto& track : tracks) {
        if (track.points.isEmpty()) {
            continue;
        }

        if (!best) {
            best = &track;
            continue;
        }

        if (track.durationSec > best->durationSec) {
            best = &track;
            continue;
        }

        if (qFuzzyCompare(track.durationSec + 1.0,
                          best->durationSec + 1.0) &&
            track.points.size() > best->points.size()) {
            best = &track;
        }
    }

    return best;
}
```

选择规则：

1. 优先持续时间最长；
2. 持续时间相同或非常接近时，选择点数最多的轨迹。

---

## 2.4 新增诊断文本生成函数

继续新增：

```cpp
QString buildTrackDiagnosticText(
    const AlignmentCoarseTracker::Track* track)
{
    if (!track) {
        return QString();
    }

    QString text =
        QStringLiteral("最长轨迹: %1点 / %2s / 净位移%3px / "
                       "拟合速度%4px/s / RMS %5px")
            .arg(track->points.size())
            .arg(track->durationSec, 0, 'f', 1)
            .arg(track->displacementPx, 0, 'f', 2)
            .arg(track->speedPxSec, 0, 'f', 4)
            .arg(track->fitRmsPx, 0, 'f', 2);

    if (!track->rejectionReason.isEmpty()) {
        text += QStringLiteral(" | 未通过: %1")
                    .arg(track->rejectionReason);
    } else if (track->usedForSolve) {
        text += QStringLiteral(" | 已通过轨迹筛选");
    }

    return text;
}
```

---

# Task 3：重写 `addFrame` 中的轨迹统计逻辑

## 文件

```text
src自动采集/AlignmentCoarseEstimator.cpp
```

## 3.1 设置所需轨迹数量

在 `AlignmentCoarseTracker::addFrame` 初始化 `estimate` 后加入：

```cpp
estimate.requiredTrackCount = config.minTracksForCenter;
```

建议放在：

```cpp
estimate.frameSize = frameSize;
```

之后。

---

## 3.2 修改轨迹拟合循环

找到当前逻辑：

```cpp
for (Track& track : m_tracks) {
    track.usedForSolve = false;
    if (track.points.size() >= 2) {
        fitTrackVelocity(&track);
    }
}
```

替换为：

```cpp
for (Track& track : m_tracks) {
    track.usedForSolve = false;
    track.rejectionReason.clear();
    fitTrackVelocity(&track);
}
```

说明：

- `fitTrackVelocity` 自己处理点数不足的情况。
- 每帧都清除上一次拒绝原因。
- 不要只在点数大于等于 2 时调用，因为函数现在负责清理旧值。

---

## 3.3 同时统计“已拟合轨迹”和“可用轨迹”

将当前创建 `usableTracks` 的整个代码段替换为：

```cpp
QVector<Track*> usableTracks;
QVector<double> fittedSpeeds;

for (Track& track : m_tracks) {
    if (track.velocityFitValid) {
        fittedSpeeds.append(track.speedPxSec);
    }

    track.rejectionReason = trackRejectionReason(track, config);
    if (!track.rejectionReason.isEmpty()) {
        continue;
    }

    track.usedForSolve = true;
    usableTracks.append(&track);
}

estimate.fittedTrackCount = fittedSpeeds.size();
estimate.medianFittedSpeedPxSec = medianOf(fittedSpeeds);
estimate.usableTrackCount = usableTracks.size();

QVector<double> usableSpeeds;
for (const Track* track : usableTracks) {
    usableSpeeds.append(track->speedPxSec);
}
estimate.medianSpeedPxSec = medianOf(usableSpeeds);
```

删除旧的这些硬编码判断：

```cpp
if (track.points.size() < config.minTrackPoints)
if (track.durationSec < config.minTrackDurationSec)
if (track.displacementPx < config.minTrackDisplacementPx)
if (track.fitRmsPx > config.maxTrackFitRmsPx)
if (track.speedPxSec <= 0.005)
```

因为它们已经统一放入 `trackRejectionReason`。

确认源码中不再出现：

```cpp
track.speedPxSec <= 0.005
```

而是出现：

```cpp
track.speedPxSec <= config.minTrackSpeedPxSec
```

---

## 3.4 让速度反推角距离不依赖圆心求解成功

当前 `medianPolarDistanceDegFromSpeed` 位于：

```cpp
if (solvedCenter) {
    ...
}
```

内部。

需要改为：

```cpp
estimate.frameCenterPx =
    QPointF(frameSize.width() * 0.5, frameSize.height() * 0.5);

if (!usableTracks.isEmpty()) {
    const double medianSpeedArcsecSec =
        estimate.medianSpeedPxSec * config.plateScaleArcsecPx;
    const double ratio =
        std::clamp(medianSpeedArcsecSec / config.siderealArcsecSec,
                   0.0,
                   1.0);
    estimate.medianPolarDistanceDegFromSpeed =
        qRadiansToDegrees(std::asin(ratio));
}

if (solvedCenter) {
    estimate.northCelestialPolePx = solvedCenterPx;
    estimate.adjustmentVectorPx =
        estimate.northCelestialPolePx - estimate.frameCenterPx;
    estimate.offsetPx =
        pointDistance(estimate.northCelestialPolePx,
                      estimate.frameCenterPx);
    estimate.offsetDeg =
        estimate.offsetPx * config.plateScaleArcsecPx / 3600.0;
}
```

重点：

- 一条可用轨迹可以输出线速度以及由线速度估计的星点极距。
- 一条轨迹仍然不能输出二维北天极圆心。
- 不要在没有可用轨迹时使用随机抖动速度计算极距。
- `medianFittedSpeedPxSec` 是诊断量，不用于角距离反推。

---

## 3.5 填充轨迹覆盖层的新字段

找到创建 `CoarseAlignmentTrackOverlay overlayTrack` 的循环。

修改为：

```cpp
for (const Track& track : m_tracks) {
    if (track.points.isEmpty()) {
        continue;
    }

    CoarseAlignmentTrackOverlay overlayTrack;
    overlayTrack.id = track.id;
    overlayTrack.pointCount = track.points.size();
    overlayTrack.startPx = track.points.first().positionPx;
    overlayTrack.endPx = track.points.last().positionPx;
    overlayTrack.velocityPxSec = track.velocityPxSec;
    overlayTrack.speedPxSec = track.speedPxSec;
    overlayTrack.durationSec = track.durationSec;
    overlayTrack.displacementPx = track.displacementPx;
    overlayTrack.fitRmsPx = track.fitRmsPx;
    overlayTrack.velocityFitValid = track.velocityFitValid;
    overlayTrack.usedForSolve = track.usedForSolve;
    overlayTrack.rejectionReason = track.rejectionReason;
    estimate.tracks.append(overlayTrack);
}
```

---

## 3.6 生成详细诊断文本

在轨迹覆盖层循环结束后加入：

```cpp
estimate.diagnosticText =
    buildTrackDiagnosticText(selectDiagnosticTrack(m_tracks));
```

---

## 3.7 重写状态文本逻辑

将当前：

```cpp
if (usableTracks.size() < config.minTracksForCenter) {
    ...
} else if (!solvedCenter) {
    ...
}
```

替换为以下完整逻辑：

```cpp
if (estimate.detectedCandidateCount <= 0) {
    estimate.tooFewTracks = true;
    estimate.statusText =
        QStringLiteral("粗对准: 未检测到星点，请检查全画幅找星阈值、"
                       "光斑面积和Mono12原始帧");
} else if (estimate.activeTrackCount <= 0) {
    estimate.tooFewTracks = true;
    estimate.statusText =
        QStringLiteral("粗对准: 已检测到候选星点，但尚未建立连续轨迹");
} else if (usableTracks.size() < config.minTracksForCenter) {
    estimate.tooFewTracks = true;

    if (usableTracks.size() == 1) {
        estimate.statusText =
            QStringLiteral("粗对准: 已获得1/%1条可用轨迹，"
                           "速度%2px/s，还需至少1条方向不同的轨迹")
                .arg(config.minTracksForCenter)
                .arg(estimate.medianSpeedPxSec, 0, 'f', 4);
    } else if (estimate.fittedTrackCount > 0) {
        estimate.statusText =
            QStringLiteral("粗对准: 活动轨迹%1条，已拟合%2条，"
                           "可用0/%3；请查看未通过原因")
                .arg(estimate.activeTrackCount)
                .arg(estimate.fittedTrackCount)
                .arg(config.minTracksForCenter);
    } else {
        estimate.statusText =
            QStringLiteral("粗对准: 活动轨迹%1条，正在积累采样点和时间")
                .arg(estimate.activeTrackCount);
    }
} else if (!solvedCenter) {
    estimate.centerIllConditioned = true;
    estimate.statusText =
        QStringLiteral("粗对准: 已有%1条可用轨迹，但轨迹方向接近平行；"
                       "请延长采样或使用绕同一中心旋转的星场")
            .arg(usableTracks.size());
} else if (estimate.centerResidualRmsPx >
           config.maxCenterResidualRmsPx) {
    estimate.statusText =
        QStringLiteral("粗对准: 圆心估计不稳定 RMS %1px")
            .arg(estimate.centerResidualRmsPx, 0, 'f', 1);
} else {
    estimate.valid = true;
    estimate.statusText =
        QStringLiteral("粗对准: 北天极距中心%1px / %2°，"
                       "可用轨迹%3条")
            .arg(estimate.offsetPx, 0, 'f', 0)
            .arg(estimate.offsetDeg, 0, 'f', 2)
            .arg(usableTracks.size());
}
```

注意：

- “等待”只能用于确实正在积累点数或时间的情况。
- 如果持续一分钟仍因净位移不足，应明确显示“净位移不足”，不能继续只显示“请等待”。
- 单星可用轨迹状态必须明确说明缺少另一条方向不同的轨迹。

---

# Task 4：扩展 Canvas 覆盖层数据

## 文件

```text
src自动采集/CanvasWidgets.h
```

## 4.1 扩展 `CoarseDriftTrackOverlay`

将当前：

```cpp
struct CoarseDriftTrackOverlay {
    QPointF startPx;
    QPointF endPx;
    QPointF velocityPxSec;
    double speedPxSec = 0.0;
    bool usedForSolve = false;
};
```

替换为：

```cpp
struct CoarseDriftTrackOverlay {
    int pointCount = 0;
    QPointF startPx;
    QPointF endPx;
    QPointF velocityPxSec;
    double speedPxSec = 0.0;
    double durationSec = 0.0;
    double displacementPx = 0.0;
    double fitRmsPx = 0.0;
    bool velocityFitValid = false;
    bool usedForSolve = false;
    QString rejectionReason;
};
```

---

## 4.2 扩展 `CoarseDriftOverlay`

将当前相关字段：

```cpp
double medianSpeedPxSec = 0.0;
double centerResidualRmsPx = 0.0;
int detectedCandidateCount = 0;
int usableTrackCount = 0;
QString statusText;
```

替换为：

```cpp
double medianSpeedPxSec = 0.0;
double medianFittedSpeedPxSec = 0.0;
double centerResidualRmsPx = 0.0;
int detectedCandidateCount = 0;
int activeTrackCount = 0;
int fittedTrackCount = 0;
int usableTrackCount = 0;
int requiredTrackCount = 0;
QString statusText;
QString diagnosticText;
```

---

# Task 5：将估计结果传给界面

## 文件

```text
src自动采集/DIMM.Alignment.cpp
```

## 5.1 显式设置速度阈值

在 `DIMM::buildCoarseAlignmentConfig()` 中找到：

```cpp
config.maxTrackFitRmsPx = 3.5;
config.maxCenterResidualRmsPx = 80.0;
```

修改为：

```cpp
config.maxTrackFitRmsPx = 3.5;
config.minTrackSpeedPxSec = 0.005;
config.maxCenterResidualRmsPx = 80.0;
```

保持数值与旧行为一致。

---

## 5.2 改善状态级别

当前 `onCoarseAlignmentEstimateReady` 对所有无效结果都倾向显示 Warning。

在设置标签前，新增：

```cpp
UiStatusLevel level = UiStatusLevel::Info;
if (estimate.valid) {
    level = UiStatusLevel::Success;
} else if (estimate.centerIllConditioned) {
    level = UiStatusLevel::Warning;
}
```

然后将：

```cpp
setAlignmentSolveLabel(
    estimate.cameraIndex,
    estimate.statusText,
    estimate.valid ? UiStatusLevel::Success : UiStatusLevel::Warning);
```

修改为：

```cpp
setAlignmentSolveLabel(
    estimate.cameraIndex,
    estimate.statusText,
    level);
```

将：

```cpp
setStatusMessage(
    QStringLiteral("状态: 相机%1 %2")
        .arg(estimate.cameraIndex + 1)
        .arg(estimate.statusText),
    estimate.valid ? UiStatusLevel::Success : UiStatusLevel::Info);
```

修改为：

```cpp
setStatusMessage(
    QStringLiteral("状态: 相机%1 %2")
        .arg(estimate.cameraIndex + 1)
        .arg(estimate.statusText),
    level);
```

行为：

- 正常采样和单星不足：Info。
- 轨迹方向退化：Warning。
- 求解有效：Success。

---

## 5.3 复制新增估计字段

在 `DIMM::updateCoarseAlignmentOverlay` 中，已有：

```cpp
overlay.medianSpeedPxSec = estimate.medianSpeedPxSec;
overlay.centerResidualRmsPx = estimate.centerResidualRmsPx;
overlay.detectedCandidateCount = estimate.detectedCandidateCount;
overlay.usableTrackCount = estimate.usableTrackCount;
overlay.statusText = estimate.statusText;
```

替换为：

```cpp
overlay.medianSpeedPxSec = estimate.medianSpeedPxSec;
overlay.medianFittedSpeedPxSec =
    estimate.medianFittedSpeedPxSec;
overlay.centerResidualRmsPx = estimate.centerResidualRmsPx;
overlay.detectedCandidateCount =
    estimate.detectedCandidateCount;
overlay.activeTrackCount = estimate.activeTrackCount;
overlay.fittedTrackCount = estimate.fittedTrackCount;
overlay.usableTrackCount = estimate.usableTrackCount;
overlay.requiredTrackCount = estimate.requiredTrackCount;
overlay.statusText = estimate.statusText;
overlay.diagnosticText = estimate.diagnosticText;
```

---

## 5.4 复制新增轨迹字段

将轨迹复制循环改为：

```cpp
for (const CoarseAlignmentTrackOverlay& track : estimate.tracks) {
    FullFrameCanvas::CoarseDriftTrackOverlay drawTrack;
    drawTrack.pointCount = track.pointCount;
    drawTrack.startPx = track.startPx;
    drawTrack.endPx = track.endPx;
    drawTrack.velocityPxSec = track.velocityPxSec;
    drawTrack.speedPxSec = track.speedPxSec;
    drawTrack.durationSec = track.durationSec;
    drawTrack.displacementPx = track.displacementPx;
    drawTrack.fitRmsPx = track.fitRmsPx;
    drawTrack.velocityFitValid = track.velocityFitValid;
    drawTrack.usedForSolve = track.usedForSolve;
    drawTrack.rejectionReason = track.rejectionReason;
    overlay.tracks.append(drawTrack);
}
```

---

# Task 6：重写粗对准覆盖层文本

## 文件

```text
src自动采集/CanvasWidgets.cpp
```

## 6.1 保留轨迹线绘制

不要删除当前轨迹线、端点、北天极标记和方向线绘制。

允许保持：

- 可用轨迹：绿色或青色；
- 未通过轨迹：灰色；
- 有效北天极：绿色十字；
- 调整方向：黄色虚线。

---

## 6.2 替换文本生成部分

在 `FullFrameCanvas::drawCoarseDriftOverlay` 中找到：

```cpp
QStringList lines;
if (!m_coarseDriftOverlay.statusText.isEmpty()) {
    lines << m_coarseDriftOverlay.statusText;
}
lines << QStringLiteral("候选 %1 | 轨迹 %2 | 速度 %3 px/s | RMS %4 px")
             .arg(m_coarseDriftOverlay.detectedCandidateCount)
             .arg(m_coarseDriftOverlay.usableTrackCount)
             .arg(m_coarseDriftOverlay.medianSpeedPxSec, 0, 'f', 3)
             .arg(m_coarseDriftOverlay.centerResidualRmsPx, 0, 'f', 1);

painter.setFont(QFont("Microsoft YaHei", 9));
painter.setPen(m_coarseDriftOverlay.valid
                   ? QColor(170, 255, 190)
                   : QColor(255, 210, 90));
painter.drawText(
    imageRect.adjusted(12.0, 70.0, -12.0, -12.0).topLeft(),
    lines.join(QLatin1String(" | ")));
```

替换为：

```cpp
QStringList lines;

if (!m_coarseDriftOverlay.statusText.isEmpty()) {
    lines << m_coarseDriftOverlay.statusText;
}

lines << QStringLiteral(
             "候选 %1 | 活动轨迹 %2 | 已拟合 %3 | 可用轨迹 %4/%5")
             .arg(m_coarseDriftOverlay.detectedCandidateCount)
             .arg(m_coarseDriftOverlay.activeTrackCount)
             .arg(m_coarseDriftOverlay.fittedTrackCount)
             .arg(m_coarseDriftOverlay.usableTrackCount)
             .arg(m_coarseDriftOverlay.requiredTrackCount);

lines << QStringLiteral(
             "拟合速度 %1 px/s | 求解速度 %2 px/s | 圆心RMS %3 px")
             .arg(m_coarseDriftOverlay.medianFittedSpeedPxSec,
                  0,
                  'f',
                  4)
             .arg(m_coarseDriftOverlay.medianSpeedPxSec,
                  0,
                  'f',
                  4)
             .arg(m_coarseDriftOverlay.centerResidualRmsPx,
                  0,
                  'f',
                  1);

if (!m_coarseDriftOverlay.diagnosticText.isEmpty()) {
    lines << m_coarseDriftOverlay.diagnosticText;
}

painter.setFont(QFont("Microsoft YaHei", 9));
painter.setPen(m_coarseDriftOverlay.valid
                   ? QColor(170, 255, 190)
                   : QColor(255, 210, 90));

const QRectF textRect =
    imageRect.adjusted(12.0, 70.0, -12.0, -12.0);

painter.drawText(textRect,
                 Qt::AlignLeft |
                     Qt::AlignTop |
                     Qt::TextWordWrap,
                 lines.join(QLatin1Char('\n')));
```

使用矩形换行而不是单个 `QPointF`，避免长诊断文字超出画面。

---

## 6.3 可选但推荐：给未通过轨迹端点增加简短编号

本步骤可选。如果 Agent 能力不足，可以跳过，不影响核心功能。

在轨迹绘制循环中，端点绘制后增加：

```cpp
if (!track.usedForSolve && track.velocityFitValid) {
    painter.setFont(QFont("Consolas", 7));
    painter.drawText(end + QPointF(4.0, -4.0),
                     QStringLiteral("%1点 %2s")
                         .arg(track.pointCount)
                         .arg(track.durationSec, 0, 'f', 0));
}
```

不要直接把完整拒绝原因绘制在每个星点旁边，避免遮挡图像。

---

# Task 7：新增静态防回归测试

## 文件

```text
src自动采集/tests/test_alignment_coarse_diagnostics_static.py
```

创建以下文件：

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentCoarseDiagnosticsStaticTest(unittest.TestCase):
    def test_estimator_header_exposes_diagnostic_fields(self):
        header = read("AlignmentCoarseEstimator.h")

        for token in [
            "minTrackSpeedPxSec",
            "medianFittedSpeedPxSec",
            "fittedTrackCount",
            "requiredTrackCount",
            "diagnosticText",
            "velocityFitValid",
            "rejectionReason",
            "displacementPx",
            "pointCount",
        ]:
            self.assertIn(token, header)

    def test_estimator_uses_configured_speed_threshold(self):
        cpp = read("AlignmentCoarseEstimator.cpp")

        self.assertIn("trackRejectionReason", cpp)
        self.assertIn("config.minTrackSpeedPxSec", cpp)
        self.assertNotIn("track.speedPxSec <= 0.005", cpp)
        self.assertIn("medianFittedSpeedPxSec", cpp)
        self.assertIn("fittedTrackCount", cpp)
        self.assertIn("buildTrackDiagnosticText", cpp)
        self.assertIn("selectDiagnosticTrack", cpp)

    def test_single_usable_track_has_explicit_status(self):
        cpp = read("AlignmentCoarseEstimator.cpp")

        self.assertIn("已获得1/%1条可用轨迹", cpp)
        self.assertIn("还需至少1条方向不同的轨迹", cpp)
        self.assertIn("净位移%1px，小于要求%2px", cpp)
        self.assertIn("线性拟合RMS", cpp)

    def test_canvas_displays_active_fitted_and_usable_counts(self):
        header = read("CanvasWidgets.h")
        cpp = read("CanvasWidgets.cpp")

        for token in [
            "activeTrackCount",
            "fittedTrackCount",
            "requiredTrackCount",
            "medianFittedSpeedPxSec",
            "diagnosticText",
        ]:
            self.assertIn(token, header)

        self.assertIn("活动轨迹 %2", cpp)
        self.assertIn("已拟合 %3", cpp)
        self.assertIn("可用轨迹 %4/%5", cpp)
        self.assertIn("拟合速度 %1 px/s", cpp)
        self.assertIn("求解速度 %2 px/s", cpp)
        self.assertIn("Qt::TextWordWrap", cpp)

    def test_dimm_copies_new_overlay_fields(self):
        cpp = read("DIMM.Alignment.cpp")

        for token in [
            "overlay.medianFittedSpeedPxSec",
            "overlay.activeTrackCount",
            "overlay.fittedTrackCount",
            "overlay.requiredTrackCount",
            "overlay.diagnosticText",
            "drawTrack.displacementPx",
            "drawTrack.velocityFitValid",
            "drawTrack.rejectionReason",
        ]:
            self.assertIn(token, cpp)

    def test_no_build_system_change_is_required(self):
        cmake = read("CMakeLists.txt")

        self.assertNotIn(
            "test_alignment_coarse_diagnostics_static.py",
            cmake,
        )


if __name__ == "__main__":
    unittest.main()
```

---

## 7.1 运行静态测试

只运行：

```powershell
cd src自动采集
python -m unittest tests.test_alignment_coarse_diagnostics_static
```

预期：

```text
......
----------------------------------------------------------------------
Ran 6 tests

OK
```

这不是 C++ 构建，不会生成可执行文件。

如果测试失败：

- 修复缺失字段或拼写；
- 不要删除断言；
- 不要为了通过测试而注释功能；
- 不要运行构建来“进一步确认”。

---

# Task 8：执行源码文本检查

## 8.1 检查是否仍有速度魔法数字

在 `src自动采集` 目录执行：

```powershell
Select-String `
  -Path "AlignmentCoarseEstimator.cpp" `
  -Pattern "speedPxSec <= 0\.005"
```

预期：无输出。

再执行：

```powershell
Select-String `
  -Path "AlignmentCoarseEstimator.cpp","AlignmentCoarseEstimator.h","DIMM.Alignment.cpp" `
  -Pattern "minTrackSpeedPxSec"
```

预期至少出现：

```text
AlignmentCoarseEstimator.h
AlignmentCoarseEstimator.cpp
DIMM.Alignment.cpp
```

---

## 8.2 检查中文替换字符

执行：

```powershell
@'
from pathlib import Path

files = [
    "AlignmentCoarseEstimator.h",
    "AlignmentCoarseEstimator.cpp",
    "CanvasWidgets.h",
    "CanvasWidgets.cpp",
    "DIMM.Alignment.cpp",
    "tests/test_alignment_coarse_diagnostics_static.py",
]

failed = False
for rel in files:
    text = Path(rel).read_text(encoding="utf-8-sig")
    count = text.count(chr(0xfffd))
    print(f"{rel}: replacement-char count = {count}")
    if count:
        failed = True

if failed:
    raise SystemExit(1)
'@ | python -
```

预期所有文件：

```text
replacement-char count = 0
```

---

## 8.3 检查禁止修改的文件

执行：

```powershell
git diff --name-only
```

允许出现：

```text
src自动采集/AlignmentCoarseEstimator.h
src自动采集/AlignmentCoarseEstimator.cpp
src自动采集/CanvasWidgets.h
src自动采集/CanvasWidgets.cpp
src自动采集/DIMM.Alignment.cpp
src自动采集/tests/test_alignment_coarse_diagnostics_static.py
```

不应出现：

```text
CMakeLists.txt
AlignmentCoarseController.cpp
AlignmentCoarseController.h
FullFrameStarDetector.cpp
CameraManager.cpp
```

如果出现非预期文件，恢复这些无关修改。

---

# Task 9：人工代码审查清单

Agent 完成后，逐项检查：

## 数据语义

- [ ] `activeTrackCount` 表示当前活动轨迹总数。
- [ ] `fittedTrackCount` 表示成功计算线性速度的轨迹数量。
- [ ] `usableTrackCount` 表示通过全部求解筛选的轨迹数量。
- [ ] `requiredTrackCount` 默认是 2。
- [ ] `medianFittedSpeedPxSec` 可以包含尚未通过净位移等门限的轨迹。
- [ ] `medianSpeedPxSec` 只包含可用轨迹。
- [ ] 随机抖动不会被自动当作可用漂移轨迹。
- [ ] 单条可用轨迹不会输出二维北天极中心。
- [ ] 单条可用轨迹可以显示线速度。
- [ ] 单条可用轨迹可以计算速度对应的极距估计。
- [ ] 两条近似平行轨迹仍然返回病态提示。

## 状态文本

- [ ] 没有星点时提示检测问题。
- [ ] 有候选但轨迹不足时提示正在积累。
- [ ] 有拟合但无可用轨迹时提示查看拒绝原因。
- [ ] 净位移不足时显示当前值和门限。
- [ ] 持续时间不足时显示当前值和门限。
- [ ] 点数不足时显示当前值和门限。
- [ ] RMS 过大时显示当前值和门限。
- [ ] 速度过低时显示当前值和门限。
- [ ] 一条可用轨迹时明确显示 `1/2`。
- [ ] 一条可用轨迹时明确说明还需要方向不同的轨迹。
- [ ] 不再把“可用轨迹 0”误写成笼统的“轨迹 0”。

## 线程和性能

- [ ] 没有在 UI 线程增加图像检测。
- [ ] 没有改变后台控制器队列。
- [ ] 没有复制完整图像用于诊断。
- [ ] 新增字段都是轻量数值或短字符串。
- [ ] 没有新增无限增长容器。
- [ ] 没有每帧向日志输出大量文本。
- [ ] 没有新增锁。

---

# Task 10：用户本地实验验收场景

以下场景由用户构建后自己测试。Agent 不执行。

## 场景 A：无星点

输入：

```text
画面中没有满足阈值的光点
```

期望：

```text
候选 0
活动轨迹 0
已拟合 0
可用轨迹 0/2
未检测到星点
```

---

## 场景 B：单星点原地随机抖动

输入：

```text
一个星点围绕原位置随机波动
持续 60 秒
首尾净位移小于 2 px
```

期望：

```text
候选约为 1
活动轨迹约为 1
已拟合约为 1
拟合速度可以是非零小值
可用轨迹 0/2
明确显示净位移不足或速度不足
不输出北天极圆心
```

关键验收：

> 不允许继续只显示“请等待”，必须告诉用户为什么一分钟后仍未通过。

---

## 场景 C：单星点稳定线性漂移

输入：

```text
一个星点
持续 20～30 秒
首尾净位移 3～8 px
轨迹近似直线
RMS 小于 3.5 px
```

期望：

```text
活动轨迹 1
已拟合 1
可用轨迹 1/2
拟合速度非零
求解速度非零
提示还需至少一条方向不同的轨迹
不输出二维北天极圆心
```

---

## 场景 D：两个星点完全平行移动

输入：

```text
两个星点做相同方向的平移
```

期望：

```text
可用轨迹可能达到 2/2
北天极中心求解失败
提示轨迹方向接近平行
不输出伪造圆心
```

---

## 场景 E：两个或更多星点绕同一中心旋转

输入：

```text
至少两个星点
绕同一固定中心缓慢旋转
不同星点的切向速度方向不平行
```

期望：

```text
可用轨迹 >= 2/2
成功求解北天极或模拟旋转中心
显示中心偏移
显示圆心RMS
显示调整方向
```

---

# Task 11：最终交付格式

Agent 最终只需要向用户报告：

```text
已完成修改：
1. AlignmentCoarseEstimator.h
2. AlignmentCoarseEstimator.cpp
3. CanvasWidgets.h
4. CanvasWidgets.cpp
5. DIMM.Alignment.cpp
6. 新增 tests/test_alignment_coarse_diagnostics_static.py

静态测试：
- test_alignment_coarse_diagnostics_static.py：通过/失败

文本检查：
- UTF-8 replacement character：0/存在问题
- 未运行 CMake
- 未运行 MSBuild
- 未执行任何 C++ 构建

需要用户本地构建并完成实验场景 B、C、D、E。
```

不要声称：

```text
编译成功
运行成功
实验验证成功
```

因为本任务明确禁止构建，Agent 没有证据作出这些结论。

---

# 预期最终行为总结

修改前：

```text
候选 1 | 轨迹 0 | 速度 0.000 px/s
粗对准: 采样中 0/2 条轨迹，请等待
```

修改后，单星随机晃动：

```text
候选 1 | 活动轨迹 1 | 已拟合 1 | 可用轨迹 0/2
拟合速度 0.0180 px/s | 求解速度 0.0000 px/s
最长轨迹: 61点 / 60.0s / 净位移0.70px / 拟合速度0.0180px/s / RMS1.20px
未通过: 净位移0.70px，小于要求2.00px
```

修改后，单星稳定漂移：

```text
候选 1 | 活动轨迹 1 | 已拟合 1 | 可用轨迹 1/2
拟合速度 0.2150 px/s | 求解速度 0.2150 px/s
粗对准: 已获得1/2条可用轨迹，还需至少1条方向不同的轨迹
```

修改后，多星旋转：

```text
候选 3 | 活动轨迹 3 | 已拟合 3 | 可用轨迹 3/2
粗对准: 北天极距中心 842px / 0.45°，可用轨迹3条
```

该结果既保留原有求解严谨性，又能让实验室单星测试明确看到跟踪、速度拟合以及轨迹被拒绝的真实原因。
