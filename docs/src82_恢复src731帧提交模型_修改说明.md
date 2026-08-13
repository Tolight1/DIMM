# `src82` 图像处理帧提交模型回退修改说明

## 1. 修改任务

将 `src82` 中 `ImageProcessor` 的“每台相机只保存最新待处理帧”模型，恢复为 `src731质心算法` 已经经过实机长时间验证的“每帧按到达顺序提交到 Worker 线程”模型。

本次修改只处理这一件事：

> 删除 `src82/ImageProcessor` 中新增的 `PendingFrameSlot`、`processLatestFrameLoop()` 和 `clearPendingFrames()`，恢复 `src731质心算法` 的 `ImageProcessor::processFrame()` 提交方式。

---

## 2. 修改目的

当前 `src82` 在 `ImageProcessor` 中为两台相机分别维护一个最新帧槽位：

```text
Camera 0 → PendingFrameSlot[0]
Camera 1 → PendingFrameSlot[1]
```

Worker 忙碌时，新帧会分别覆盖两台相机尚未处理的旧帧。由于两路覆盖彼此独立，可能出现：

```text
Camera 0 实际处理：Frame 100、102、104
Camera 1 实际处理：Frame 101、103、105
```

而后续质心配对要求校准后的 FrameID 精确一致，因此可能增加未配对样本。

`src731质心算法` 已在实际实验中证明能够流畅、长时间观察和计算。本次修改以该版本的帧提交行为为基准，同时保留 `src82` 的其他工程重构。

---

## 3. 必须遵守的范围限制

### 3.1 只允许修改两个文件

```text
src82/ImageProcessor.h
src82/ImageProcessor.cpp
```

### 3.2 禁止修改的文件

不要修改以下任何文件：

```text
src82/DIMM.cpp
src82/DIMM.LiveRoi.cpp
src82/DIMM.AutoExposure.cpp
src82/DIMM.Config.cpp
src82/CameraManager.cpp
src82/CameraManager.h
src82/AutoExposureLogic.h
src82/AutoExposureController.h
src82/CentroidLogic.h
src82/AppConfig.h
```

也不要修改其他 `DIMM.*.cpp`、对准模块、通信模块、配置模块或历史源码目录。

### 3.3 禁止修改的逻辑

本任务不得改变：

- 质心算法；
- 自动曝光算法；
- 热像素修正；
- ROI 更新流程；
- 相机回调；
- `CameraManager` 的 `frameNotificationPending`；
- acquisition generation 的 Worker 侧检查；
- FrameID 校准；
- 双相机质心配对；
- 大气参数计算；
- `frameProcessed`、`centroidReady` 等信号含义；
- 函数参数和信号参数；
- `ImageProcessorWorker::processFrame()`。

### 3.4 不要顺手修复其他问题

即使看到其他潜在问题，也不要在本次修改中处理，例如：

- `setRoiCentroidConfig()` 的成员赋值问题；
- `centroid.valid` 和 `measurementUsable` 的语义问题；
- 配置验证问题；
- 自动曝光状态机问题；
- `DIMM.cpp` 的其他结构问题。

本次提交必须保持单一目的，便于回归验证。

---

# 4. 修改前先确认基线

修改前确认下列两个目录都存在：

```text
src82/
src731质心算法/
```

确认参考文件：

```text
src731质心算法/ImageProcessor.h
src731质心算法/ImageProcessor.cpp
```

本次只参考 `src731质心算法` 中 `ImageProcessor` 外层提交逻辑，不要整文件覆盖，因为 `src82` 的 Worker、大气参数和其他实现已经有变化。

---

# 5. 修改 `src82/ImageProcessor.h`

## 5.1 找到需要删除的代码

在 `class ImageProcessor` 的 `private:` 区域找到以下完整代码：

```cpp
private:
    struct PendingFrameSlot {
        QMutex mutex;
        cv::Mat latestFrame;
        quint64 frameId = 0;
        quint64 cameraTimestamp = 0;
        quint64 acquisitionGeneration = 0;
        quint64 scheduledGeneration = 0;
        bool scheduled = false;
        quint64 replacedFrameCount = 0;
    };

    void processLatestFrameLoop(int cameraIndex);
    void processLatestFrameLoop(int cameraIndex, quint64 scheduledGeneration);
    void clearPendingFrames();

    QThread* m_workerThread = nullptr;
    ImageProcessorWorker* m_worker = nullptr;
    PendingFrameSlot m_pendingFrames[2];
    RoiRect m_currentRoi[2];
```

## 5.2 将其替换为

```cpp
private:
    QThread* m_workerThread = nullptr;
    ImageProcessorWorker* m_worker = nullptr;
    RoiRect m_currentRoi[2];
```

也就是说，必须删除：

```cpp
struct PendingFrameSlot
```

必须删除：

```cpp
void processLatestFrameLoop(int cameraIndex);
void processLatestFrameLoop(int cameraIndex, quint64 scheduledGeneration);
void clearPendingFrames();
```

必须删除：

```cpp
PendingFrameSlot m_pendingFrames[2];
```

## 5.3 不要删除 `QMutex`

虽然删除了 `PendingFrameSlot` 中的互斥锁，但 `ImageProcessorWorker` 仍然使用：

```cpp
QMutex
QMutexLocker
```

因此不要因为本次修改而删除相关 Qt 头文件。

## 5.4 不要修改公开接口

以下声明必须保持原样：

```cpp
public slots:
    void processFrame(int cameraIndex,
                      const cv::Mat& frame,
                      quint64 frameId = 0,
                      quint64 cameraTimestamp = 0,
                      quint64 acquisitionGeneration = 0);
```

不得修改参数类型、顺序、默认值或函数名称。

---

# 6. 修改 `src82/ImageProcessor.cpp`

本文件只修改四个区域：

1. `advanceAcquisitionGeneration()`；
2. `resetProcessingState()`；
3. 删除 `clearPendingFrames()`；
4. 删除两个 `processLatestFrameLoop()`，并替换 `processFrame()`。

---

## 6.1 修改 `advanceAcquisitionGeneration()`

### 当前 `src82` 代码

```cpp
void ImageProcessor::advanceAcquisitionGeneration()
{
    ++(*m_acquisitionGeneration);
    clearPendingFrames();
    QMetaObject::invokeMethod(m_worker, "advanceAcquisitionGeneration", Qt::QueuedConnection);
}
```

### 替换为 `src731质心算法` 行为

```cpp
void ImageProcessor::advanceAcquisitionGeneration()
{
    ++(*m_acquisitionGeneration);
    QMetaObject::invokeMethod(m_worker, "advanceAcquisitionGeneration", Qt::QueuedConnection);
}
```

只删除这一行：

```cpp
clearPendingFrames();
```

不要修改其他内容。

---

## 6.2 修改 `resetProcessingState()`

### 当前 `src82` 代码

```cpp
void ImageProcessor::resetProcessingState()
{
    ++(*m_acquisitionGeneration);
    clearPendingFrames();
    QMetaObject::invokeMethod(m_worker, "resetRunProcessingState", Qt::QueuedConnection);
}
```

### 替换为

```cpp
void ImageProcessor::resetProcessingState()
{
    ++(*m_acquisitionGeneration);
    QMetaObject::invokeMethod(m_worker, "resetRunProcessingState", Qt::QueuedConnection);
}
```

只删除：

```cpp
clearPendingFrames();
```

---

## 6.3 删除 `clearPendingFrames()`

完整删除以下函数，不要保留空函数：

```cpp
void ImageProcessor::clearPendingFrames()
{
    for (PendingFrameSlot& slot : m_pendingFrames) {
        QMutexLocker locker(&slot.mutex);
        slot.latestFrame.release();
        slot.frameId = 0;
        slot.cameraTimestamp = 0;
        slot.acquisitionGeneration = 0;
        slot.scheduledGeneration = 0;
        slot.scheduled = false;
    }
}
```

---

## 6.4 删除第一个 `processLatestFrameLoop()`

完整删除：

```cpp
void ImageProcessor::processLatestFrameLoop(int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    quint64 scheduledGeneration = 0;
    {
        PendingFrameSlot& slot = m_pendingFrames[cameraIndex];
        QMutexLocker locker(&slot.mutex);
        scheduledGeneration = slot.scheduledGeneration;
    }

    processLatestFrameLoop(cameraIndex, scheduledGeneration);
}
```

---

## 6.5 删除第二个 `processLatestFrameLoop()`

完整删除整个函数：

```cpp
void ImageProcessor::processLatestFrameLoop(int cameraIndex, quint64 scheduledGeneration)
{
    ...
}
```

删除范围从：

```cpp
void ImageProcessor::processLatestFrameLoop(int cameraIndex, quint64 scheduledGeneration)
```

开始，一直到该函数自己的最后一个右花括号结束。

不要误删后面的：

```cpp
void ImageProcessor::processFrame(...)
```

---

# 7. 完整替换 `ImageProcessor::processFrame()`

## 7.1 删除当前 `src82` 实现

当前实现内部包含这些特征：

```cpp
const quint64 effectiveGeneration
bool needsWakeup
quint64 scheduledGeneration
PendingFrameSlot& slot
generationChangedWhileScheduled
slot.replacedFrameCount
slot.latestFrame
processLatestFrameLoop
```

必须删除当前 `ImageProcessor::processFrame()` 的整个函数体。

## 7.2 替换为以下完整代码

```cpp
void ImageProcessor::processFrame(int cameraIndex,
                                  const cv::Mat& frame,
                                  quint64 frameId,
                                  quint64 cameraTimestamp,
                                  quint64 acquisitionGeneration)
{
    if (frame.empty() || cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    cv::Mat frameCopy = frame.clone();
    QMetaObject::invokeMethod(m_worker,
                              "processFrame",
                              Qt::QueuedConnection,
                              Q_ARG(int, cameraIndex),
                              Q_ARG(cv::Mat, frameCopy),
                              Q_ARG(quint64, frameId),
                              Q_ARG(quint64, cameraTimestamp),
                              Q_ARG(quint64, acquisitionGeneration));
}
```

该代码必须与 `src731质心算法/ImageProcessor.cpp` 对应实现保持一致。

---

# 8. 修改后的预期结构

修改后，`src82/ImageProcessor.h` 的 `ImageProcessor` 私有成员应类似：

```cpp
private:
    QThread* m_workerThread = nullptr;
    ImageProcessorWorker* m_worker = nullptr;
    RoiRect m_currentRoi[2];
    std::shared_ptr<std::atomic<quint64>> m_acquisitionGeneration =
        std::make_shared<std::atomic<quint64>>(1);

    int m_centroidMode = 0;
    int m_peakKernelMethod = 1;
    int m_peakKernelRadiusPx = 3;
    double m_strongHotPixelExcessDn = 100.0;
    int m_backgroundDenoiseKernelSize = 5;
    double m_backgroundDenoiseSigmaMultiplier = 4.0;
    double m_apertureDiameterMm = 56.0;
    double m_baselineSeparationMm = 250.0;
    double m_baselineAngleDeg = 0.0;
    double m_focalLengthCm = 26.9;
    double m_zenithAngleDeg = 49.6;
    double m_wavelengthNm = 500.0;
    double m_pixelSizeUm = 2.5;
    double m_targetFrameRateHz = 200.0;
};
```

不要用这段覆盖整个类，只用于检查结构。

---

# 9. 修改后的帧流

修改前：

```text
CameraManager 最新帧
    ↓
ImageProcessor PendingFrameSlot[0/1]
    ↓
两路分别覆盖尚未处理的帧
    ↓
单 Worker
```

修改后：

```text
CameraManager 最新帧
    ↓
ImageProcessor::processFrame()
    ↓
frame.clone()
    ↓
Qt QueuedConnection
    ↓
ImageProcessorWorker::processFrame()
```

恢复后的每次提交都保留：

```text
cameraIndex
frame
frameId
cameraTimestamp
acquisitionGeneration
```

并按 Qt 事件队列顺序进入同一个 Worker。

---

# 10. acquisition generation 行为说明

不要试图重新设计 generation 逻辑。

恢复后：

```cpp
ImageProcessor::advanceAcquisitionGeneration()
```

仍然会递增：

```cpp
*m_acquisitionGeneration
```

Worker 仍然保留现有检查：

```cpp
if (acquisitionGeneration > 0 &&
    acquisitionGeneration != m_acquisitionGeneration->load()) {
    ...
}
```

因此，已经携带旧 generation 的排队帧到达 Worker 后仍会被拒绝。

本次只删除 `ImageProcessor` 外层的 `clearPendingFrames()`，不删除 Worker 的 generation 检查，也不修改调用方传入 generation 的方式。

特别注意：

- 不要把 `acquisitionGeneration` 强制替换成 `currentAcquisitionGeneration()`；
- 不要在 `processFrame()` 中自行计算 `effectiveGeneration`；
- 不要改变参数为引用或指针；
- 不要在 Worker 中增加、删除或移动 generation 检查。

---

# 11. 为什么必须使用 `frame.clone()`

恢复后的代码必须保留：

```cpp
cv::Mat frameCopy = frame.clone();
```

原因是 `cv::Mat` 默认是浅拷贝。输入图像可能来自：

- `CameraManager` 的最新帧缓存；
- 相机 SDK 回调复制缓冲；
- DIMM 的临时 ROI；
- 后续可能被覆盖或释放的图像对象。

由于 Worker 通过 `Qt::QueuedConnection` 异步执行，不能只把原始 `cv::Mat` 头部传入队列。

禁止改成：

```cpp
cv::Mat frameCopy = frame;
```

也禁止直接传：

```cpp
Q_ARG(cv::Mat, frame)
```

必须深拷贝：

```cpp
cv::Mat frameCopy = frame.clone();
```

---

# 12. 不要改变连接类型

必须保持：

```cpp
Qt::QueuedConnection
```

禁止改成：

```cpp
Qt::DirectConnection
Qt::BlockingQueuedConnection
Qt::AutoConnection
```

原因：

- `ImageProcessorWorker` 位于独立线程；
- `DirectConnection` 可能让质心计算在调用线程执行；
- `BlockingQueuedConnection` 可能阻塞 GUI 或采集路径；
- 本任务目标是恢复 `src731质心算法` 的已验证行为。

---

# 13. 不要调用 Worker 普通成员函数

禁止写成：

```cpp
m_worker->processFrame(cameraIndex,
                       frameCopy,
                       frameId,
                       cameraTimestamp,
                       acquisitionGeneration);
```

因为这可能绕过 Worker 的线程归属。

必须使用：

```cpp
QMetaObject::invokeMethod(
    m_worker,
    "processFrame",
    Qt::QueuedConnection,
    ...
);
```

---

# 14. 修改后的静态搜索检查

修改完成后，在仓库根目录执行搜索。

## 14.1 以下关键词在 `src82/ImageProcessor.*` 中必须为 0 个结果

```text
PendingFrameSlot
m_pendingFrames
processLatestFrameLoop
clearPendingFrames
replacedFrameCount
scheduledGeneration
needsAnotherWakeup
generationChangedWhileScheduled
```

示例命令：

```bash
git grep -n "PendingFrameSlot" -- src82/ImageProcessor.h src82/ImageProcessor.cpp
git grep -n "m_pendingFrames" -- src82/ImageProcessor.h src82/ImageProcessor.cpp
git grep -n "processLatestFrameLoop" -- src82/ImageProcessor.h src82/ImageProcessor.cpp
git grep -n "clearPendingFrames" -- src82/ImageProcessor.h src82/ImageProcessor.cpp
git grep -n "replacedFrameCount" -- src82/ImageProcessor.h src82/ImageProcessor.cpp
git grep -n "scheduledGeneration" -- src82/ImageProcessor.h src82/ImageProcessor.cpp
```

这些命令应无输出。

## 14.2 以下关键词必须存在

```text
cv::Mat frameCopy = frame.clone();
"processFrame"
Qt::QueuedConnection
Q_ARG(int, cameraIndex)
Q_ARG(cv::Mat, frameCopy)
Q_ARG(quint64, frameId)
Q_ARG(quint64, cameraTimestamp)
Q_ARG(quint64, acquisitionGeneration)
```

示例：

```bash
git grep -n "frameCopy = frame.clone" -- src82/ImageProcessor.cpp
git grep -n 'Q_ARG(cv::Mat, frameCopy)' -- src82/ImageProcessor.cpp
```

---

# 15. Diff 检查要求

执行：

```bash
git diff -- src82/ImageProcessor.h src82/ImageProcessor.cpp
```

预期 Diff 只能包含以下内容：

### `ImageProcessor.h`

- 删除 `PendingFrameSlot`；
- 删除两个 `processLatestFrameLoop()` 声明；
- 删除 `clearPendingFrames()` 声明；
- 删除 `m_pendingFrames[2]`。

### `ImageProcessor.cpp`

- 从 `advanceAcquisitionGeneration()` 删除 `clearPendingFrames()`；
- 从 `resetProcessingState()` 删除 `clearPendingFrames()`；
- 删除 `clearPendingFrames()` 实现；
- 删除两个 `processLatestFrameLoop()` 实现；
- 将 `processFrame()` 替换为 `src731质心算法` 的 clone + queued invoke 实现。

如果 Diff 出现以下文件，说明修改越界：

```text
DIMM.*
CameraManager.*
AutoExposure*
CentroidLogic*
AppConfig*
```

必须撤销这些额外修改。

---

# 16. 禁止采用的错误方案

## 错误方案 A：只关闭覆盖，但保留 PendingFrameSlot

不要通过添加布尔开关禁用覆盖，例如：

```cpp
bool enableLatestFrameCoalescing = false;
```

本任务要求恢复到 `src731质心算法` 的直接排队模型，应删除整套第二层槽位代码。

## 错误方案 B：把槽位改成队列

不要将：

```cpp
cv::Mat latestFrame;
```

改成：

```cpp
QQueue<cv::Mat>
```

这会形成一个新的、未经实验验证的实现，也会增加维护复杂度。

## 错误方案 C：两台相机各增加一个 Worker

不要增加：

```text
Worker 0
Worker 1
```

双 Worker 是另一个架构方案，不属于本次回退任务。

## 错误方案 D：创建 FramePair 缓冲

不要在本次修改中创建 FrameID 帧对队列。成对缓冲可以作为后续独立任务，本次只恢复已经验证的 `src731质心算法` 行为。

## 错误方案 E：修改 CameraManager

`CameraManager` 已经具有自己的最新帧和通知合并机制。本次不要删除、增加或修改：

```cpp
latestFrame
latestFramePacket
frameNotificationPending
frameReady
takeLatestFramePacket
```

## 错误方案 F：为了清理警告而删除 Worker 的 QMutex

`ImageProcessorWorker` 大量使用 QMutex。不要因为删除了 `PendingFrameSlot::mutex` 就删除公共互斥锁依赖。

---

# 17. 编译检查项目

由项目维护者使用现有环境编译。Agent 不要因为本机缺少厂商 SDK 而修改源码规避依赖。

编译时重点确认：

- 不存在 `PendingFrameSlot` 未定义；
- 不存在 `m_pendingFrames` 未定义；
- 不存在 `clearPendingFrames` 未定义；
- 不存在 `processLatestFrameLoop` 未定义；
- `QMetaObject::invokeMethod` 参数匹配；
- `cv::Mat` 已通过 `qRegisterMetaType<cv::Mat>()` 注册；
- `ImageProcessorWorker::processFrame` 仍是 Qt slot；
- 两个修改文件编码保持 UTF-8。

如本机缺少 Galaxy SDK、Qt、OpenCV 或其他硬件库，只报告环境问题，不要修改本任务范围外的源代码。

---

# 18. 基本运行验证

## 18.1 启动/停止

检查：

1. 程序能够启动；
2. 相机能够连接；
3. 实时采集能够启动；
4. 实时采集能够停止；
5. 停止后程序不崩溃；
6. 再次启动采集能够恢复。

## 18.2 ROI 流程

检查：

1. 全画幅定位正常；
2. 双相机 ROI 能够建立；
3. ROI 更新正常；
4. ROI 更新后两台相机继续采集；
5. 重定位后不会混入旧 generation 的测量结果。

## 18.3 质心与配对

观察：

- 两台相机质心持续输出；
- paired sample 持续增加；
- dropped unpaired 不应因本次回退异常上升；
- FrameID 配对保持稳定；
- 同步残差保持在历史实验的合理范围。

## 18.4 长时间运行

建议与 `src731质心算法` 使用相同条件：

- 相同两台相机；
- 相同触发频率；
- 相同曝光和增益；
- 相同 ROI 尺寸；
- 相同质心模式；
- 相同热像素模板；
- 相同自动曝光设置。

至少记录：

```text
运行时间
相机0接收帧数
相机1接收帧数
相机0处理帧数
相机1处理帧数
双相机有效质心数
配对样本数
未配对样本数
平均处理耗时
最大处理耗时
内存占用趋势
停止采集后的响应时间
```

---

# 19. 回归通过标准

满足以下条件即可认为本次修改完成：

- 只修改 `src82/ImageProcessor.h` 和 `src82/ImageProcessor.cpp`；
- `PendingFrameSlot` 完全删除；
- `m_pendingFrames` 完全删除；
- `processLatestFrameLoop()` 完全删除；
- `clearPendingFrames()` 完全删除；
- `processFrame()` 与 `src731质心算法` 的提交实现一致；
- 使用 `frame.clone()`；
- 使用 `Qt::QueuedConnection`；
- 不修改 Worker 质心处理；
- 不修改双相机 FrameID 配对；
- 不修改 ROI 和 CameraManager；
- 能通过项目现有编译；
- 实机运行时配对样本持续产生；
- 不出现明显新增崩溃、死锁或停止异常。

---

# 20. 建议的提交信息

```text
Restore src731 frame submission behavior in ImageProcessor
```

或中文：

```text
恢复 ImageProcessor 的 src731 顺序帧提交模型
```

提交说明建议写明：

```text
- 删除 src82 的 per-camera latest-frame pending slots
- 删除 processLatestFrameLoop 和 clearPendingFrames
- 恢复 clone + Qt queued invoke 的帧提交方式
- 不修改质心、自动曝光、ROI、CameraManager 和 FrameID 配对逻辑
```

---

# 21. Agent 最终回复模板

完成后只需报告：

```text
已完成 src82 ImageProcessor 帧提交模型回退。

修改文件：
- src82/ImageProcessor.h
- src82/ImageProcessor.cpp

完成内容：
- 删除 PendingFrameSlot 和 m_pendingFrames
- 删除 clearPendingFrames
- 删除两个 processLatestFrameLoop
- advanceAcquisitionGeneration/resetProcessingState 不再清理 pending slot
- processFrame 恢复为 frame.clone() + Qt::QueuedConnection 提交

未修改：
- CameraManager
- ROI 流程
- 质心算法
- 自动曝光
- FrameID 配对
- 大气参数计算

验证：
- 已搜索确认不存在 PendingFrameSlot/m_pendingFrames/processLatestFrameLoop/clearPendingFrames
- [填写编译状态]
- [填写实机测试状态]
```

---

# 22. 最终提醒

本任务的关键不是“重新优化线程”，而是：

> 在保留 `src82` 工程重构的前提下，仅将 `ImageProcessor` 外层帧提交方式恢复为已经通过实机长时间验证的 `src731质心算法` 行为。

不要扩大任务范围，不要重新设计，不要修改其他模块。
