# DIMM 硬件触发分阶段确认与自动采集自恢复（二次修复）执行任务

> 目标仓库：`Tolight1/DIMM`  
> 修改目录：`src1分钟窗口/`  
> 执行要求：**只修改代码，不构建、不运行、不测试、不提交 Git**  
> 适用对象：能力较弱、上下文理解有限的执行 Agent  
> 原则：严格按本文步骤操作，不重构无关代码，不撤销前一轮已经完成的自动采集窗口修复。

---

## 1. 本次任务只解决什么

前一轮已经增加了自动采集窗口抑制、首帧检查、失败停止和自动重试，但当前仍有四个问题：

1. 全画幅低频阶段两台相机各收到一帧后，就过早执行 `m_liveStartupConfirmed = true`；
2. ROI 写入后切换到高频脉冲时，即使串口 ACK 超时，也直接进入 `Tracking`，没有确认两台相机是否真的收到新的 ROI 图像；
3. ROI 高频阶段的 `m_pulseBoardResponseTimedOut` 可能无法清除，UI 会持续出现 timeout 警告；
4. 手动启动的内部重试再次进入 `onStartCapture()` 时，可能把 `m_liveStartupRetryCount` 清零，导致一直显示第 1/3 次重试。

修复后，硬件触发启动必须按下列状态执行：

```text
None
  ↓
WaitingFullFramePair
  ↓  两台相机收到新的全画幅帧
全画幅定位与 ROI 写入
  ↓
WaitingRoiTrackingPair
  ↓  两台相机收到新的 64×64 ROI 帧
Running
```

只有 `WaitingRoiTrackingPair` 成功后，才允许：

```cpp
m_liveStartupConfirmed = true;
```

---

## 2. 允许修改的文件

只修改：

```text
src1分钟窗口/DIMM.h
src1分钟窗口/DIMM.cpp
src1分钟窗口/DIMM.CommCamera.cpp
src1分钟窗口/DIMM.LiveRoi.cpp
```

不要修改：

```text
DIMM.AutoExposure.cpp
DIMM.Config.cpp
AutoAcquisitionScheduler.cpp
CameraManager.*
PulseGeneratorManager.*
ImageProcessor.*
任何 .ui 文件
```

---

## 3. 禁止事项

- 不要构建；
- 不要运行；
- 不要执行测试；
- 不要增加 `qDebug()`；
- 不要增加日志文件；
- 不要修改 UI 布局；
- 不要修改串口协议；
- 不要重构相机或脉冲板管理器；
- 不要修改 CSV；
- 不要提交 Git。

---

# 第一部分：修改 `DIMM.h`

## 4. 增加硬件触发启动阶段枚举

在现有：

```cpp
enum class LiveStartupOrigin {
    Manual,
    AutoAcquisition
};
```

后增加：

```cpp
enum class HardwareTriggerStartupStage {
    None,
    WaitingFullFramePair,
    WaitingRoiTrackingPair,
    Running
};
```

含义：

- `None`：当前没有等待中的触发确认；
- `WaitingFullFramePair`：等待两台相机收到新的全画幅低频触发帧；
- `WaitingRoiTrackingPair`：等待两台相机收到新的 ROI 高频触发帧；
- `Running`：ROI 高频阶段已经确认，采集正式运行。

---

## 5. 增加函数声明

在以下已有声明附近：

```cpp
void scheduleHardwareTriggerStartupCheck();
void checkHardwareTriggerStartup();
void confirmHardwareTriggerStartupIfReady();
```

增加：

```cpp
void beginHardwareTriggerStartupStage(
    HardwareTriggerStartupStage stage);

void recordHardwareTriggerStartupFrame(
    int cameraIndex,
    bool frameLooksLikeHardwareRoi);
```

相关声明最终应为：

```cpp
void scheduleHardwareTriggerStartupCheck();
void checkHardwareTriggerStartup();

void beginHardwareTriggerStartupStage(
    HardwareTriggerStartupStage stage);

void recordHardwareTriggerStartupFrame(
    int cameraIndex,
    bool frameLooksLikeHardwareRoi);

void confirmHardwareTriggerStartupIfReady();
void handleHardwareTriggerStartupFailure(
    const QString& detail);

bool shouldRetryFailedLiveStartup() const;
void retryFailedLiveStartup();
void resetLiveStartupRecoveryState(
    bool resetRetryCount);
```

---

## 6. 增加成员字段

在现有：

```cpp
LiveStartupOrigin m_liveStartupOrigin =
    LiveStartupOrigin::Manual;

bool m_liveStartupConfirmed = false;
bool m_liveStartupRecoveryInProgress = false;
bool m_pulseBoardResponseTimedOut = false;
```

附近增加：

```cpp
HardwareTriggerStartupStage
    m_hardwareTriggerStartupStage =
        HardwareTriggerStartupStage::None;

quint64
    m_hardwareTriggerStageBaselineFrameCount[2] =
        {0, 0};

bool
    m_hardwareTriggerStageFrameSeen[2] =
        {false, false};

bool m_internalLiveStartupRetry = false;
```

字段作用：

- `m_hardwareTriggerStartupStage`：当前确认阶段；
- `m_hardwareTriggerStageBaselineFrameCount`：阶段开始时的帧数基线；
- `m_hardwareTriggerStageFrameSeen`：阶段开始后各相机是否收到符合尺寸要求的新帧；
- `m_internalLiveStartupRetry`：当前 `onStartCapture()` 是否由内部自动重试调用。

---

## 7. 增加 ROI 高频确认超时常量

在：

```cpp
static constexpr int kHardwareTriggerFirstFrameTimeoutMs = 5000;
static constexpr int kLiveStartupRetryDelayMs = 3000;
static constexpr int kLiveStartupMaxImmediateRetries = 3;
```

中间增加：

```cpp
static constexpr int kRoiTrackingFirstFrameTimeoutMs = 3000;
```

最终：

```cpp
static constexpr int kHardwareTriggerFirstFrameTimeoutMs = 5000;
static constexpr int kRoiTrackingFirstFrameTimeoutMs = 3000;
static constexpr int kLiveStartupRetryDelayMs = 3000;
static constexpr int kLiveStartupMaxImmediateRetries = 3;
```

---

# 第二部分：实现阶段初始化与阶段帧记录

## 8. 在 `DIMM.CommCamera.cpp` 中实现阶段初始化

在 `scheduleHardwareTriggerStartupCheck()` 前增加：

```cpp
void DIMM::beginHardwareTriggerStartupStage(
    HardwareTriggerStartupStage stage)
{
    m_hardwareTriggerStartupStage = stage;

    const auto& runtime = activeRuntime();

    for (int cameraIndex = 0;
         cameraIndex < 2;
         ++cameraIndex) {
        m_hardwareTriggerStageBaselineFrameCount[cameraIndex] =
            runtime.frameCountPerCamera[cameraIndex];

        m_hardwareTriggerStageFrameSeen[cameraIndex] =
            false;
    }

    scheduleHardwareTriggerStartupCheck();
}
```

每次进入新阶段都必须重新记录基线，不能复用上一阶段的帧状态。

---

## 9. 修改 `scheduleHardwareTriggerStartupCheck()`

将当前固定 5 秒版本替换为：

```cpp
void DIMM::scheduleHardwareTriggerStartupCheck()
{
    if (!m_hardwareTriggerStartupTimer) {
        return;
    }

    int timeoutMs =
        kHardwareTriggerFirstFrameTimeoutMs;

    if (m_hardwareTriggerStartupStage ==
        HardwareTriggerStartupStage::WaitingRoiTrackingPair) {
        timeoutMs =
            kRoiTrackingFirstFrameTimeoutMs;
    }

    m_hardwareTriggerStartupTimer->start(timeoutMs);
}
```

结果：

```text
全画幅低频确认：5 秒
ROI 高频确认：3 秒
```

---

## 10. 实现 `recordHardwareTriggerStartupFrame()`

在 `DIMM.CommCamera.cpp` 增加：

```cpp
void DIMM::recordHardwareTriggerStartupFrame(
    int cameraIndex,
    bool frameLooksLikeHardwareRoi)
{
    if (cameraIndex < 0 ||
        cameraIndex >= 2 ||
        m_captureState != CaptureState::Live ||
        m_configTriggerMode == 0) {
        return;
    }

    const auto stage =
        m_hardwareTriggerStartupStage;

    if (stage != HardwareTriggerStartupStage::WaitingFullFramePair &&
        stage != HardwareTriggerStartupStage::WaitingRoiTrackingPair) {
        return;
    }

    const auto& runtime = activeRuntime();

    const bool isNewStageFrame =
        runtime.frameCountPerCamera[cameraIndex] >
        m_hardwareTriggerStageBaselineFrameCount[cameraIndex];

    if (!isNewStageFrame) {
        return;
    }

    if (stage ==
        HardwareTriggerStartupStage::WaitingFullFramePair) {
        // 全画幅阶段不能使用 64×64 ROI 帧完成确认。
        if (frameLooksLikeHardwareRoi) {
            return;
        }

        m_hardwareTriggerStageFrameSeen[cameraIndex] = true;
    } else {
        // ROI 高频阶段必须收到 64×64 或更小的硬件 ROI 图像。
        if (!frameLooksLikeHardwareRoi) {
            return;
        }

        m_hardwareTriggerStageFrameSeen[cameraIndex] = true;
    }

    confirmHardwareTriggerStartupIfReady();
}
```

---

# 第三部分：修改图像接收入口

## 11. 修改 `handleLiveFramePacket()`

找到：

```cpp
++runtime.frameCount;

if (m_configTriggerMode != 0) {
    confirmHardwareTriggerStartupIfReady();
}

if (m_configTriggerMode != 0 &&
    m_pulseBoardResponseTimedOut) {
    setPulseBoardResponseTimeoutStatus(
        QStringLiteral(
            "状态: 已收到硬件触发图像帧，脉冲板未返回串口应答但采集继续"));
}
```

替换为：

```cpp
++runtime.frameCount;

if (m_configTriggerMode != 0) {
    recordHardwareTriggerStartupFrame(
        cameraIndex,
        frameLooksLikeHardwareRoi);
}
```

要求：

- 删除逐帧重复显示 timeout 的逻辑；
- 不再对任意硬件触发图像直接调用旧的确认函数；
- 由阶段和图像尺寸决定是否完成确认。

---

# 第四部分：重写阶段确认函数

## 12. 完整替换 `confirmHardwareTriggerStartupIfReady()`

使用：

```cpp
void DIMM::confirmHardwareTriggerStartupIfReady()
{
    if (m_captureState != CaptureState::Live ||
        m_configTriggerMode == 0) {
        return;
    }

    const auto stage =
        m_hardwareTriggerStartupStage;

    if (stage != HardwareTriggerStartupStage::WaitingFullFramePair &&
        stage != HardwareTriggerStartupStage::WaitingRoiTrackingPair) {
        return;
    }

    const bool bothReady =
        m_hardwareTriggerStageFrameSeen[0] &&
        m_hardwareTriggerStageFrameSeen[1];

    if (!bothReady) {
        return;
    }

    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    const bool hadPulseBoardTimeout =
        m_pulseBoardResponseTimedOut;

    m_pulseBoardResponseTimedOut = false;
    m_lastPulseBoardTimeoutStatusMs = -1;

    if (stage ==
        HardwareTriggerStartupStage::WaitingFullFramePair) {
        /*
         * 这里只确认低频全画幅触发有效。
         * 不能将整个采集标记为最终启动成功。
         */
        m_hardwareTriggerStartupStage =
            HardwareTriggerStartupStage::None;

        if (hadPulseBoardTimeout) {
            setStatusMessage(
                QStringLiteral(
                    "状态: 脉冲板未返回全画幅触发应答，但双相机已收到新的全画幅图像，继续定位"),
                UiStatusLevel::Warning);
        } else {
            setStatusMessage(
                QStringLiteral(
                    "状态: 双相机全画幅低频触发已确认，继续进行星点定位"),
                UiStatusLevel::Success);
        }

        return;
    }

    /*
     * 只有 ROI 高频阶段双相机都收到新 ROI 帧，
     * 才算完整启动成功。
     */
    m_hardwareTriggerStartupStage =
        HardwareTriggerStartupStage::Running;

    m_liveStartupConfirmed = true;
    m_liveStartupRecoveryInProgress = false;
    m_liveStartupRetryCount = 0;

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->stop();
    }

    if (m_liveStartupOrigin ==
        LiveStartupOrigin::AutoAcquisition) {
        setAutoAcquisitionStatus(
            QStringLiteral(
                "自动采集启动成功，双相机 ROI 高频触发已确认"),
            UiStatusLevel::Success,
            QStringLiteral("auto-start-confirmed"));
    }

    if (hadPulseBoardTimeout) {
        setStatusMessage(
            QStringLiteral(
                "状态: 脉冲板未返回 ROI 高频切换应答，但双相机已收到新的 ROI 图像，继续采集"),
            UiStatusLevel::Warning);
    } else {
        setStatusMessage(
            QStringLiteral(
                "状态: 双相机 ROI 高频触发确认成功，实时采集已稳定运行"),
            UiStatusLevel::Success);
    }
}
```

关键限制：

```cpp
m_liveStartupConfirmed = true;
```

只能出现在 ROI 高频确认成功分支。

---

# 第五部分：重写阶段超时函数

## 13. 完整替换 `checkHardwareTriggerStartup()`

使用：

```cpp
void DIMM::checkHardwareTriggerStartup()
{
    if (m_captureState != CaptureState::Live ||
        m_configTriggerMode == 0) {
        return;
    }

    const auto stage =
        m_hardwareTriggerStartupStage;

    if (stage != HardwareTriggerStartupStage::WaitingFullFramePair &&
        stage != HardwareTriggerStartupStage::WaitingRoiTrackingPair) {
        return;
    }

    if (m_hardwareTriggerStageFrameSeen[0] &&
        m_hardwareTriggerStageFrameSeen[1]) {
        confirmHardwareTriggerStartupIfReady();
        return;
    }

    QString detail;

    if (stage ==
        HardwareTriggerStartupStage::WaitingFullFramePair) {
        if (!m_hardwareTriggerStageFrameSeen[0] &&
            !m_hardwareTriggerStageFrameSeen[1]) {
            detail = QStringLiteral(
                "全画幅低频触发后，两台相机均未收到新的全画幅图像");
        } else if (!m_hardwareTriggerStageFrameSeen[0]) {
            detail = QStringLiteral(
                "全画幅低频触发后，只有相机2收到新图像，相机1未触发");
        } else {
            detail = QStringLiteral(
                "全画幅低频触发后，只有相机1收到新图像，相机2未触发");
        }
    } else {
        if (!m_hardwareTriggerStageFrameSeen[0] &&
            !m_hardwareTriggerStageFrameSeen[1]) {
            detail = QStringLiteral(
                "ROI 高频触发切换后，两台相机均未收到新的 64×64 ROI 图像");
        } else if (!m_hardwareTriggerStageFrameSeen[0]) {
            detail = QStringLiteral(
                "ROI 高频触发切换后，只有相机2收到新的 ROI 图像，相机1未触发");
        } else {
            detail = QStringLiteral(
                "ROI 高频触发切换后，只有相机1收到新的 ROI 图像，相机2未触发");
        }
    }

    handleHardwareTriggerStartupFailure(detail);
}
```

该检查由独立 `QTimer` 驱动，即使后续完全无图像也会执行。

---

# 第六部分：修改初始全画幅触发启动

## 14. 修改 `DIMM.cpp` 的 `onStartCapture()`

找到硬件触发分支中：

```cpp
m_liveStartupPhase = LiveStartupPhase::LocatePair;
const bool reuseRunningPulse =
    isFullFrameLocalizationPulseRunning();
```

将后续全画幅脉冲处理整理为：

```cpp
m_liveStartupPhase =
    LiveStartupPhase::LocatePair;

const bool reuseRunningPulse =
    isFullFrameLocalizationPulseRunning();

if (reuseRunningPulse) {
    beginHardwareTriggerStartupStage(
        HardwareTriggerStartupStage::WaitingFullFramePair);

    setStatusMessage(
        QStringLiteral(
            "状态: 硬件触发已就绪，复用当前脉冲输出并等待双相机新的全画幅图像"),
        UiStatusLevel::Success);

    return;
}

if (!startFullFrameLocalizationPulse(&reason)) {
    if (isPulseBoardResponseTimeout(reason)) {
        m_pulseBoardResponseTimedOut = true;

        beginHardwareTriggerStartupStage(
            HardwareTriggerStartupStage::WaitingFullFramePair);

        setPulseBoardResponseTimeoutStatus(
            QStringLiteral(
                "状态: 脉冲板全画幅触发应答超时，继续等待双相机新的全画幅图像确认触发是否生效"));

        return;
    }

    m_hardwareTriggerStartupStage =
        HardwareTriggerStartupStage::None;

    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    m_liveStartupConfirmed = false;
    m_pulseBoardResponseTimedOut = false;

    m_cameraManager->stopAll();

    updateCaptureState(CaptureState::Idle);

    setStatusMessage(
        reason.isEmpty()
            ? QStringLiteral("状态: 全画幅低频触发启动失败")
            : reason,
        UiStatusLevel::Error);

    QMessageBox::warning(
        this,
        QStringLiteral("开始采集"),
        reason.isEmpty()
            ? QStringLiteral("全画幅低频触发启动失败。")
            : reason);

    return;
}

beginHardwareTriggerStartupStage(
    HardwareTriggerStartupStage::WaitingFullFramePair);

setStatusMessage(
    m_pulseGeneratorEnabled
        ? QStringLiteral(
              "状态: 全画幅低频触发已发起，等待双相机新的全画幅图像")
        : QStringLiteral(
              "状态: 请输出低频脉冲，等待双相机新的全画幅图像"),
    m_pulseGeneratorEnabled
        ? UiStatusLevel::Success
        : UiStatusLevel::Warning);

return;
```

删除该硬件分支末尾旧的：

```cpp
scheduleHardwareTriggerStartupCheck();
```

因为 `beginHardwareTriggerStartupStage()` 已经启动定时器。

---

# 第七部分：修改 ROI 高频切换

## 15. 修改 `DIMM.LiveRoi.cpp` 的 `commitPairedInitialRoisIfReady()`

找到：

```cpp
bool roiPulseResponseTimeout = false;

if (m_captureState == CaptureState::Live &&
    !switchToRoiTrackingPulse(&reason)) {
    ...
}
```

替换为：

```cpp
bool roiPulseResponseTimeout = false;

if (m_captureState == CaptureState::Live) {
    if (!switchToRoiTrackingPulse(&reason)) {
        if (!isPulseBoardResponseTimeout(reason)) {
            m_liveHardwareRoiActive = false;
            clearPendingLiveRelocalizationRois();

            m_hardwareTriggerStartupStage =
                HardwareTriggerStartupStage::None;

            if (m_hardwareTriggerStartupTimer) {
                m_hardwareTriggerStartupTimer->stop();
            }

            handleHardwareTriggerStartupFailure(
                reason.isEmpty()
                    ? QStringLiteral("ROI 高频触发切换失败")
                    : reason);

            return false;
        }

        roiPulseResponseTimeout = true;
        m_pulseBoardResponseTimedOut = true;
    }

    /*
     * switchToRoiTrackingPulse() 已经返回。
     * Qt 当前线程中的图像回调尚未处理，因此此时记录帧数基线。
     */
    beginHardwareTriggerStartupStage(
        HardwareTriggerStartupStage::WaitingRoiTrackingPair);
}
```

然后保留：

```cpp
m_imageProcessor->setPairRois(actualRois);
runtime.initialRoiConfirmed[0] = true;
runtime.initialRoiConfirmed[1] = true;
m_liveHardwareRoiActive =
    m_captureState == CaptureState::Live;
m_liveStartupPhase =
    LiveStartupPhase::Tracking;
```

但此处不要设置：

```cpp
m_liveStartupConfirmed = true;
```

---

## 16. 修改 ROI 切换后的提示文字

将当前成功提示替换为：

```cpp
if (roiPulseResponseTimeout) {
    setPulseBoardResponseTimeoutStatus(
        QStringLiteral(
            "状态: ROI 已写入，脉冲板未返回高频切换应答；正在等待双相机新的 ROI 图像确认触发是否生效"));
} else {
    setStatusMessage(
        QStringLiteral(
            "状态: ROI 已写入并已发起高频触发，等待双相机新的 ROI 图像确认"),
        UiStatusLevel::Warning);
}
```

此时禁止显示“启动成功”或“稳定运行”。

---

# 第八部分：修复全画幅重定位中的英文 timeout

## 17. 修改 `applyLiveFullFrameForRelocalization()`

找到函数末尾：

```cpp
advanceLiveAcquisitionGeneration();

if (m_configTriggerMode != 0) {
    return startFullFrameLocalizationPulse(reason);
}
```

替换为：

```cpp
advanceLiveAcquisitionGeneration();

if (m_configTriggerMode != 0) {
    QString pulseReason;

    const bool pulseStarted =
        startFullFrameLocalizationPulse(&pulseReason);

    if (!pulseStarted) {
        if (!isPulseBoardResponseTimeout(pulseReason)) {
            if (reason) {
                *reason =
                    pulseReason.isEmpty()
                        ? QStringLiteral(
                              "全画幅重定位低频触发启动失败。")
                        : pulseReason;
            }

            return false;
        }

        m_pulseBoardResponseTimedOut = true;

        setPulseBoardResponseTimeoutStatus(
            QStringLiteral(
                "状态: 全画幅重定位脉冲板应答超时，继续等待双相机新的全画幅图像确认触发是否生效"));
    }

    beginHardwareTriggerStartupStage(
        HardwareTriggerStartupStage::WaitingFullFramePair);

    if (reason) {
        reason->clear();
    }

    /*
     * ACK 超时时返回 true：
     * 相机全画幅切换已经完成，是否真的有脉冲由后续图像确认。
     */
    return true;
}
```

保留后面的连续采集模式代码。

这样 `requestLiveFullFrameRelocalization()` 和重定位看门狗不会再直接显示底层英文 timeout。

---

# 第九部分：修复手动重试次数被清零

## 18. 修改 `onStartCapture()` 开头

当前：

```cpp
if (!m_autoAcquisitionCommandInProgress &&
    !m_liveStartupRecoveryInProgress &&
    m_captureState != CaptureState::Live) {
```

替换为：

```cpp
if (!m_autoAcquisitionCommandInProgress &&
    !m_internalLiveStartupRetry &&
    !m_liveStartupRecoveryInProgress &&
    m_captureState != CaptureState::Live) {
```

这样内部重试不会被识别为新的用户手动启动。

---

## 19. 修改 `retryFailedLiveStartup()`

找到调用 `onStartCapture()` 的部分，替换为：

```cpp
const bool previousAutoCommandState =
    m_autoAcquisitionCommandInProgress;

const bool previousInternalRetryState =
    m_internalLiveStartupRetry;

m_autoAcquisitionCommandInProgress =
    automatic;

m_internalLiveStartupRetry = true;

m_liveStartupOrigin = startupOrigin;
m_liveStartupWindowId = startupWindowId;

onStartCapture();

m_internalLiveStartupRetry =
    previousInternalRetryState;

m_autoAcquisitionCommandInProgress =
    previousAutoCommandState;
```

禁止在内部重试前执行：

```cpp
m_liveStartupRetryCount = 0;
```

预期次数：

```text
失败 → 1/3
失败 → 2/3
失败 → 3/3
再次失败 → 停止立即重试
```

---

# 第十部分：状态清理

## 20. 修改 `resetLiveStartupRecoveryState()`

在现有函数中增加：

```cpp
m_hardwareTriggerStartupStage =
    HardwareTriggerStartupStage::None;

for (int cameraIndex = 0;
     cameraIndex < 2;
     ++cameraIndex) {
    m_hardwareTriggerStageBaselineFrameCount[cameraIndex] = 0;
    m_hardwareTriggerStageFrameSeen[cameraIndex] = false;
}

m_internalLiveStartupRetry = false;
```

保留已有：

```cpp
m_liveStartupConfirmed = false;
m_liveStartupRecoveryInProgress = false;
m_pulseBoardResponseTimedOut = false;
m_lastPulseBoardTimeoutStatusMs = -1;
```

---

## 21. 修改 `stopLiveCapture()`

增加：

```cpp
m_hardwareTriggerStartupStage =
    HardwareTriggerStartupStage::None;

for (int cameraIndex = 0;
     cameraIndex < 2;
     ++cameraIndex) {
    m_hardwareTriggerStageFrameSeen[cameraIndex] = false;
}
```

不要在 `stopLiveCapture()` 清零：

```cpp
m_liveStartupRetryCount
m_liveStartupWindowId
```

失败恢复还需要这两个值。

---

## 22. 修改 `handleHardwareTriggerStartupFailure()`

在函数开始位置增加：

```cpp
m_hardwareTriggerStartupStage =
    HardwareTriggerStartupStage::None;

for (int cameraIndex = 0;
     cameraIndex < 2;
     ++cameraIndex) {
    m_hardwareTriggerStageFrameSeen[cameraIndex] = false;
}
```

不要清零 `m_liveStartupRetryCount`。

---

# 第十一部分：不要破坏已修复的自动采集逻辑

## 23. 保持 Live 优先判断

`evaluateAutoAcquisitionSchedule()` 中必须继续保持：

```cpp
if (m_captureState == CaptureState::Live) {
    return;
}
```

位于：

```cpp
if (m_autoAcquisitionSuppressedWindowId ==
    window.windowId)
```

之前。

不要改回旧顺序。

---

## 24. 自动采集成功只能在 ROI 确认后显示

自动启动刚发起时可以显示：

```text
自动采集启动流程已发起，等待全画幅和 ROI 高频触发确认
```

真正的：

```text
自动采集启动成功
```

只能由 `confirmHardwareTriggerStartupIfReady()` 的 ROI 高频成功分支显示。

---

# 第十二部分：静态检查清单

完成后只做文本检查，不构建。

## 25. 检查新增符号

确认存在：

```cpp
HardwareTriggerStartupStage
m_hardwareTriggerStartupStage
m_hardwareTriggerStageBaselineFrameCount
m_hardwareTriggerStageFrameSeen
m_internalLiveStartupRetry
kRoiTrackingFirstFrameTimeoutMs
beginHardwareTriggerStartupStage
recordHardwareTriggerStartupFrame
```

---

## 26. 检查启动成功赋值

全局搜索：

```cpp
m_liveStartupConfirmed = true;
```

理想情况下只存在于：

```text
confirmHardwareTriggerStartupIfReady()
→ WaitingRoiTrackingPair 成功分支
```

全画幅阶段禁止设置为 `true`。

---

## 27. 检查 timeout 设置路径

搜索：

```cpp
m_pulseBoardResponseTimedOut = true;
```

允许存在于：

- 初始全画幅脉冲 ACK 超时；
- 全画幅重定位脉冲 ACK 超时；
- ROI 高频切换 ACK 超时。

每条路径都必须随后进入：

```cpp
beginHardwareTriggerStartupStage(...)
```

---

## 28. 检查逐帧 timeout 提示

`handleLiveFramePacket()` 中不应再存在：

```cpp
if (m_pulseBoardResponseTimedOut) {
    setPulseBoardResponseTimeoutStatus(...);
}
```

---

## 29. 检查 ROI 阶段

`commitPairedInitialRoisIfReady()` 必须调用：

```cpp
beginHardwareTriggerStartupStage(
    HardwareTriggerStartupStage::WaitingRoiTrackingPair);
```

并且不能直接设置：

```cpp
m_liveStartupConfirmed = true;
```

---

## 30. 检查图像尺寸判定

`recordHardwareTriggerStartupFrame()` 必须满足：

```text
WaitingFullFramePair：
    只接受非 64×64 图像

WaitingRoiTrackingPair：
    只接受 64×64 或更小图像
```

---

## 31. 检查内部重试

`onStartCapture()` 的新手动启动判断必须包含：

```cpp
!m_internalLiveStartupRetry
```

`retryFailedLiveStartup()` 调用 `onStartCapture()` 前必须设置：

```cpp
m_internalLiveStartupRetry = true;
```

调用后恢复旧值。

---

## 32. 检查独立定时器

以下两个阶段都必须通过 `beginHardwareTriggerStartupStage()` 启动：

```text
WaitingFullFramePair
WaitingRoiTrackingPair
```

不能依赖只有收到图像才会运行的重定位看门狗。

---

# 第十三部分：预期结果

## 33. ROI 高频 ACK 超时但图像正常

```text
ROI 写入
→ 高频命令 ACK 超时
→ 等待两台相机新的 ROI 图像
→ 两台均收到
→ 清除 timeout
→ Running
```

---

## 34. ROI 高频切换后完全没有图像

```text
WaitingRoiTrackingPair
→ 3 秒定时器到期
→ handleHardwareTriggerStartupFailure()
→ 停止脉冲
→ 停止相机
→ Idle
→ 3 秒后自动重试
```

---

## 35. 手动模式重试

```text
第 1 次自动重试
第 2 次自动重试
第 3 次自动重试
再次失败后停止
```

不能反复显示第 1/3 次。

---

# 第十四部分：最终回复格式

执行完成后只回复：

```text
已完成硬件触发分阶段确认与自动采集自恢复二次修复。

修改文件：
- src1分钟窗口/DIMM.h
- src1分钟窗口/DIMM.cpp
- src1分钟窗口/DIMM.CommCamera.cpp
- src1分钟窗口/DIMM.LiveRoi.cpp

未构建，未运行测试。
```

不要声称编译成功或测试通过。

---

# 第十五部分：执行顺序

严格按照：

```text
1. 修改 DIMM.h 枚举
2. 修改 DIMM.h 函数声明
3. 修改 DIMM.h 成员字段和常量
4. 实现 beginHardwareTriggerStartupStage
5. 实现 recordHardwareTriggerStartupFrame
6. 修改 scheduleHardwareTriggerStartupCheck
7. 修改 handleLiveFramePacket
8. 重写 confirmHardwareTriggerStartupIfReady
9. 重写 checkHardwareTriggerStartup
10. 修改 onStartCapture 的全画幅阶段
11. 修改 commitPairedInitialRoisIfReady
12. 修改 applyLiveFullFrameForRelocalization
13. 修改 onStartCapture 的内部重试判断
14. 修改 retryFailedLiveStartup
15. 修改 resetLiveStartupRecoveryState
16. 修改 stopLiveCapture
17. 修改 handleHardwareTriggerStartupFailure
18. 静态搜索检查
19. 不构建
```
