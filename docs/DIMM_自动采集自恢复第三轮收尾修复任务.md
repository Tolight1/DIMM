# DIMM 自动采集自恢复第三轮收尾修复执行任务

> 目标仓库：`Tolight1/DIMM`  
> 目标目录：`src1分钟窗口/`  
> 执行要求：**只修改代码，不构建、不运行、不测试、不提交 Git**  
> 适用对象：性能较弱、上下文理解有限的执行 Agent  
> 修改性质：在第二轮分阶段确认代码基础上的收尾修复  
> 重要原则：不重写现有状态机，不撤销前两轮修改，不修改无关模块

---

# 1. 当前代码状态

当前版本已经完成以下主要改造：

```text
HardwareTriggerStartupStage::None
HardwareTriggerStartupStage::WaitingFullFramePair
HardwareTriggerStartupStage::WaitingRoiTrackingPair
HardwareTriggerStartupStage::Running
```

并且已经具备：

- 全画幅低频阶段双相机新帧确认；
- ROI 高频阶段双相机新帧确认；
- 全画幅和 ROI 使用不同超时时间；
- ROI 高频阶段确认成功后才设置 `m_liveStartupConfirmed = true`；
- 内部失败后停止相机、停止脉冲、恢复 `Idle`；
- 延迟重试定时器；
- `m_internalLiveStartupRetry`，用于避免内部重试被当成新的手动启动；
- 全画幅和 ROI 脉冲 ACK 超时后，继续用实际图像确认。

本任务不再修改上述整体架构。

---

# 2. 本次只修复以下问题

## 2.1 第 3 次重试只显示提示，但不会真正执行

当前代码在：

```cpp
shouldRetryFailedLiveStartup()
```

中包含：

```cpp
if (m_liveStartupRetryCount >=
    kLiveStartupMaxImmediateRetries) {
    return false;
}
```

但失败处理流程是：

```text
先 ++m_liveStartupRetryCount
再启动重试定时器
```

于是会出现：

```text
count = 1 → 执行第 1 次重试
count = 2 → 执行第 2 次重试
count = 3 → 显示第 3/3 次重试
定时器触发时 count >= 3 → 直接取消
```

实际只执行两次重试。

修复目标：

```text
初始失败后：
执行第 1 次重试
执行第 2 次重试
执行第 3 次重试
第 3 次重试再次失败后停止
```

---

## 2.2 全画幅重定位的普通失败会停留在假 Live

当前：

```cpp
applyLiveFullFrameForRelocalization()
```

遇到非 timeout 的脉冲启动失败、触发线恢复失败、相机恢复失败等情况时会返回：

```cpp
false
```

但调用方：

```cpp
requestLiveFullFrameRelocalization()
handleLiveRelocalizationWatchdog()
```

目前只显示错误状态，没有执行：

```text
停止相机
停止脉冲
返回 Idle
延迟自动重试
```

结果可能是：

```text
正常 Tracking
→ 星点丢失
→ 切回全画幅重定位
→ 普通错误
→ applyLiveFullFrameForRelocalization() 返回 false
→ CaptureState 仍为 Live
→ 没有图像
→ 没有有效确认定时器
→ 自动采集卡住
```

修复目标：

- 硬件触发模式下，任何全画幅重定位失败都必须进入统一的：
  ```cpp
  handleHardwareTriggerStartupFailure(...)
  ```
- 连续采集模式维持现有错误显示逻辑。

---

## 2.3 进入新的等待阶段时没有清除旧的启动确认状态

正常运行后：

```cpp
m_liveStartupConfirmed == true
```

如果星点丢失并切回全画幅：

```text
Running
→ WaitingFullFramePair
```

当前 `beginHardwareTriggerStartupStage()` 没有将：

```cpp
m_liveStartupConfirmed
```

重新设为 `false`。

这样会导致等待重定位确认期间，某些使用：

```cpp
!m_liveStartupConfirmed
```

判断“是否正在启动/恢复”的代码得到错误结果。

修复目标：

```text
WaitingFullFramePair       → m_liveStartupConfirmed = false
WaitingRoiTrackingPair     → m_liveStartupConfirmed = false
Running                    → m_liveStartupConfirmed = true
None                       → 按调用方负责清理
```

---

## 2.4 自动采集启动文案不准确

当前自动启动提示仍可能是：

```text
自动采集启动流程已发起，等待双相机首帧确认
```

但现在实际需要经过：

```text
全画幅双相机确认
+
ROI 高频双相机确认
```

应改为：

```text
自动采集启动流程已发起，等待全画幅和 ROI 高频触发确认
```

该项只修改文字，不修改状态逻辑。

---

# 3. 允许修改的文件

只修改：

```text
src1分钟窗口/DIMM.cpp
src1分钟窗口/DIMM.CommCamera.cpp
src1分钟窗口/DIMM.LiveRoi.cpp
```

通常不需要修改：

```text
src1分钟窗口/DIMM.h
```

除非执行过程中发现声明不匹配，否则不要修改头文件。

---

# 4. 禁止事项

- 不要构建；
- 不要运行；
- 不要测试；
- 不要执行格式化工具；
- 不要增加 `qDebug()`；
- 不要增加日志；
- 不要修改 UI 文件；
- 不要修改自动曝光；
- 不要修改相干时间；
- 不要修改窗口 ID；
- 不要修改 `AutoAcquisitionScheduler`；
- 不要修改 `CameraManager`；
- 不要修改 `PulseGeneratorManager`；
- 不要修改串口协议；
- 不要修改 CSV；
- 不要提交 Git。

---

# 第一部分：修复重试次数

## 5. 修改 `shouldRetryFailedLiveStartup()`

文件：

```text
src1分钟窗口/DIMM.cpp
```

找到：

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

    ...
}
```

删除下面这一整段：

```cpp
if (m_liveStartupRetryCount >=
    kLiveStartupMaxImmediateRetries) {
    return false;
}
```

修改后函数开头应为：

```cpp
bool DIMM::shouldRetryFailedLiveStartup() const
{
    if (m_liveStartupOrigin ==
        LiveStartupOrigin::Manual) {
        return true;
    }

    if (!m_autoAcquisitionConfig.enabled) {
        return false;
    }

    ...
}
```

说明：

`shouldRetryFailedLiveStartup()` 从本次修改后只负责检查：

- 手动启动来源是否仍允许内部重试；
- 自动采集是否启用；
- 当前时间是否仍在自动采集窗口内；
- 当前窗口 ID 是否仍一致；
- 当前窗口是否被用户手动停止。

它不再负责判断重试次数。

---

## 6. 修改 `handleHardwareTriggerStartupFailure()`

文件：

```text
src1分钟窗口/DIMM.cpp
```

找到：

```cpp
const bool retryAllowed =
    shouldRetryFailedLiveStartup();
```

替换为：

```cpp
const bool retryAllowed =
    m_liveStartupRetryCount <
        kLiveStartupMaxImmediateRetries &&
    shouldRetryFailedLiveStartup();
```

不要改变后面的：

```cpp
if (!retryAllowed) {
    ...
}
```

也不要改变：

```cpp
++m_liveStartupRetryCount;
```

修改后的逻辑为：

```text
当前 count = 0：
0 < 3，允许安排第 1 次重试
++count → 1

当前 count = 1：
1 < 3，允许安排第 2 次重试
++count → 2

当前 count = 2：
2 < 3，允许安排第 3 次重试
++count → 3

第 3 次重试再次失败：
当前 count = 3
3 < 3 为 false
不再安排新重试
```

---

## 7. 保留 `retryFailedLiveStartup()` 中的窗口检查

当前函数中存在：

```cpp
if (!shouldRetryFailedLiveStartup()) {
    m_liveStartupRecoveryInProgress = false;

    setStatusMessage(
        QStringLiteral(
            "硬件触发自动重试已取消"),
        UiStatusLevel::Warning);

    return;
}
```

此处保留，不删除。

因为 `shouldRetryFailedLiveStartup()` 已经不再检查计数，所以该处现在只负责在定时器等待期间重新确认：

- 用户是否关闭了自动采集；
- 自动采集窗口是否已经结束；
- 用户是否修改了计划；
- 当前窗口是否被手动抑制。

---

# 第二部分：进入等待阶段时清除旧确认状态

## 8. 修改 `beginHardwareTriggerStartupStage()`

文件：

```text
src1分钟窗口/DIMM.CommCamera.cpp
```

当前函数类似：

```cpp
void DIMM::beginHardwareTriggerStartupStage(
    HardwareTriggerStartupStage stage)
{
    m_hardwareTriggerStartupStage = stage;

    const auto& runtime = activeRuntime();

    ...
}
```

在：

```cpp
m_hardwareTriggerStartupStage = stage;
```

后增加：

```cpp
if (stage ==
        HardwareTriggerStartupStage::
            WaitingFullFramePair ||
    stage ==
        HardwareTriggerStartupStage::
            WaitingRoiTrackingPair) {
    m_liveStartupConfirmed = false;
}
```

完整函数应类似：

```cpp
void DIMM::beginHardwareTriggerStartupStage(
    HardwareTriggerStartupStage stage)
{
    m_hardwareTriggerStartupStage = stage;

    if (stage ==
            HardwareTriggerStartupStage::
                WaitingFullFramePair ||
        stage ==
            HardwareTriggerStartupStage::
                WaitingRoiTrackingPair) {
        m_liveStartupConfirmed = false;
    }

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

不要在该函数中清零：

```cpp
m_liveStartupRetryCount
m_liveStartupWindowId
m_liveStartupOrigin
```

---

# 第三部分：修复主动全画幅重定位失败

## 9. 修改 `requestLiveFullFrameRelocalization()`

文件：

```text
src1分钟窗口/DIMM.LiveRoi.cpp
```

当前函数开头类似：

```cpp
void DIMM::requestLiveFullFrameRelocalization(
    const QString& reason)
{
    if (m_captureState != CaptureState::Live ||
        !m_cameraManager) {
        return;
    }

    auto& runtime = activeRuntime();
    runtime.liveRelocalizationStartedMs =
        QDateTime::currentMSecsSinceEpoch();

    resetLiveFrameAcceptanceGates();

    QString switchReason;

    const bool fullFrameReady =
        applyLiveFullFrameForRelocalization(
            &switchReason);

    // 后面继续清理 ROI 状态
    ...
}
```

在：

```cpp
const bool fullFrameReady =
    applyLiveFullFrameForRelocalization(
        &switchReason);
```

后立即增加：

```cpp
if (!fullFrameReady &&
    m_configTriggerMode != 0) {
    const QString detail =
        switchReason.isEmpty()
            ? QStringLiteral(
                  "切回全画幅重定位失败")
            : switchReason;

    handleHardwareTriggerStartupFailure(
        detail);

    return;
}
```

修改后结构应为：

```cpp
QString switchReason;

const bool fullFrameReady =
    applyLiveFullFrameForRelocalization(
        &switchReason);

if (!fullFrameReady &&
    m_configTriggerMode != 0) {
    const QString detail =
        switchReason.isEmpty()
            ? QStringLiteral(
                  "切回全画幅重定位失败")
            : switchReason;

    handleHardwareTriggerStartupFailure(
        detail);

    return;
}

// 只有成功，或连续采集模式失败时，
// 才继续执行后面的 UI 和 ROI 状态更新。
```

说明：

- 硬件触发模式失败后，统一进入自动停止和重试；
- `handleHardwareTriggerStartupFailure()` 会负责：
  - 停止脉冲；
  - 停止相机；
  - 关闭结果文件；
  - 恢复 `Idle`；
  - 判断是否安排下一次重试；
- 因此这里调用后必须立刻 `return`；
- 不要在调用后继续清除 Canvas 或修改 `LiveStartupPhase`。

---

## 10. 保留连续采集模式的现有行为

当前函数后面存在：

```cpp
if (!fullFrameReady) {
    setStatusMessage(
        switchReason.isEmpty()
            ? QStringLiteral(
                  "状态: 回全画幅重新定位失败")
            : switchReason,
        UiStatusLevel::Error);
} else {
    ...
}
```

这段保留，用于：

```text
m_configTriggerMode == 0
```

的连续采集模式。

因为硬件触发模式失败已经在前面的新分支中 `return`，不会重复显示。

---

# 第四部分：修复重定位看门狗失败

## 11. 修改 `handleLiveRelocalizationWatchdog()`

文件：

```text
src1分钟窗口/DIMM.LiveRoi.cpp
```

找到：

```cpp
QString switchReason;

const bool fullFrameReady =
    applyLiveFullFrameForRelocalization(
        &switchReason);

if (ui->lblROITimeCurrent) {
    ...
}
```

在 `fullFrameReady` 计算后、任何 UI 更新前增加：

```cpp
if (!fullFrameReady &&
    m_configTriggerMode != 0) {
    const QString detail =
        switchReason.isEmpty()
            ? QStringLiteral(
                  "全画幅重定位超时后重新切换失败")
            : switchReason;

    handleHardwareTriggerStartupFailure(
        detail);

    return;
}
```

修改后应为：

```cpp
QString switchReason;

const bool fullFrameReady =
    applyLiveFullFrameForRelocalization(
        &switchReason);

if (!fullFrameReady &&
    m_configTriggerMode != 0) {
    const QString detail =
        switchReason.isEmpty()
            ? QStringLiteral(
                  "全画幅重定位超时后重新切换失败")
            : switchReason;

    handleHardwareTriggerStartupFailure(
        detail);

    return;
}

if (ui->lblROITimeCurrent) {
    ...
}
```

说明：

- 硬件触发模式中，重定位失败不能只显示错误；
- 必须停止整个本轮采集并进入统一重试；
- 连续采集模式继续使用原来的 UI 错误提示。

---

# 第五部分：修正自动采集启动文案

## 12. 修改 `evaluateAutoAcquisitionSchedule()`

文件：

```text
src1分钟窗口/DIMM.cpp
```

找到自动硬件触发启动后显示的文字：

```cpp
setAutoAcquisitionStatus(
    QStringLiteral(
        "自动采集启动流程已发起，等待双相机首帧确认"),
    UiStatusLevel::Warning,
    QStringLiteral(
        "auto-start-pending"));
```

仅替换文字为：

```cpp
setAutoAcquisitionStatus(
    QStringLiteral(
        "自动采集启动流程已发起，等待全画幅和 ROI 高频触发确认"),
    UiStatusLevel::Warning,
    QStringLiteral(
        "auto-start-pending"));
```

不要修改节流 key：

```cpp
"auto-start-pending"
```

不要修改自动启动状态字段。

---

# 第六部分：检查失败路径不会重复处理

## 13. 检查 `handleHardwareTriggerStartupFailure()`

确认函数开头仍有：

```cpp
if (m_liveStartupRecoveryInProgress) {
    return;
}
```

必须保留。

原因：

- 图像定时器；
- 相机断开信号；
- 重定位函数；
- 重定位看门狗；

可能在接近的时间触发同一个失败。

该保护用于防止重复停止、重复关闭文件和重复安排重试。

---

## 14. 检查重定位失败调用后必须 `return`

以下两个函数的新失败分支中必须包含：

```cpp
handleHardwareTriggerStartupFailure(
    detail);

return;
```

函数：

```text
requestLiveFullFrameRelocalization()
handleLiveRelocalizationWatchdog()
```

不能只调用而不返回。

否则失败处理已经将状态改成 `Idle` 后，函数仍会继续按照 `Live` 状态修改 ROI、Canvas 和提示文字。

---

# 第七部分：预期行为

## 15. 三次重试行为

正确顺序：

```text
初始启动失败
→ 3 秒后第 1/3 次重试

第 1 次重试失败
→ 3 秒后第 2/3 次重试

第 2 次重试失败
→ 3 秒后第 3/3 次重试

第 3 次重试失败
→ 不再安排立即重试
```

自动采集模式：

```text
保持 Idle
清除本轮 StartedCurrentRun
由现有自动调度稍后再次尝试
```

手动模式：

```text
保持 Idle
显示明确失败
不再自动循环
```

---

## 16. 正常运行后星点丢失

```text
Running
m_liveStartupConfirmed = true
→ 星点丢失
→ requestLiveFullFrameRelocalization()
→ beginHardwareTriggerStartupStage(
      WaitingFullFramePair)
→ m_liveStartupConfirmed = false
→ 等待双相机新的全画幅帧
```

---

## 17. 重定位普通错误

例如：

```text
恢复 Line0 触发源失败
全画幅低频脉冲启动普通错误
相机恢复采集失败
全画幅尺寸恢复失败
```

正确行为：

```text
applyLiveFullFrameForRelocalization() 返回 false
→ 调用 handleHardwareTriggerStartupFailure()
→ 停止脉冲
→ 停止相机
→ CaptureState = Idle
→ 延迟自动重试
```

不能只显示错误后继续保持 `Live`。

---

# 第八部分：静态检查清单

完成后只做文本搜索，不构建。

## 18. 检查重试次数判断

搜索：

```cpp
m_liveStartupRetryCount >=
```

在：

```cpp
shouldRetryFailedLiveStartup()
```

中不应再存在。

确认 `handleHardwareTriggerStartupFailure()` 中存在：

```cpp
const bool retryAllowed =
    m_liveStartupRetryCount <
        kLiveStartupMaxImmediateRetries &&
    shouldRetryFailedLiveStartup();
```

---

## 19. 检查阶段确认状态

确认 `beginHardwareTriggerStartupStage()` 中存在：

```cpp
m_liveStartupConfirmed = false;
```

并且只在以下阶段执行：

```text
WaitingFullFramePair
WaitingRoiTrackingPair
```

不要在 `Running` 时设为 false。

---

## 20. 检查主动重定位失败

确认 `requestLiveFullFrameRelocalization()` 中存在：

```cpp
if (!fullFrameReady &&
    m_configTriggerMode != 0) {
    ...
    handleHardwareTriggerStartupFailure(
        detail);
    return;
}
```

---

## 21. 检查看门狗重定位失败

确认 `handleLiveRelocalizationWatchdog()` 中存在相同类型的硬件触发失败分支。

---

## 22. 检查失败后无后续 UI 修改

在上述两个函数中，失败处理分支的：

```cpp
return;
```

必须位于任何后续 Canvas、ROI 和状态文字更新之前。

---

## 23. 检查自动启动文案

搜索旧文字：

```text
自动采集启动流程已发起，等待双相机首帧确认
```

应不存在。

确认新文字存在：

```text
自动采集启动流程已发起，等待全画幅和 ROI 高频触发确认
```

---

## 24. 检查没有破坏已有成功条件

搜索：

```cpp
m_liveStartupConfirmed = true;
```

仍应只存在于：

```text
DIMM.CommCamera.cpp
confirmHardwareTriggerStartupIfReady()
WaitingRoiTrackingPair 成功分支
```

不要新增其他赋值为 true 的位置。

---

# 第九部分：最终交付格式

执行完成后只回复：

```text
已完成自动采集自恢复第三轮收尾修复。

修改文件：
- src1分钟窗口/DIMM.cpp
- src1分钟窗口/DIMM.CommCamera.cpp
- src1分钟窗口/DIMM.LiveRoi.cpp

未构建，未运行测试。
```

不要声称：

```text
构建成功
测试通过
运行正常
```

---

# 第十部分：执行顺序摘要

严格按以下顺序执行：

```text
1. 删除 shouldRetryFailedLiveStartup() 中的次数判断
2. 在 handleHardwareTriggerStartupFailure() 中加入次数上限判断
3. 修改 beginHardwareTriggerStartupStage()，进入等待阶段时确认状态设为 false
4. 修改 requestLiveFullFrameRelocalization() 的硬件触发失败路径
5. 修改 handleLiveRelocalizationWatchdog() 的硬件触发失败路径
6. 修改自动采集启动等待文案
7. 搜索 m_liveStartupRetryCount >=
8. 搜索 m_liveStartupConfirmed = true
9. 搜索旧自动采集文案
10. 确认两个失败分支都有 return
11. 不构建
```
