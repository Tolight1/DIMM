# DIMM 自动采集窗口重置与硬件触发启动自恢复——详细执行任务

> 目标仓库：`Tolight1/DIMM`  
> 修改目录：`src1分钟窗口/`  
> 任务性质：代码修改任务  
> 执行要求：**只修改代码，不构建、不运行、不执行测试、不提交 Git**  
> 适用对象：能力较弱、上下文理解有限的执行 Agent  
> 原则：严格按本文步骤执行，不自行扩大修改范围，不重构无关模块

---

## 1. 任务目标

本任务同时修复以下两个自动采集问题。

### 问题 1：修改自动采集时间后，旧的“手动停止”状态仍然生效

复现场景：

1. 自动采集计划设为 08:00 开始；
2. 08:00 自动启动；
3. 08:10 用户手动停止；
4. UI 显示：
   `自动采集本窗口已被手动停止，等待下一观测窗口`
5. 用户重新设置自动采集开始时间为 08:20；
6. 到 08:20 时，系统仍然认为当前窗口已被手动停止，不再启动。

根因：

- `AutoAcquisitionScheduler.cpp` 中的窗口 ID 只包含起止日期，不包含具体时分秒；
- 同一天修改开始时间后，旧窗口和新窗口得到同一个 `windowId`；
- `m_autoAcquisitionSuppressedWindowId` 因此继续匹配；
- 配置回调目前只在“由关闭切换为开启”时清除抑制状态，单纯修改时间不会清除。

修复目标：

- 自动采集窗口 ID 必须唯一表示完整的起止时间；
- 只要自动采集计划发生变化，旧的“本窗口手动停止”状态必须失效；
- 修改计划后允许新窗口在设定时间正常启动。

---

### 问题 2：硬件触发启动失败后，系统停留在假 Live 状态

可能出现的状态：

- 脉冲板串口应答超时；
- 两台相机在启动后规定时间内均未收到首帧；
- 只有一台相机收到首帧；
- UI 显示硬件触发首帧超时；
- `CaptureState` 仍为 `Live`；
- 自动采集调度器看到 `Live` 后直接返回，不会再次尝试；
- 系统必须人工停止并重新点击开始，才可能恢复。

修复目标：

1. 将“进入 `Live`”和“启动确认成功”区分开；
2. 硬件触发模式下，必须收到两台相机首帧后才视为启动成功；
3. 首帧超时后自动执行：
   - 停止脉冲板；
   - 停止双相机；
   - 清理当前采集状态；
   - 返回 `Idle`；
   - 延迟后自动重试；
4. 自动重试不能被误判为“用户手动停止”；
5. 立即重试最多 3 次；
6. 自动采集模式下，3 次立即重试失败后保持 `Idle`，由现有自动采集调度器稍后重新尝试；
7. 用户手动停止时必须取消尚未执行的自动重试。

---

## 2. 修改范围

只允许修改以下文件：

```text
src1分钟窗口/AutoAcquisitionScheduler.cpp
src1分钟窗口/DIMM.h
src1分钟窗口/DIMM.cpp
src1分钟窗口/DIMM.Config.cpp
src1分钟窗口/DIMM.CommCamera.cpp
```

除非编译器以后明确指出声明位置不符，否则不要修改其他文件。

---

## 3. 禁止事项

执行本任务时禁止：

- 不要构建；
- 不要运行程序；
- 不要执行单元测试；
- 不要添加日志或 `qDebug()` 诊断输出；
- 不要修改 UI 文件；
- 不要修改网络协议；
- 不要修改 CSV 格式；
- 不要修改相干时间相关代码；
- 不要重构相机管理器；
- 不要修改脉冲板通信协议；
- 不要提交 Git；
- 不要创建新分支；
- 不要改动 `src82/` 或其他历史目录。

---

# 第一部分：修复自动采集窗口 ID 与旧抑制状态

## 4. 修改 `AutoAcquisitionScheduler.cpp`

文件：

```text
src1分钟窗口/AutoAcquisitionScheduler.cpp
```

### 4.1 替换窗口 ID 生成函数

找到当前函数：

```cpp
QString windowIdForDates(const QDateTime& start, const QDateTime& stop)
{
    return QStringLiteral("%1/%2")
        .arg(start.date().toString(Qt::ISODate), stop.date().toString(Qt::ISODate));
}
```

将整个函数替换为：

```cpp
QString windowIdForRange(const QDateTime& start, const QDateTime& stop)
{
    return QStringLiteral("%1/%2")
        .arg(start.toMSecsSinceEpoch())
        .arg(stop.toMSecsSinceEpoch());
}
```

说明：

- 新 ID 包含完整起止时间；
- 开始时间、停止时间任意一项变化，窗口 ID 都会变化；
- 使用毫秒时间戳，避免字符串格式和本地化问题。

---

### 4.2 替换所有调用点

在该文件中搜索：

```cpp
windowIdForDates
```

应存在两处调用。

将所有：

```cpp
window.windowId = windowIdForDates(start, stop);
```

替换为：

```cpp
window.windowId = windowIdForRange(start, stop);
```

同时将太阳窗口中的类似调用：

```cpp
window.windowId = windowIdForDates(window.start, window.stop);
```

替换为：

```cpp
window.windowId = windowIdForRange(window.start, window.stop);
```

完成后，该文件中不应再存在：

```text
windowIdForDates
```

---

## 5. 修改 `DIMM.Config.cpp`

文件：

```text
src1分钟窗口/DIMM.Config.cpp
```

目标函数：

```cpp
void DIMM::setupAutoAcquisitionSettingsCallbacks()
```

### 5.1 替换 `onApplyAutoAcquisition` 回调开头逻辑

找到当前代码：

```cpp
m_settingsDialog->onApplyAutoAcquisition = [this](const AutoAcquisitionConfig& config) {
    const bool wasEnabled = m_autoAcquisitionConfig.enabled;
    m_autoAcquisitionConfig = config;
    if (!wasEnabled && config.enabled) {
        m_autoAcquisitionSuppressedWindowId.clear();
    }
    const AutoAcquisitionWindow window =
        AutoAcquisitionScheduler::resolveWindow(m_autoAcquisitionConfig, QDateTime::currentDateTime());
```

将上述部分替换为：

```cpp
m_settingsDialog->onApplyAutoAcquisition = [this](const AutoAcquisitionConfig& config) {
    const QDateTime now = QDateTime::currentDateTime();

    const bool wasEnabled = m_autoAcquisitionConfig.enabled;

    const AutoAcquisitionWindow oldWindow =
        AutoAcquisitionScheduler::resolveWindow(
            m_autoAcquisitionConfig,
            now);

    m_autoAcquisitionConfig = config;

    const AutoAcquisitionWindow newWindow =
        AutoAcquisitionScheduler::resolveWindow(
            m_autoAcquisitionConfig,
            now);

    const bool scheduleChanged =
        oldWindow.valid != newWindow.valid ||
        (oldWindow.valid &&
         newWindow.valid &&
         oldWindow.windowId != newWindow.windowId);

    if ((!wasEnabled && config.enabled) || scheduleChanged) {
        m_autoAcquisitionSuppressedWindowId.clear();
        m_lastAutoAcquisitionAttemptMs = -1;
        m_lastAutoAcquisitionStatusKey.clear();
        m_lastAutoAcquisitionStatusMs = -1;

        if (!m_autoAcquisitionStartedCurrentRun) {
            m_autoAcquisitionActiveWindowId.clear();
        }
    }

    const AutoAcquisitionWindow window = newWindow;
```

后面的“下次开始”“下次停止”和状态消息代码保持原样。

---

### 5.2 配置变更时取消尚未执行的启动重试

在上面的：

```cpp
if ((!wasEnabled && config.enabled) || scheduleChanged) {
```

代码块内部，再加入：

```cpp
if (m_liveStartupRetryTimer) {
    m_liveStartupRetryTimer->stop();
}

m_liveStartupRecoveryInProgress = false;
m_liveStartupRetryCount = 0;
m_liveStartupWindowId.clear();
```

最终该代码块应类似：

```cpp
if ((!wasEnabled && config.enabled) || scheduleChanged) {
    m_autoAcquisitionSuppressedWindowId.clear();
    m_lastAutoAcquisitionAttemptMs = -1;
    m_lastAutoAcquisitionStatusKey.clear();
    m_lastAutoAcquisitionStatusMs = -1;

    if (!m_autoAcquisitionStartedCurrentRun) {
        m_autoAcquisitionActiveWindowId.clear();
    }

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->stop();
    }

    m_liveStartupRecoveryInProgress = false;
    m_liveStartupRetryCount = 0;
    m_liveStartupWindowId.clear();
}
```

原因：

- 用户修改计划后，旧窗口对应的延迟重试不应继续执行；
- 新计划应从全新的启动状态开始。

---

# 第二部分：增加硬件触发启动确认与自动恢复状态

## 6. 修改 `DIMM.h`

文件：

```text
src1分钟窗口/DIMM.h
```

---

## 6.1 增加启动来源枚举

在 `DIMM` 类内部，与现有：

```cpp
enum class CaptureState
enum class DetailViewMode
enum class LiveStartupPhase
```

相邻的位置增加：

```cpp
enum class LiveStartupOrigin {
    Manual,
    AutoAcquisition
};
```

不要放到类外。

---

## 6.2 增加私有函数声明

在 `private:` 的函数声明区域，找到：

```cpp
void scheduleHardwareTriggerStartupCheck();
void checkHardwareTriggerStartup();
```

如果 `scheduleHardwareTriggerStartupCheck()` 和 `checkHardwareTriggerStartup()` 声明位置不相邻，也不要移动原声明。

在附近增加以下声明：

```cpp
void confirmHardwareTriggerStartupIfReady();
void handleHardwareTriggerStartupFailure(const QString& detail);
bool shouldRetryFailedLiveStartup() const;
void retryFailedLiveStartup();
void resetLiveStartupRecoveryState(bool resetRetryCount);
```

其中：

- `confirmHardwareTriggerStartupIfReady()`：双相机首帧齐全后确认成功；
- `handleHardwareTriggerStartupFailure()`：统一处理首帧超时和重启；
- `shouldRetryFailedLiveStartup()`：判断是否仍允许重试；
- `retryFailedLiveStartup()`：执行延迟重启；
- `resetLiveStartupRecoveryState()`：统一清理启动恢复字段。

---

## 6.3 增加成员字段

找到自动采集字段：

```cpp
AutoAcquisitionConfig m_autoAcquisitionConfig;
bool m_autoAcquisitionCommandInProgress = false;
bool m_autoAcquisitionStartedCurrentRun = false;
QString m_autoAcquisitionActiveWindowId;
QString m_autoAcquisitionSuppressedWindowId;
qint64 m_lastAutoAcquisitionAttemptMs = -1;
QString m_lastAutoAcquisitionStatusKey;
qint64 m_lastAutoAcquisitionStatusMs = -1;
```

在这组字段后增加：

```cpp
LiveStartupOrigin m_liveStartupOrigin =
    LiveStartupOrigin::Manual;

bool m_liveStartupConfirmed = false;
bool m_liveStartupRecoveryInProgress = false;
bool m_pulseBoardResponseTimedOut = false;

int m_liveStartupRetryCount = 0;
QString m_liveStartupWindowId;

QTimer* m_liveStartupRetryTimer = nullptr;
```

---

## 6.4 增加常量

在 `DIMM.h` 中查找现有 `static constexpr` 常量区域。

增加：

```cpp
static constexpr int kHardwareTriggerFirstFrameTimeoutMs = 5000;
static constexpr int kLiveStartupRetryDelayMs = 3000;
static constexpr int kLiveStartupMaxImmediateRetries = 3;
```

说明：

- 首帧检查由原 2.5 秒改为 5 秒；
- 每次自动重启前等待 3 秒；
- 最多执行 3 次立即重试。

如果该类的常量统一使用 `qint64`，可以保持 `int`，无需改成 `qint64`。

---

# 第三部分：初始化和销毁重试定时器

## 7. 修改 `DIMM.cpp` 的 `setupRuntimeTimers()`

文件：

```text
src1分钟窗口/DIMM.cpp
```

找到：

```cpp
void DIMM::setupRuntimeTimers()
```

在现有 `m_hardwareTriggerStartupTimer` 初始化代码后增加：

```cpp
m_liveStartupRetryTimer = new QTimer(this);
m_liveStartupRetryTimer->setSingleShot(true);

connect(
    m_liveStartupRetryTimer,
    &QTimer::timeout,
    this,
    [this]() {
        retryFailedLiveStartup();
    });
```

不要使用 `QTimer::singleShot()` 替代成员定时器，因为必须支持手动停止时取消重试。

---

## 8. 修改析构函数

文件：

```text
src1分钟窗口/DIMM.cpp
```

目标函数：

```cpp
DIMM::~DIMM()
```

在已有定时器停止逻辑中增加：

```cpp
if (m_hardwareTriggerStartupTimer) {
    m_hardwareTriggerStartupTimer->stop();
}

if (m_liveStartupRetryTimer) {
    m_liveStartupRetryTimer->stop();
}
```

如果 `m_hardwareTriggerStartupTimer` 已经停止，则只增加 `m_liveStartupRetryTimer`。

---

# 第四部分：增加统一启动状态清理函数

## 9. 在 `DIMM.cpp` 中实现 `resetLiveStartupRecoveryState`

文件：

```text
src1分钟窗口/DIMM.cpp
```

建议放置位置：

- `stopLiveCapture()` 附近；
- 或自动采集相关函数附近。

增加：

```cpp
void DIMM::resetLiveStartupRecoveryState(bool resetRetryCount)
{
    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->stop();
    }

    m_liveStartupConfirmed = false;
    m_liveStartupRecoveryInProgress = false;
    m_pulseBoardResponseTimedOut = false;

    if (resetRetryCount) {
        m_liveStartupRetryCount = 0;
        m_liveStartupWindowId.clear();
    }
}
```

注意：

- 自动重试流程内部调用时，有时不能清零重试次数；
- 因此函数带 `resetRetryCount` 参数；
- 用户手动停止和新启动时传 `true`；
- 单次失败清理时不要直接调用该函数，避免把重试次数清零。

---

# 第五部分：调整启动入口和自动采集入口

## 10. 修改 `evaluateAutoAcquisitionSchedule()`

文件：

```text
src1分钟窗口/DIMM.cpp
```

目标函数：

```cpp
void DIMM::evaluateAutoAcquisitionSchedule()
```

---

## 10.1 当前窗口变更时清除旧抑制 ID

在成功解析 `window` 后、计算 `insideWindow` 前增加：

```cpp
if (!m_autoAcquisitionSuppressedWindowId.isEmpty() &&
    m_autoAcquisitionSuppressedWindowId != window.windowId) {
    m_autoAcquisitionSuppressedWindowId.clear();
}
```

这样即使窗口自然进入下一天，也不会永久保留旧抑制状态。

---

## 10.2 自动启动前记录启动来源

找到：

```cpp
m_autoAcquisitionCommandInProgress = true;
onStartCapture();
m_autoAcquisitionCommandInProgress = false;
```

替换为：

```cpp
m_liveStartupOrigin =
    LiveStartupOrigin::AutoAcquisition;

m_liveStartupWindowId =
    window.windowId;

m_liveStartupRetryCount = 0;
m_liveStartupConfirmed = false;
m_liveStartupRecoveryInProgress = false;
m_pulseBoardResponseTimedOut = false;

m_autoAcquisitionCommandInProgress = true;
onStartCapture();
m_autoAcquisitionCommandInProgress = false;
```

---

## 10.3 不要过早显示“已成功启动”

找到：

```cpp
if (m_captureState == CaptureState::Live) {
    m_autoAcquisitionStartedCurrentRun = true;
    m_autoAcquisitionActiveWindowId = window.windowId;
    setAutoAcquisitionStatus(QStringLiteral("自动采集已按计划启动"),
                             UiStatusLevel::Success,
                             QStringLiteral("auto-start"));
}
```

替换为：

```cpp
if (m_captureState == CaptureState::Live) {
    m_autoAcquisitionStartedCurrentRun = true;
    m_autoAcquisitionActiveWindowId = window.windowId;

    if (m_configTriggerMode == 0) {
        setAutoAcquisitionStatus(
            QStringLiteral("自动采集已按计划启动"),
            UiStatusLevel::Success,
            QStringLiteral("auto-start"));
    } else {
        setAutoAcquisitionStatus(
            QStringLiteral("自动采集启动流程已发起，等待双相机首帧确认"),
            UiStatusLevel::Warning,
            QStringLiteral("auto-start-pending"));
    }
}
```

连续采集模式无需首帧重试状态机，因此仍可立即显示成功。

---

## 11. 修改 `onStartCapture()`

文件：

```text
src1分钟窗口/DIMM.cpp
```

目标函数：

```cpp
void DIMM::onStartCapture()
```

### 11.1 手动开始时设置启动来源

在函数开头，处理对准模式之前，增加：

```cpp
if (!m_autoAcquisitionCommandInProgress &&
    !m_liveStartupRecoveryInProgress &&
    m_captureState != CaptureState::Live) {
    m_liveStartupOrigin =
        LiveStartupOrigin::Manual;

    m_liveStartupWindowId.clear();
    m_liveStartupRetryCount = 0;
    m_liveStartupConfirmed = false;
    m_pulseBoardResponseTimedOut = false;

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->stop();
    }
}
```

注意：

- 自动重试调用 `onStartCapture()` 时，`m_liveStartupRecoveryInProgress` 可能已经被清回 `false`；
- 因此自动重试函数调用前必须设置 `m_autoAcquisitionCommandInProgress`，或者在后文按要求保留启动来源；
- 本文后面的 `retryFailedLiveStartup()` 会处理。

---

### 11.2 新一轮启动时清理确认状态，但不要错误清零重试次数

在：

```cpp
closeResultFile();
resetMeasurementState();
```

之前增加：

```cpp
m_liveStartupConfirmed = false;
m_pulseBoardResponseTimedOut = false;

if (m_hardwareTriggerStartupTimer) {
    m_hardwareTriggerStartupTimer->stop();
}
```

不要在这里统一执行：

```cpp
m_liveStartupRetryCount = 0;
```

因为自动重试期间必须保留当前重试次数。

---

### 11.3 脉冲板应答超时时记录状态

找到硬件触发启动分支：

```cpp
if (pulseResponseTimeout) {
    setStatusMessage(...);
    scheduleHardwareTriggerStartupCheck();
    return;
}
```

替换为：

```cpp
if (pulseResponseTimeout) {
    m_pulseBoardResponseTimedOut = true;

    setStatusMessage(
        QStringLiteral(
            "状态: 脉冲板应答超时，但已继续等待首帧确认硬件触发是否生效"),
        UiStatusLevel::Warning);

    scheduleHardwareTriggerStartupCheck();
    return;
}
```

不要立即判定失败，因为脉冲可能已经实际输出。

---

### 11.4 非超时的脉冲启动失败保持现有行为

以下分支继续保留：

```cpp
m_cameraManager->stopAll();
updateCaptureState(CaptureState::Idle);
...
return;
```

但在进入 `Idle` 前增加：

```cpp
m_liveStartupConfirmed = false;
m_pulseBoardResponseTimedOut = false;
```

这类错误不是“等待首帧后判断”的故障，不强制进入本任务的自动首帧恢复流程。

---

# 第六部分：首帧确认和启动失败恢复

## 12. 修改 `DIMM.CommCamera.cpp`

文件：

```text
src1分钟窗口/DIMM.CommCamera.cpp
```

---

## 12.1 修改首帧检查定时器时间

目标函数：

```cpp
void DIMM::scheduleHardwareTriggerStartupCheck()
```

找到：

```cpp
m_hardwareTriggerStartupTimer->start(2500);
```

替换为：

```cpp
m_hardwareTriggerStartupTimer->start(
    kHardwareTriggerFirstFrameTimeoutMs);
```

---

## 12.2 收到帧后检查双相机是否都已准备

目标函数：

```cpp
void DIMM::handleLiveFramePacket(
    int cameraIndex,
    const CameraFrame& packet)
```

找到双相机帧计数更新部分：

```cpp
if (cameraIndex >= 0 && cameraIndex < 2) {
    ...
    ++runtime.frameCountPerCamera[cameraIndex];
}

++runtime.frameCount;
```

在：

```cpp
++runtime.frameCount;
```

之后增加：

```cpp
if (m_configTriggerMode != 0) {
    confirmHardwareTriggerStartupIfReady();
}
```

不要放在 `frame.empty()` 检查之前。

---

## 12.3 实现 `confirmHardwareTriggerStartupIfReady()`

在 `scheduleHardwareTriggerStartupCheck()` 前或后增加：

```cpp
void DIMM::confirmHardwareTriggerStartupIfReady()
{
    if (m_captureState != CaptureState::Live ||
        m_configTriggerMode == 0 ||
        m_liveStartupConfirmed) {
        return;
    }

    const auto& runtime = activeRuntime();

    const bool bothReady =
        runtime.frameCountPerCamera[0] > 0 &&
        runtime.frameCountPerCamera[1] > 0;

    if (!bothReady) {
        return;
    }

    m_liveStartupConfirmed = true;
    m_liveStartupRecoveryInProgress = false;
    m_liveStartupRetryCount = 0;

    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->stop();
    }

    if (m_liveStartupOrigin ==
        LiveStartupOrigin::AutoAcquisition) {
        setAutoAcquisitionStatus(
            QStringLiteral(
                "自动采集启动成功，双相机首帧已确认"),
            UiStatusLevel::Success,
            QStringLiteral(
                "auto-start-confirmed"));
    }

    if (m_pulseBoardResponseTimedOut) {
        setStatusMessage(
            QStringLiteral(
                "状态: 脉冲板未返回串口应答，但双相机已收到硬件触发首帧，继续采集"),
            UiStatusLevel::Warning);
    } else {
        setStatusMessage(
            QStringLiteral(
                "状态: 双相机硬件触发首帧确认成功"),
            UiStatusLevel::Success);
    }

    m_pulseBoardResponseTimedOut = false;
}
```

说明：

- 两台相机都收到首帧后，启动视为成功；
- 即使脉冲板无串口 ACK，只要双相机首帧正常，仍继续采集；
- 成功后重试计数清零。

---

## 12.4 替换 `checkHardwareTriggerStartup()`

找到整个函数：

```cpp
void DIMM::checkHardwareTriggerStartup()
```

将整个函数替换为：

```cpp
void DIMM::checkHardwareTriggerStartup()
{
    if (m_captureState != CaptureState::Live ||
        m_configTriggerMode == 0 ||
        m_liveStartupConfirmed) {
        return;
    }

    const auto& runtime = activeRuntime();

    const bool cam1Ready =
        runtime.frameCountPerCamera[0] > 0;

    const bool cam2Ready =
        runtime.frameCountPerCamera[1] > 0;

    if (cam1Ready && cam2Ready) {
        confirmHardwareTriggerStartupIfReady();
        return;
    }

    QString detail;

    if (!cam1Ready && !cam2Ready) {
        detail =
            QStringLiteral(
                "两台相机在启动超时时间内均未收到首帧");
    } else if (!cam1Ready) {
        detail =
            QStringLiteral(
                "只有相机2收到首帧，相机1未触发");
    } else {
        detail =
            QStringLiteral(
                "只有相机1收到首帧，相机2未触发");
    }

    handleHardwareTriggerStartupFailure(detail);
}
```

不再只显示警告后留在 `Live`。

---

# 第七部分：实现失败清理和自动重试

## 13. 在 `DIMM.cpp` 中实现 `shouldRetryFailedLiveStartup()`

建议放在自动采集函数附近。

增加：

```cpp
bool DIMM::shouldRetryFailedLiveStartup() const
{
    if (m_liveStartupRetryCount >=
        kLiveStartupMaxImmediateRetries) {
        return false;
    }

    if (m_liveStartupOrigin ==
        LiveStartupOrigin::Manual) {
        return true;
    }

    if (!m_autoAcquisitionConfig.enabled) {
        return false;
    }

    const QDateTime now =
        QDateTime::currentDateTime();

    const AutoAcquisitionWindow window =
        AutoAcquisitionScheduler::resolveWindow(
            m_autoAcquisitionConfig,
            now);

    if (!window.valid ||
        !AutoAcquisitionScheduler::contains(
            window,
            now)) {
        return false;
    }

    if (window.windowId !=
        m_liveStartupWindowId) {
        return false;
    }

    if (m_autoAcquisitionSuppressedWindowId ==
        window.windowId) {
        return false;
    }

    return true;
}
```

---

## 14. 实现 `handleHardwareTriggerStartupFailure()`

在 `DIMM.cpp` 中增加：

```cpp
void DIMM::handleHardwareTriggerStartupFailure(
    const QString& detail)
{
    if (m_liveStartupRecoveryInProgress) {
        return;
    }

    m_liveStartupRecoveryInProgress = true;
    m_liveStartupConfirmed = false;
    m_pulseBoardResponseTimedOut = false;

    if (m_hardwareTriggerStartupTimer) {
        m_hardwareTriggerStartupTimer->stop();
    }

    const bool previousCommandState =
        m_autoAcquisitionCommandInProgress;

    /*
     * 内部故障恢复不属于用户手动停止。
     * 临时置为 true，阻止
     * noteManualAutoAcquisitionStopIfNeeded()
     * 写入 suppressedWindowId。
     */
    m_autoAcquisitionCommandInProgress = true;

    stopLiveCapture();

    m_reporting = false;
    if (m_reportTimer) {
        m_reportTimer->stop();
    }

    closeResultFile();
    updateCaptureState(CaptureState::Idle);
    resetMeasurementState();

    m_autoAcquisitionCommandInProgress =
        previousCommandState;

    const bool retryAllowed =
        shouldRetryFailedLiveStartup();

    if (!retryAllowed) {
        m_liveStartupRecoveryInProgress = false;

        if (m_liveStartupOrigin ==
            LiveStartupOrigin::AutoAcquisition) {
            /*
             * 允许现有 1 分钟调度器稍后再次尝试。
             */
            m_autoAcquisitionStartedCurrentRun = false;
            m_autoAcquisitionActiveWindowId.clear();
            m_lastAutoAcquisitionAttemptMs =
                QDateTime::currentMSecsSinceEpoch();

            setAutoAcquisitionStatus(
                QStringLiteral(
                    "自动采集硬件触发连续启动失败，已安全停止，稍后重新尝试"),
                UiStatusLevel::Error,
                QStringLiteral(
                    "auto-start-retry-exhausted"));
        } else {
            setStatusMessage(
                QStringLiteral(
                    "硬件触发启动失败，已安全停止: %1")
                    .arg(detail),
                UiStatusLevel::Error);
        }

        return;
    }

    ++m_liveStartupRetryCount;

    setStatusMessage(
        QStringLiteral(
            "硬件触发启动失败，已自动停止；"
            "%1 秒后进行第 %2/%3 次重试。原因: %4")
            .arg(kLiveStartupRetryDelayMs / 1000)
            .arg(m_liveStartupRetryCount)
            .arg(kLiveStartupMaxImmediateRetries)
            .arg(detail),
        UiStatusLevel::Warning);

    if (m_liveStartupRetryTimer) {
        m_liveStartupRetryTimer->start(
            kLiveStartupRetryDelayMs);
    } else {
        m_liveStartupRecoveryInProgress = false;
    }
}
```

重要说明：

- 不调用 `onStopCapture()`；
- 直接调用内部清理函数，防止记录成用户手动停止；
- 自动采集模式重试耗尽后清除 `m_autoAcquisitionStartedCurrentRun`；
- 保留 `m_autoAcquisitionSuppressedWindowId` 不变，不写入当前窗口；
- 一分钟后，现有调度器可以再次发起新一轮尝试。

---

## 15. 实现 `retryFailedLiveStartup()`

在 `DIMM.cpp` 中增加：

```cpp
void DIMM::retryFailedLiveStartup()
{
    if (!m_liveStartupRecoveryInProgress) {
        return;
    }

    if (!shouldRetryFailedLiveStartup()) {
        m_liveStartupRecoveryInProgress = false;

        setStatusMessage(
            QStringLiteral(
                "硬件触发自动重试已取消"),
            UiStatusLevel::Warning);

        return;
    }

    const LiveStartupOrigin startupOrigin =
        m_liveStartupOrigin;

    const QString startupWindowId =
        m_liveStartupWindowId;

    m_liveStartupRecoveryInProgress = false;
    m_liveStartupConfirmed = false;
    m_pulseBoardResponseTimedOut = false;

    /*
     * 防止 onStartCapture() 把自动重试覆盖为手动启动。
     */
    const bool automatic =
        startupOrigin ==
        LiveStartupOrigin::AutoAcquisition;

    m_autoAcquisitionCommandInProgress =
        automatic;

    m_liveStartupOrigin = startupOrigin;
    m_liveStartupWindowId = startupWindowId;

    onStartCapture();

    m_autoAcquisitionCommandInProgress = false;

    /*
     * onStartCapture() 可能立即失败并回到 Idle。
     * 此时继续进入统一失败恢复。
     */
    if (m_captureState != CaptureState::Live) {
        handleHardwareTriggerStartupFailure(
            QStringLiteral(
                "重新启动采集流程失败"));
    }
}
```

注意：

- 不要重置 `m_liveStartupRetryCount`；
- 不要清空 `m_liveStartupWindowId`；
- 自动重试仍属于原来的自动采集窗口。

---

# 第八部分：用户手动停止时取消自动重试

## 16. 修改 `onStopCapture()`

文件：

```text
src1分钟窗口/DIMM.cpp
```

目标函数：

```cpp
void DIMM::onStopCapture()
```

在函数开头、对准模式判断之后加入：

```cpp
if (m_liveStartupRetryTimer) {
    m_liveStartupRetryTimer->stop();
}

m_liveStartupRecoveryInProgress = false;
m_liveStartupRetryCount = 0;
m_liveStartupConfirmed = false;
m_pulseBoardResponseTimedOut = false;
m_liveStartupWindowId.clear();
```

然后保留现有：

```cpp
noteManualAutoAcquisitionStopIfNeeded();
```

效果：

- 用户手动点击停止时，取消所有尚未执行的内部重启；
- 如果当前确实是自动采集运行，则继续按现有逻辑记录“本窗口手动停止”；
- 内部故障恢复不会调用该函数，因此不会产生误判。

---

## 17. 修改 `onStartCapture()` 中“Live 时点击开始即暂停”的分支

当前可能存在：

```cpp
if (m_captureState == CaptureState::Live) {
    noteManualAutoAcquisitionStopIfNeeded();
    stopLiveCapture();
    updateCaptureState(CaptureState::Paused);
    ...
    return;
}
```

在该分支调用 `noteManualAutoAcquisitionStopIfNeeded()` 前增加：

```cpp
if (m_liveStartupRetryTimer) {
    m_liveStartupRetryTimer->stop();
}

m_liveStartupRecoveryInProgress = false;
m_liveStartupRetryCount = 0;
m_liveStartupConfirmed = false;
m_pulseBoardResponseTimedOut = false;
m_liveStartupWindowId.clear();
```

这是用户通过“再次点击开始按钮”执行暂停，也应取消自动重试。

---

# 第九部分：停止函数与断开相机的补充处理

## 18. 修改 `stopLiveCapture()`

文件：

```text
src1分钟窗口/DIMM.cpp
```

目标函数：

```cpp
bool DIMM::stopLiveCapture()
```

函数已有：

```cpp
if (m_hardwareTriggerStartupTimer) {
    m_hardwareTriggerStartupTimer->stop();
}
```

在后面增加：

```cpp
m_liveStartupConfirmed = false;
m_pulseBoardResponseTimedOut = false;
```

不要在这里清零：

```cpp
m_liveStartupRetryCount
m_liveStartupWindowId
```

原因：

- 故障恢复会调用 `stopLiveCapture()`；
- 如果在这里清零，就无法继续按原窗口和重试次数自动重启。

---

## 19. 修改相机断开处理

文件：

```text
src1分钟窗口/DIMM.CommCamera.cpp
```

目标函数：

```cpp
void DIMM::onCameraDisconnected(int index)
```

如果当前是硬件触发启动等待阶段，直接将状态置为 `Paused` 会造成自动采集卡住。

在函数中，现有状态处理前增加：

```cpp
if (m_captureState == CaptureState::Live &&
    m_configTriggerMode != 0 &&
    !m_liveStartupConfirmed) {
    handleHardwareTriggerStartupFailure(
        QStringLiteral(
            "硬件触发启动期间相机%1断开")
            .arg(index + 1));
    return;
}
```

其他已正常运行后的相机断开行为保持不变。

---

# 第十部分：防止自动采集调度器误认为失败启动仍在运行

## 20. 检查失败路径必须回到 `Idle`

以下所有失败流程最终必须满足：

```cpp
m_captureState == CaptureState::Idle
```

重点检查：

```text
handleHardwareTriggerStartupFailure()
```

必须调用：

```cpp
updateCaptureState(CaptureState::Idle);
```

不能改成 `Paused`。

原因：

- 自动采集调度器只会在非 `Live` 状态下重新尝试；
- `Idle` 是失败清理后的明确状态；
- `Paused` 容易与用户主动暂停混淆。

---

# 第十一部分：状态机预期行为

## 21. 手动启动成功

流程：

```text
用户点击开始
→ 启动相机
→ 启动脉冲板
→ 等待双相机首帧
→ 两台都有首帧
→ m_liveStartupConfirmed = true
→ 清零重试次数
→ 正常采集
```

---

## 22. 脉冲板无 ACK，但图像正常

流程：

```text
脉冲板串口应答超时
→ m_pulseBoardResponseTimedOut = true
→ 不立即停止
→ 等待首帧
→ 两台相机都收到首帧
→ 继续采集
→ UI 提示无串口 ACK，但硬件触发有效
```

---

## 23. 两台相机都没有首帧

流程：

```text
启动后 5 秒无首帧
→ handleHardwareTriggerStartupFailure()
→ 停止脉冲板
→ 停止相机
→ 清理数据和状态
→ 返回 Idle
→ 等待 3 秒
→ 自动重启
```

---

## 24. 只有一台相机收到首帧

流程与两台均无首帧相同：

```text
5 秒后检查失败
→ 安全停止
→ 延迟重启
```

---

## 25. 自动采集修改时间后的行为

示例：

```text
旧计划：08:00～10:00
08:10 用户手动停止
新计划：08:20～10:00
```

应用新计划后：

```text
旧 suppressedWindowId 被清除
新 windowId 与旧 windowId 不同
08:20 自动采集重新启动
```

---

## 26. 自动重试次数

一次启动失败后的立即重试次数：

```text
第 1 次失败 → 3 秒后重试 1/3
第 2 次失败 → 3 秒后重试 2/3
第 3 次失败 → 3 秒后重试 3/3
再次失败 → 停止立即重试
```

自动采集模式：

```text
保持 Idle
m_autoAcquisitionStartedCurrentRun = false
现有调度器约 60 秒后重新尝试
```

手动模式：

```text
保持 Idle
明确显示启动失败
不再自动无限循环
```

---

# 第十二部分：静态检查清单

完成修改后，只做文本检查，不构建。

## 27. 窗口 ID 检查

确认 `AutoAcquisitionScheduler.cpp` 中：

```text
windowIdForDates
```

不存在。

确认存在：

```text
windowIdForRange
```

并且两个窗口解析路径都使用新函数。

---

## 28. 声明与定义检查

确认 `DIMM.h` 声明了：

```cpp
confirmHardwareTriggerStartupIfReady
handleHardwareTriggerStartupFailure
shouldRetryFailedLiveStartup
retryFailedLiveStartup
resetLiveStartupRecoveryState
```

确认对应定义均存在。

如果 `resetLiveStartupRecoveryState()` 最终没有被调用，可以删除其声明和定义，不要保留未使用函数。  
优先建议在用户停止或新计划应用时使用该函数，以减少重复清理代码。

---

## 29. 定时器检查

确认：

```cpp
m_liveStartupRetryTimer
```

满足：

- 在 `DIMM.h` 中声明；
- 在 `setupRuntimeTimers()` 中创建；
- 设置为 single-shot；
- 连接到 `retryFailedLiveStartup()`；
- 析构时停止；
- 用户手动停止时停止；
- 修改自动采集计划时停止。

---

## 30. 手动停止抑制检查

搜索：

```cpp
noteManualAutoAcquisitionStopIfNeeded
```

确认：

- 用户操作路径仍会调用；
- `handleHardwareTriggerStartupFailure()` 不调用；
- `retryFailedLiveStartup()` 不调用；
- 内部恢复不会写入 `m_autoAcquisitionSuppressedWindowId`。

---

## 31. 假 Live 状态检查

确认 `checkHardwareTriggerStartup()` 失败后不再只有：

```cpp
setStatusMessage(...)
```

而是调用：

```cpp
handleHardwareTriggerStartupFailure(detail);
```

---

## 32. 重试计数检查

确认以下位置不会错误清零 `m_liveStartupRetryCount`：

```text
stopLiveCapture()
resetMeasurementState()
retryFailedLiveStartup() 开始前
```

只有以下情况清零：

- 新的手动启动；
- 新的自动采集窗口首次启动；
- 双相机首帧确认成功；
- 用户手动停止；
- 自动采集计划发生变化。

---

## 33. 自动采集重试耗尽检查

确认自动模式重试耗尽后：

```cpp
m_autoAcquisitionStartedCurrentRun = false;
m_autoAcquisitionActiveWindowId.clear();
```

否则调度器可能认为当前窗口已经由自动采集启动。

---

# 第十三部分：最终交付要求

执行完成后，仅返回以下内容：

```text
已完成自动采集窗口状态与硬件触发启动自恢复修改。

修改文件：
- src1分钟窗口/AutoAcquisitionScheduler.cpp
- src1分钟窗口/DIMM.h
- src1分钟窗口/DIMM.cpp
- src1分钟窗口/DIMM.Config.cpp
- src1分钟窗口/DIMM.CommCamera.cpp

未构建，未运行测试。
```

不要：

- 输出大段代码；
- 声称构建成功；
- 声称测试通过；
- 修改其他目录；
- 自动提交 Git。

---

# 第十四部分：执行顺序摘要

必须按以下顺序执行：

```text
1. 修改 AutoAcquisitionScheduler.cpp 的窗口 ID
2. 修改 DIMM.h，增加枚举、函数、字段、常量
3. 修改 setupRuntimeTimers() 创建重试定时器
4. 修改析构和手动停止路径
5. 修改自动采集配置回调
6. 修改 evaluateAutoAcquisitionSchedule()
7. 修改 onStartCapture()
8. 修改 handleLiveFramePacket()
9. 修改 scheduleHardwareTriggerStartupCheck()
10. 替换 checkHardwareTriggerStartup()
11. 实现首帧确认函数
12. 实现失败清理函数
13. 实现重试条件函数
14. 实现延迟重试函数
15. 补充相机断开启动失败处理
16. 进行静态搜索检查
17. 不构建
```
