# DIMM 参数计算与 ROI 同帧预览改造计划

> 执行对象：后续负责改代码的 agent  
> 当前项目路径：`E:\Softwoare\visual studio\project\UI\UI_2`  
> 重要边界：`src87m` 是备份，不要修改；不要改 `CMakeLists.txt`；不要运行 CMake；不要完整构建。

## 0. 结论

这个计划适合当前项目，但原计划里的 `src1/` 路径不适合当前工作区。当前应修改的是 `src/` 目录。

本任务不是 CMake 问题。需要改的是参数计算数据窗口、参数计算前的质心二次筛选、以及 ROI 预览信号与 UI 显示同步。

## 1. 最终目标

只完成三件事：

1. 参数计算窗口从“秒”改成“成功配对样本帧数”
   - 新设置项：`数据采样帧数`
   - 默认值：`12000`
   - 含义：最近 `N` 个成功双相机配对后的 `DifferentialSample`
   - 不再用目标帧率换算 60 秒
   - 不再用 timestamp 判断主参数窗口

2. 参数计算不再做额外质心质量筛选
   - 只要质心算法返回 `centroid.valid == true`，就允许进入双相机 pending 配对队列
   - 删除或绕开参数计算链路上的 `measurementCentroidQuality()` / `isMeasurementUsableCentroid()`
   - 不新增任何质量筛选设置项
   - 自动曝光自己的质量判断、ROI 靠边/丢星安全保护必须保留

3. ROI 图像、红十字、顶部坐标改成同帧预览
   - ROI 图像仍保持约 10 Hz 刷新
   - 新增 `RoiPreviewResult`
   - 每次低频预览同时携带当前帧 ROI 图像、当前帧质心、当前 ROI、frameId
   - UI 不再用 `runtime.centroidX/Y` 拼红十字坐标

## 2. 不要做的事

- 不要改 `src87m`
- 不要改 `CMakeLists.txt`
- 不要运行 CMake
- 不要完整构建
- 不要改质心数学算法
- 不要改高斯拟合、重心公式、Otsu/峰值小核算法
- 不要改 Frame ID 配对逻辑
- 不要把 ROI UI 刷新提高到 200 Hz
- 不要删除 ROI 靠边、丢星、重定位安全逻辑
- 不要删除自动曝光独立质量判断
- 不要新增参数计算质量筛选开关

## 3. 建议修改文件

检查并按需修改：

```text
src/AppConfig.h
src/AppConfigPersistence.cpp
src/SettingsDialog.h
src/SettingsDialog.cpp
src/ConfigApplicationController.h
src/ConfigApplicationController.cpp
src/ImageProcessor.h
src/ImageProcessor.cpp
src/CanvasWidgets.h
src/CanvasWidgets.cpp
src/DIMM.h
src/DIMM.cpp
src/DIMM.Config.cpp
```

如需加静态回归测试，可放在 `tests/`。不要为了本任务改 CMake。

## 4. 配置结构

文件：`src/AppConfig.h`

新增：

```cpp
struct ParameterCalculationConfig {
    int sampleFrameCount = 12000;
};
```

加入 `AppConfig`：

```cpp
struct AppConfig {
    CameraConfig camera;
    AutoExposureConfig autoExposure;
    ProcessingConfig processing;
    ParameterCalculationConfig parameterCalculation;
    RoiRecenteringConfig roiRecentering;
    // ...
};
```

不要创建：

```cpp
CentroidQualityFilterConfig
MeasurementQualityMetrics
```

也不要新增类似字段：

```text
enablePeakAboveThreshold
enablePeakAboveBackground
enableMinSignalPixels
enableMaxSignalPixels
enableMinTotalFlux
enableRoiEdgeMargin
```

## 5. 配置持久化

文件：`src/AppConfigPersistence.cpp`

在 `saveSimpleGroups(...)` 增加：

```cpp
settings.setValue(QStringLiteral("parameterCalculation/sampleFrameCount"),
                  config.parameterCalculation.sampleFrameCount);
```

在 `loadSimpleGroups(...)` 增加：

```cpp
target.parameterCalculation.sampleFrameCount =
    settings.value(QStringLiteral("parameterCalculation/sampleFrameCount"),
                   defaults.parameterCalculation.sampleFrameCount)
        .toInt();
```

不要保存或读取：

```text
parameterCalculation/sampleWindowSec
parameterCalculation/enablePeakAboveThreshold
parameterCalculation/peakThresholdMarginDn
parameterCalculation/enablePeakAboveBackground
parameterCalculation/peakBackgroundMarginDn
parameterCalculation/enableMinSignalPixels
parameterCalculation/minSignalPixels
parameterCalculation/enableMaxSignalPixels
parameterCalculation/maxSignalPixels
parameterCalculation/enableMinTotalFlux
parameterCalculation/minTotalFlux
parameterCalculation/enableRoiEdgeMargin
parameterCalculation/roiEdgeMarginPx
```

旧 QSettings 文件里残留这些 key 没关系，新代码不要再读取。

## 6. 设置窗口

文件：`src/SettingsDialog.h`

新增回调：

```cpp
std::function<void(const ParameterCalculationConfig& config)> onApplyParameterCalculation;
```

新增一个输入框成员：

```cpp
QLineEdit* parameterSampleFrameCountEdit = nullptr;
```

文件：`src/SettingsDialog.cpp`

在“图像处理”页之后新增独立页，标题建议为：

```cpp
QStringLiteral("参数计算")
```

页面里只放一个核心设置项：

```cpp
parameterSampleFrameCountEdit = new QLineEdit(QStringLiteral("12000"));
calculationLayout->addRow(QStringLiteral("数据采样帧数:"), parameterSampleFrameCountEdit);
```

在 `applySettings()` 中读取并校验：

```cpp
ParameterCalculationConfig parameterCalculationConfig;

if (!readIntField(parameterSampleFrameCountEdit,
                  QStringLiteral("数据采样帧数"),
                  &parameterCalculationConfig.sampleFrameCount)) {
    return false;
}

if (parameterCalculationConfig.sampleFrameCount < 2 ||
    parameterCalculationConfig.sampleFrameCount > 200000) {
    showInvalid(QStringLiteral("数据采样帧数必须在 2 到 200000 之间。"));
    return false;
}
```

写入：

```cpp
appConfig.parameterCalculation = parameterCalculationConfig;
configCallbacks.applyParameterCalculation = onApplyParameterCalculation;
```

## 7. 配置应用控制器

文件：`src/ConfigApplicationController.h`

在 `ConfigApplicationCallbacks` 增加：

```cpp
std::function<void(const ParameterCalculationConfig& config)> applyParameterCalculation;
```

文件：`src/ConfigApplicationController.cpp`

在 `applyValidatedConfig(...)` 中，建议放在图像处理配置之后：

```cpp
if (callbacks.applyParameterCalculation) {
    callbacks.applyParameterCalculation(config.parameterCalculation);
}
```

不要放到 `applyPreValidationConfig(...)`。

## 8. DIMM 配置接线

文件：`src/DIMM.h`

新增声明：

```cpp
void setupParameterCalculationSettingsCallbacks();
```

新增成员：

```cpp
ParameterCalculationConfig m_parameterCalculationConfig;
```

文件：`src/DIMM.Config.cpp`

在 `setupSettingsCallbacks()` 中，`setupProcessingSettingsCallbacks();` 后面增加：

```cpp
setupParameterCalculationSettingsCallbacks();
```

实现回调：

```cpp
void DIMM::setupParameterCalculationSettingsCallbacks()
{
    m_settingsDialog->onApplyParameterCalculation =
        [this](const ParameterCalculationConfig& config) {
            m_parameterCalculationConfig = config;

            if (m_imageProcessor) {
                m_imageProcessor->setParameterCalculationConfig(config);
            }

            setStatusMessage(QStringLiteral("参数计算采样帧数已更新，历史样本已重新累计"),
                             UiStatusLevel::Success);
        };
}
```

在 `currentAppConfig()` 增加：

```cpp
config.parameterCalculation = m_parameterCalculationConfig;
```

在 `applyStartupConfig(...)` 增加：

```cpp
m_parameterCalculationConfig = config.parameterCalculation;
```

在 `if (m_imageProcessor) { ... }` 内增加：

```cpp
m_imageProcessor->setParameterCalculationConfig(m_parameterCalculationConfig);
```

在 `if (m_settingsDialog) { ... }` 内同步 UI：

```cpp
if (m_settingsDialog->parameterSampleFrameCountEdit) {
    m_settingsDialog->parameterSampleFrameCountEdit->setText(
        QString::number(m_parameterCalculationConfig.sampleFrameCount));
}
```

## 9. ImageProcessor 接收采样帧数

文件：`src/ImageProcessor.h`

确保能看到 `ParameterCalculationConfig`，必要时增加：

```cpp
#include "AppConfig.h"
```

Worker slot：

```cpp
void setParameterCalculationConfig(ParameterCalculationConfig config);
```

Worker 成员：

```cpp
ParameterCalculationConfig m_parameterCalculationConfig;
```

`ImageProcessor` public wrapper：

```cpp
void setParameterCalculationConfig(const ParameterCalculationConfig& config);
```

文件：`src/ImageProcessor.cpp`

Worker 实现：

```cpp
void ImageProcessorWorker::setParameterCalculationConfig(ParameterCalculationConfig config)
{
    config.sampleFrameCount = std::clamp(config.sampleFrameCount, 2, 200000);

    QMutexLocker locker(&m_mutex);
    m_parameterCalculationConfig = config;
    resetRoiProcessingHistory();
}
```

如果 `resetRoiProcessingHistory()` 当前假设外层已持锁，可以保持这样；如果后来改成内部持锁，避免重复加锁。

Wrapper 可参考项目已有 setter：

```cpp
void ImageProcessor::setParameterCalculationConfig(const ParameterCalculationConfig& config)
{
    QMetaObject::invokeMethod(m_worker,
                              "setParameterCalculationConfig",
                              Qt::QueuedConnection,
                              Q_ARG(ParameterCalculationConfig, config));
}
```

若 queued meta call 需要，注册：

```cpp
Q_DECLARE_METATYPE(ParameterCalculationConfig)
qRegisterMetaType<ParameterCalculationConfig>("ParameterCalculationConfig");
```

也可以使用 queued lambda，按当前项目 Qt 版本实际支持选择。

## 10. 主参数窗口改成样本数

文件：`src/ImageProcessor.h`

删除：

```cpp
static constexpr double ATMOSPHERE_HISTORY_WINDOW_SECONDS = 60.0;
```

保留：

```cpp
static constexpr double TAU0_HISTORY_WINDOW_SECONDS = 3.0;
static constexpr double TAU0_MAX_LAG_MS = 200.0;
static constexpr int TAU0_MIN_SAMPLES = 30;
```

把：

```cpp
static constexpr int MAX_HISTORY_WINDOW = 60000;
```

改成：

```cpp
static constexpr int MAX_HISTORY_WINDOW = 200000;
```

文件：`src/ImageProcessor.cpp`

把 `historyWindowSize()` 改为：

```cpp
int ImageProcessorWorker::historyWindowSize() const
{
    QMutexLocker locker(&m_mutex);
    return std::clamp(m_parameterCalculationConfig.sampleFrameCount,
                      2,
                      MAX_HISTORY_WINDOW);
}
```

`minimumAtmosphereSamples()` 可以继续：

```cpp
return historyWindowSize();
```

`appendDifferentialSample()` 继续按 `historyWindowSize()` 裁剪：

```cpp
while (m_differentialHistory.size() > historyWindowSize()) {
    m_differentialHistory.removeFirst();
}
```

## 11. 删除参数计算前的质心二次筛选

文件：`src/ImageProcessor.cpp`

当前类似逻辑：

```cpp
const bool correctedMeasurementUsable =
    centroid.valid && isMeasurementUsableCentroid(centroid, correctedRoiImage);
const bool measurementUsable = correctedMeasurementUsable;
```

改成：

```cpp
const bool measurementUsable = centroid.valid;
```

并删除这种早退：

```cpp
if (!measurementUsable) {
    emitRoiImageIfDue(...);
    finishProcessing(true);
    return;
}
```

目标逻辑：

```cpp
if (centroid.valid) {
    CentroidResult absoluteCentroid = centroid;
    absoluteCentroid.x += roi.x;
    absoluteCentroid.y += roi.y;

    emit centroidReady(...);

    PendingCentroidSample pending;
    pending.centroid = absoluteCentroid;
    pending.frameId = frameId;
    pending.cameraTimestamp = cameraTimestamp;
    pending.timestampMs = nowMs;

    m_pendingCentroids[cameraIndex].append(pending);
    // 后续 Frame ID 配对逻辑保持不变
}
```

如果 `measurementCentroidQuality()` / `isMeasurementUsableCentroid()` 只服务于参数计算链路，则删除声明和实现。  
如果其它模块还引用它们，只保证参数计算 pending queue 不再依赖它们。

## 12. 自动曝光与 ROI 安全逻辑不要误删

自动曝光相关字段和信号仍保留，例如：

```cpp
autoExposureMeasurementUsable
autoExposureSampleReady(...)
```

这些是自动曝光保护，不属于要删除的“参数计算前筛选”。

ROI 安全逻辑也要保留：

```cpp
isCentroidNearCurrentRoiEdge(...)
handleLiveRoiCentroidLoss(...)
shouldUpdateRoiForRecentering(...)
requestLiveFullFrameRelocalization(...)
```

## 13. ROI 同帧预览结构

文件：`src/ImageProcessor.h`

新增：

```cpp
struct RoiPreviewResult {
    int cameraIndex = -1;
    quint64 frameId = 0;

    RoiRect roi;
    cv::Mat image;

    bool centroidValid = false;

    double localX = 0.0;
    double localY = 0.0;
    double absoluteX = 0.0;
    double absoluteY = 0.0;
};

Q_DECLARE_METATYPE(RoiPreviewResult)
```

必要时增加：

```cpp
#include <QMetaType>
```

## 14. 替换 ROI 信号

文件：`src/ImageProcessor.h`

把 worker 和 wrapper 的：

```cpp
void roiImageReady(int cameraIndex, cv::Mat roiImage);
```

改为：

```cpp
void roiPreviewReady(RoiPreviewResult preview);
```

项目最终不应再有 `roiImageReady`。

保留：

```cpp
static constexpr qint64 ROI_IMAGE_PUBLISH_INTERVAL_MS = 100;
```

## 15. 实现 `emitRoiPreviewIfDue`

文件：`src/ImageProcessor.h`

把：

```cpp
void emitRoiImageIfDue(int cameraIndex,
                       const cv::Mat& roiImage,
                       qint64 nowMs,
                       bool force = false);
```

改为：

```cpp
void emitRoiPreviewIfDue(int cameraIndex,
                         const cv::Mat& roiImage,
                         const RoiRect& roi,
                         const CentroidResult& centroid,
                         quint64 frameId,
                         qint64 nowMs,
                         bool force = false);
```

文件：`src/ImageProcessor.cpp`

实现：

```cpp
void ImageProcessorWorker::emitRoiPreviewIfDue(int cameraIndex,
                                               const cv::Mat& roiImage,
                                               const RoiRect& roi,
                                               const CentroidResult& centroid,
                                               quint64 frameId,
                                               qint64 nowMs,
                                               bool force)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || roiImage.empty()) {
        return;
    }

    if (!force &&
        m_lastRoiImagePublishMs[cameraIndex] > 0 &&
        (nowMs - m_lastRoiImagePublishMs[cameraIndex]) < ROI_IMAGE_PUBLISH_INTERVAL_MS) {
        return;
    }

    m_lastRoiImagePublishMs[cameraIndex] = nowMs;

    RoiPreviewResult preview;
    preview.cameraIndex = cameraIndex;
    preview.frameId = frameId;
    preview.roi = roi;
    preview.image = roiImage.clone();
    preview.centroidValid = centroid.valid;

    if (centroid.valid) {
        preview.localX = centroid.x;
        preview.localY = centroid.y;
        preview.absoluteX = centroid.x + static_cast<double>(roi.x);
        preview.absoluteY = centroid.y + static_cast<double>(roi.y);
    }

    emit roiPreviewReady(preview);
}
```

在 `processFrame()` 里所有旧的：

```cpp
emitRoiImageIfDue(...)
```

全部替换为：

```cpp
emitRoiPreviewIfDue(cameraIndex,
                    roiImage,
                    roi,
                    centroid,
                    frameId,
                    nowMs);
```

对于质心无效的路径，传当前帧的 `centroid`，其中 `centroid.valid == false` 即可。

## 16. 连接新 ROI 信号

文件：`src/ImageProcessor.cpp`

构造函数中把：

```cpp
connect(m_worker, &ImageProcessorWorker::roiImageReady, this, &ImageProcessor::roiImageReady);
```

改为：

```cpp
connect(m_worker, &ImageProcessorWorker::roiPreviewReady, this, &ImageProcessor::roiPreviewReady);
```

文件：`src/DIMM.cpp`

在 `DIMM::registerMetaTypes()` 增加：

```cpp
qRegisterMetaType<RoiPreviewResult>("RoiPreviewResult");
```

如 `ParameterCalculationConfig` 通过 queued meta call 传递，也注册它。

## 17. ROI Canvas 支持只清红十字

文件：`src/CanvasWidgets.h`

新增：

```cpp
void clearCentroid();
```

文件：`src/CanvasWidgets.cpp`

实现：

```cpp
void RoiStarCanvas::clearCentroid()
{
    m_hasCentroid = false;
    update();
}
```

不要用 `clear()` 代替，因为 `clear()` 会清掉 ROI 图像。

## 18. DIMM UI 使用同帧预览

文件：`src/DIMM.cpp`

`setupRoiImageProcessorConnection()` 函数名可以保留，但内部连接改成：

```cpp
connect(m_imageProcessor,
        &ImageProcessor::roiPreviewReady,
        this,
        [this](RoiPreviewResult preview) {
            if (!hasActiveCapture()) {
                return;
            }

            const int camIdx = preview.cameraIndex;
            if (camIdx < 0 || camIdx >= 2) {
                return;
            }

            RoiStarCanvas* canvas = camIdx == 0 ? m_cam1RoiCanvas : m_cam2RoiCanvas;
            QLabel* label = camIdx == 0 ? ui->lblCam1ROICoord : ui->lblCam2ROICoord;
            if (!canvas) {
                return;
            }

            canvas->setRoiImage(preview.image);

            const bool localInsideImage =
                preview.centroidValid &&
                std::isfinite(preview.localX) &&
                std::isfinite(preview.localY) &&
                preview.localX >= 0.0 &&
                preview.localY >= 0.0 &&
                preview.localX < preview.image.cols &&
                preview.localY < preview.image.rows;

            if (localInsideImage) {
                canvas->setCentroid(preview.localX, preview.localY);
                if (label) {
                    label->setText(QStringLiteral("(%1, %2)")
                                       .arg(preview.absoluteX, 0, 'f', 1)
                                       .arg(preview.absoluteY, 0, 'f', 1));
                }
            } else {
                canvas->clearCentroid();
                if (label) {
                    label->setText(QStringLiteral("无有效质心"));
                }
            }
        });
```

同时在 `setupCentroidProcessorConnection()` 中删除高频更新顶部 ROI 坐标的代码：

```cpp
auto* label = camIdx == 0 ? ui->lblCam1ROICoord : ui->lblCam2ROICoord;
label->setText(QStringLiteral("(%1, %2)").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
```

保留高频 `centroidReady` 对运行状态的更新：

```cpp
runtime.centroidX[camIdx] = x;
runtime.centroidY[camIdx] = y;
runtime.peakBrightness[camIdx] = peakValue;
runtime.hasValidCentroid[camIdx] = true;
```

## 19. 期望数据流

参数计算：

```text
单相机图像
-> 热像素修正
-> 质心算法
-> centroid.valid == true
-> centroidReady 高频发送
-> PendingCentroidSample
-> 双相机 Frame ID 对齐
-> DifferentialSample
-> m_differentialHistory
-> 只保留最近 sampleFrameCount 个样本
-> 样本数满 N 后最多每 1 秒发布一次大气参数
```

ROI 预览：

```text
processFrame 当前帧
-> 当前 ROI 图像 + 当前帧质心 + 当前 ROI + frameId
-> 最多每 100 ms 发送一次 RoiPreviewResult
-> UI 同时更新 ROI 图像、红十字、顶部坐标
```

频率关系：

```text
相机采集：约 200 Hz，视配置而定
质心计算：约 200 Hz，视采集而定
双相机配对：最高约 200 Hz
ROI UI 预览：最高约 10 Hz
大气参数发布：最高约 1 Hz
```

## 20. 静态自查

不要构建。建议只做搜索或轻量静态测试。

必须检查：

```text
ATMOSPHERE_HISTORY_WINDOW_SECONDS
parameterCalculation/sampleWindowSec
roiImageReady
emitRoiImageIfDue
sampleFrameCount
roiPreviewReady
measurementCentroidQuality
isMeasurementUsableCentroid
```

期望：

- `ATMOSPHERE_HISTORY_WINDOW_SECONDS`：源码中 0 个引用
- `parameterCalculation/sampleWindowSec`：0 个引用
- `roiImageReady`：0 个引用
- `emitRoiImageIfDue`：0 个引用
- `sampleFrameCount`：覆盖配置、持久化、设置页、DIMM、ImageProcessor
- `roiPreviewReady`：覆盖 `ImageProcessor.h/.cpp` 和 `DIMM.cpp`
- `measurementCentroidQuality` / `isMeasurementUsableCentroid`：若保留，参数计算 pending queue 不再依赖它们
- `setupRoiImageProcessorConnection()` 内不能再用 `runtime.centroidX/Y` 或 `getCurrentRoi()` 生成红十字局部坐标

## 21. 建议静态测试

可新增一个 Python unittest 静态测试，检查：

- `AppConfig.h` 有 `ParameterCalculationConfig`
- 默认 `sampleFrameCount = 12000`
- QSettings key 是 `parameterCalculation/sampleFrameCount`
- 没有 `parameterCalculation/sampleWindowSec`
- `ImageProcessor.h` 没有 `ATMOSPHERE_HISTORY_WINDOW_SECONDS`
- `MAX_HISTORY_WINDOW = 200000`
- `historyWindowSize()` 使用 `m_parameterCalculationConfig.sampleFrameCount`
- `processFrame()` 不再用 `isMeasurementUsableCentroid` 阻止 pending queue
- ROI 信号为 `roiPreviewReady`
- `setupRoiImageProcessorConnection()` 使用 `preview.localX/localY`

如果执行测试，只运行轻量 Python 静态测试，不做 CMake、不构建。

## 22. 最终汇报模板

```markdown
## 已修改文件
- src/...
- tests/...（如果新增）

## 参数计算
- 新设置项：数据采样帧数
- 默认值：12000
- 是否按 DifferentialSample 数量：是/否
- 是否仍按秒换算：是/否
- 修改采样帧数后是否清空历史：是/否

## 质心质量筛选
- 参数计算前固定质量筛选是否删除：是/否
- centroid.valid 是否成为进入 pending queue 的唯一质心算法层条件：是/否
- 自动曝光独立质量判断是否保留：是/否
- ROI 安全重定位逻辑是否保留：是/否

## ROI 同帧预览
- roiPreviewReady：完成/未完成
- ROI 预览频率：仍约 10 Hz/已改变
- 红十字与图像是否同帧：是/否
- 顶部坐标与图像是否同帧：是/否
- 高频 centroidReady 是否保留：是/否

## 静态搜索结果
- roiImageReady 剩余引用：
- emitRoiImageIfDue 剩余引用：
- ATMOSPHERE_HISTORY_WINDOW_SECONDS 剩余引用：
- parameterCalculation/sampleWindowSec 剩余引用：
- sampleFrameCount 引用文件：
- measurementCentroidQuality 剩余引用：
- isMeasurementUsableCentroid 剩余引用：

## 未执行
- 未构建
- 未运行 CMake
- 未完整运行程序
```

## 23. 实施顺序

```text
1. AppConfig 增加 sampleFrameCount
2. QSettings 持久化
3. SettingsDialog 新增“参数计算”页
4. ConfigApplicationController 接线
5. DIMM 保存/加载/应用配置
6. ImageProcessor 接收 sampleFrameCount
7. historyWindowSize 改为 sampleFrameCount
8. 删除参数计算质心质量筛选
9. 确认 centroid.valid 直接进入 pending queue
10. 新增 RoiPreviewResult
11. 替换 roiImageReady 为 roiPreviewReady
12. 新增 RoiStarCanvas::clearCentroid
13. 修改 DIMM ROI UI 同帧显示
14. 静态搜索自查
```
