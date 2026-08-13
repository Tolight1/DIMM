# 对准模式人工选星到正式采集的目标继承修复执行任务

> **执行对象：** 性能或推理能力较弱的代码 Agent  
> **执行原则：** 只修改源码、增加静态测试和文本检查。**禁止构建，禁止运行 CMake、MSBuild、Visual Studio Build、Qt Build 或生成可执行文件。**最终构建和相机实验由用户完成。

---

# 1. 任务背景

当前程序支持以下对准流程：

1. 进入对准模式；
2. 关闭北极星自动识别；
3. 显示全画幅候选星；
4. 用户分别点击“确认相机1的北极星”或“确认相机2的北极星”；
5. 用户输入候选星编号；
6. 程序记录人工确认的星点坐标；
7. 用户退出对准模式；
8. 用户开始正式采集；
9. 两台相机先进行全画幅定位，再切换到 `64×64` ROI 跟踪。

人工确认时，程序会保存：

```cpp
confirmedPolarisPosition[cameraIndex]
hasConfirmedPolarisPosition[cameraIndex]

lastTargetPosition[cameraIndex]
hasLastTargetPosition[cameraIndex]
```

退出对准模式时，人工确认坐标仍然存在。

但是开始正式采集时，`resetMeasurementState()` 会：

- 保留 `confirmedPolarisPosition`；
- 清空 `lastTargetPosition`；
- 清空候选编号。

正式采集的全画幅定位函数 `selectLiveRelocalizationCentroid()` 当前只检查：

```cpp
hasLastTargetPosition
```

如果不存在，就自动选择最强候选星。

它没有读取已经保留下来的：

```cpp
hasConfirmedPolarisPosition
confirmedPolarisPosition
```

因此当前行为是：

```text
人工确认某颗星
    ↓
退出对准模式
    ↓
确认坐标仍然保存
    ↓
开始正式采集
    ↓
lastTargetPosition 被清空
    ↓
正式采集没有使用 confirmedPolarisPosition
    ↓
可能自动锁定另一颗更亮的星
```

---

# 2. 修复目标

完成本任务后，正式采集全画幅定位必须按以下优先级选择目标：

```text
优先级 1：本次实时采集已经跟踪过的 lastTargetPosition
    ↓ 不存在
优先级 2：对准模式人工确认的 confirmedPolarisPosition
    ↓ 不存在
优先级 3：自动选择信号最强候选星
```

完整规则：

1. 如果当前实时采集已经建立过目标位置，优先匹配该位置附近的候选星。
2. 如果还没有实时目标，但对准模式保存了人工确认位置，优先匹配人工确认位置附近的候选星。
3. 只有前两类位置都不存在时，才允许自动选择最强候选星。
4. 如果存在优先目标，但当前帧在目标附近找不到合格候选：
   - 本帧定位失败；
   - 等待下一张全画幅；
   - **禁止悄悄切换到其他更亮的星。**
5. 全画幅成功选中候选后，立即写入 `lastTargetPosition`，使本次正式采集后续重定位保持星点身份。
6. 如果本次候选来自人工确认目标附近，需要把人工确认坐标更新为实际重新检测到的当前坐标，以吸收少量装调漂移或帧间位移。
7. 两台相机独立处理：
   - 相机1人工确认只影响相机1；
   - 相机2人工确认只影响相机2；
   - 未人工确认的相机仍保持自动选星行为。

---

# 3. 预期行为

## 3.1 人工确认星点不是最亮星

假设候选星：

```text
候选1：峰值 3500，位置 (1000, 1200)
候选2：峰值 2600，位置 (2100, 1800)
```

用户在对准模式人工选择候选2。

开始正式采集后应当：

```text
优先在 (2100, 1800) 附近重新寻找候选星
锁定候选2
建立以候选2为中心的 ROI
```

不得因为候选1更亮而切换到候选1。

---

## 3.2 人工确认目标暂时未检测到

用户确认位置：

```text
(2100, 1800)
```

当前正式采集全画幅只有其他亮星，确认目标附近没有候选。

程序应当：

```text
不选择其他星
保持目标身份
等待下一张全画幅
显示“人工确认星点附近未找到候选星”
```

不得自动选最亮星。

---

## 3.3 没有人工确认

如果该相机：

```cpp
hasLastTargetPosition == false
hasConfirmedPolarisPosition == false
```

则保持原有逻辑：

```text
自动选择满足条件的最强候选星
```

---

## 3.4 正式采集过程中的重定位

正式采集已经成功锁定某颗星后：

```cpp
hasLastTargetPosition == true
```

发生 ROI 丢失并回到全画幅重定位时，应优先使用最新的 `lastTargetPosition`，而不是较早的对准确认位置。

也就是：

```text
本次实时采集最新位置
优先于
对准模式历史确认位置
```

---

# 4. 非目标

本任务不做以下事情：

- 不改变候选星检测算法。
- 不改变星点连通域阈值。
- 不改变 ROI 尺寸。
- 不改变双相机同步切换逻辑。
- 不改变硬件触发流程。
- 不改变连续采集流程。
- 不修改北极星自动识别算法。
- 不修改粗对准算法。
- 不修改 `PolarisSolver`。
- 不修改 `ImageProcessor`。
- 不增加数据库或配置项。
- 不将人工确认坐标写入磁盘。
- 不让相机1的目标坐标用于相机2。
- 不允许有偏好目标时自动回退到最亮星。
- 不运行任何 C++ 构建。
- 不提交、不推送、不创建 PR，除非用户另行明确要求。

---

# 5. 修改文件清单

需要修改：

```text
src自动采集/DIMM.h
src自动采集/DIMM.LiveRoi.cpp
src自动采集/DIMM.cpp
```

需要新增：

```text
src自动采集/tests/test_alignment_target_handoff_static.py
```

不应修改：

```text
src自动采集/DIMM.Alignment.cpp
src自动采集/AlignmentSession.cpp
src自动采集/AlignmentSession.h
src自动采集/PolarisDetectionPipeline.cpp
src自动采集/PolarisDetectionPipeline.h
src自动采集/FullFrameStarDetector.cpp
src自动采集/ImageProcessor.cpp
src自动采集/CameraManager.cpp
src自动采集/CMakeLists.txt
```

`DIMM.cpp` 只增加解释性注释，不改变 `resetMeasurementState()` 的现有保存和清理语义。

---

# 6. 强制执行规则

执行 Agent 必须遵守：

- [ ] 修改前先确认函数和字段存在。
- [ ] 使用小范围补丁。
- [ ] 优先使用 `apply_patch`。
- [ ] 不使用 PowerShell `Set-Content` 重写整个 C++ 文件。
- [ ] 不改变文件换行和编码。
- [ ] 不损坏中文字符串。
- [ ] 不运行 CMake。
- [ ] 不运行 MSBuild。
- [ ] 不运行 Visual Studio Build。
- [ ] 不运行 Qt Build。
- [ ] 不生成 `.exe`、`.obj`、`.pdb` 或构建目录。
- [ ] 只允许运行 Python 静态测试和文本检查。
- [ ] 不进行无关格式化。
- [ ] 不重命名现有类和函数。
- [ ] 不更改线程模型。
- [ ] 不更改相机启动顺序。
- [ ] 不更改触发模式。
- [ ] 不删除原有自动选择逻辑。
- [ ] 不在有偏好目标时自动回退到最亮星。
- [ ] 最终不得声称“编译成功”或“运行成功”。

---

# Task 0：确认代码基线

## 0.1 检查 `DIMM.h`

确认存在声明：

```cpp
bool selectLiveRelocalizationCentroid(int cameraIndex,
                                      const cv::Mat& mono8,
                                      QPointF* centroid,
                                      double* peakValue);
```

确认 `CaptureRuntimeContext` 中存在：

```cpp
QPointF lastTargetPosition[2];
bool hasLastTargetPosition[2] = {false, false};

QPointF confirmedPolarisPosition[2];
bool hasConfirmedPolarisPosition[2] = {false, false};
```

---

## 0.2 检查 `DIMM.LiveRoi.cpp`

确认存在函数：

```cpp
bool DIMM::selectLiveRelocalizationCentroid(...)
```

确认当前函数中存在：

```cpp
if (runtime.hasLastTargetPosition[cameraIndex]) {
```

以及自动选择：

```cpp
PolarisDetectionPipeline::chooseAutomaticInitialStarCandidate(...)
```

确认存在调用位置：

```cpp
if (!selectLiveRelocalizationCentroid(cameraIndex,
                                      grayscale,
                                      &centroid,
                                      &peakValue)) {
```

如果以上结构不存在，停止修改并报告代码基线不一致，不要猜测新的插入位置。

---

## 0.3 检查 `DIMM.cpp`

确认 `resetMeasurementState()` 当前：

1. 保存并恢复 `confirmedPolarisPosition`；
2. 保存并恢复 `hasConfirmedPolarisPosition`；
3. 清空 `lastTargetPosition`；
4. 将 `hasLastTargetPosition` 设为 `false`。

本任务保留这种语义：

```text
confirmedPolarisPosition：
    用于从对准模式向正式采集传递人工目标身份。

lastTargetPosition：
    用于本次正式采集过程中的动态目标身份。
```

不得简单删除对 `lastTargetPosition` 的清理。

---

# Task 1：扩展全画幅目标选择函数接口

## 文件

```text
src自动采集/DIMM.h
```

找到：

```cpp
bool selectLiveRelocalizationCentroid(int cameraIndex,
                                      const cv::Mat& mono8,
                                      QPointF* centroid,
                                      double* peakValue);
```

替换为：

```cpp
bool selectLiveRelocalizationCentroid(
    int cameraIndex,
    const cv::Mat& mono8,
    QPointF* centroid,
    double* peakValue,
    QString* selectionSource = nullptr,
    QString* failureReason = nullptr);
```

字段说明：

- `selectionSource`：
  - 成功时返回目标选择来源；
  - 用于界面显示和实验验证；
  - 允许传入 `nullptr`。
- `failureReason`：
  - 失败时返回具体原因；
  - 允许传入 `nullptr`。
- 默认参数保证未来如果还有未发现的旧调用点，也不会因为新增参数而必须立即修改。

不要：

- 新建类；
- 新建枚举；
- 新增头文件；
- 改变函数可见性；
- 修改函数名称。

---

# Task 2：重写正式采集全画幅候选选择逻辑

## 文件

```text
src自动采集/DIMM.LiveRoi.cpp
```

找到完整函数：

```cpp
bool DIMM::selectLiveRelocalizationCentroid(...)
```

将整个函数替换为以下实现：

```cpp
bool DIMM::selectLiveRelocalizationCentroid(
    int cameraIndex,
    const cv::Mat& fullFrame,
    QPointF* centroid,
    double* peakValue,
    QString* selectionSource,
    QString* failureReason)
{
    if (selectionSource) {
        selectionSource->clear();
    }
    if (failureReason) {
        failureReason->clear();
    }

    if (cameraIndex < 0 ||
        cameraIndex >= 2 ||
        fullFrame.empty() ||
        !centroid) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("全画幅目标选择参数无效");
        }
        return false;
    }

    QVector<InitialStarCandidate> candidates =
        detectInitialStarCandidates(fullFrame, peakValue);
    if (candidates.isEmpty()) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("相机%1全画幅未检测到有效候选星")
                    .arg(cameraIndex + 1);
        }
        return false;
    }

    auto& runtime = activeRuntime();

    const auto applySelection =
        [&](const InitialStarSelection& selection,
            const QString& source) {
            if (!selection.selected) {
                return false;
            }

            *centroid = selection.candidate.center;
            if (peakValue) {
                *peakValue = selection.candidate.peak;
            }
            if (selectionSource) {
                *selectionSource = source;
            }
            return true;
        };

    if (runtime.hasLastTargetPosition[cameraIndex]) {
        const InitialStarSelection selection =
            PolarisDetectionPipeline::selectInitialStarCandidate(
                candidates,
                true,
                runtime.lastTargetPosition[cameraIndex],
                0);

        if (!selection.selected) {
            if (failureReason) {
                *failureReason =
                    QStringLiteral(
                        "相机%1上次实时跟踪目标附近未找到候选星，"
                        "保持目标身份并等待下一帧")
                        .arg(cameraIndex + 1);
            }
            return false;
        }

        return applySelection(
            selection,
            QStringLiteral("上次实时跟踪目标"));
    }

    if (runtime.hasConfirmedPolarisPosition[cameraIndex]) {
        const InitialStarSelection selection =
            PolarisDetectionPipeline::selectInitialStarCandidate(
                candidates,
                true,
                runtime.confirmedPolarisPosition[cameraIndex],
                0);

        if (!selection.selected) {
            if (failureReason) {
                *failureReason =
                    QStringLiteral(
                        "相机%1人工确认星点附近未找到候选星，"
                        "不切换到其他亮星，等待下一帧")
                        .arg(cameraIndex + 1);
            }
            return false;
        }

        return applySelection(
            selection,
            QStringLiteral("对准模式人工确认目标"));
    }

    InitialStarCandidate selectedCandidate;
    const InitialStarCandidate strongestCandidate =
        candidates.first();

    if (!PolarisDetectionPipeline::chooseAutomaticInitialStarCandidate(
            candidates,
            strongestCandidate,
            &selectedCandidate,
            nullptr)) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "相机%1没有人工确认目标，自动候选筛选未通过")
                    .arg(cameraIndex + 1);
        }
        return false;
    }

    *centroid = selectedCandidate.center;
    if (peakValue) {
        *peakValue = selectedCandidate.peak;
    }
    if (selectionSource) {
        *selectionSource =
            QStringLiteral("自动最强候选");
    }
    return true;
}
```

---

## 2.1 必须保留的选择顺序

源码中的实际顺序必须是：

```cpp
runtime.hasLastTargetPosition[cameraIndex]
```

先于：

```cpp
runtime.hasConfirmedPolarisPosition[cameraIndex]
```

并且两者都先于：

```cpp
chooseAutomaticInitialStarCandidate
```

即：

```text
lastTargetPosition
    ↓
confirmedPolarisPosition
    ↓
automatic strongest candidate
```

---

## 2.2 禁止静默回退

在以下两种情况中：

```cpp
hasLastTargetPosition == true
```

或：

```cpp
hasConfirmedPolarisPosition == true
```

如果附近匹配失败，必须直接：

```cpp
return false;
```

不得继续执行：

```cpp
chooseAutomaticInitialStarCandidate(...)
```

原因：

- 自动回退会破坏人工目标身份；
- 用户无法察觉程序已经换星；
- 后续测量可能对应错误目标；
- 位置门限本身就是身份保护机制。

---

## 2.3 不修改候选匹配算法

继续使用：

```cpp
PolarisDetectionPipeline::selectInitialStarCandidate(
    candidates,
    true,
    preferredTarget,
    0);
```

本任务不修改：

- 距离门限；
- 候选排序；
- 候选面积；
- 峰值阈值；
- `selectInitialStarCandidate()` 的内部实现。

---

# Task 3：在正式采集定位时记录选择来源和目标位置

## 文件

```text
src自动采集/DIMM.LiveRoi.cpp
```

在 `DIMM::maybeSeedRoiFromFrame()` 中找到：

```cpp
if (liveLocatePhase) {
    if (!selectLiveRelocalizationCentroid(
            cameraIndex,
            grayscale,
            &centroid,
            &peakValue)) {
        ...
    }
    runtime.liveRelocalizationPreviewFrame[cameraIndex] =
        frame.clone();
    ...
}
```

将整个 `if (liveLocatePhase)` 分支的对应部分修改为以下结构。

注意：只替换该分支，不要重写整个 `maybeSeedRoiFromFrame()`。

```cpp
if (liveLocatePhase) {
    QString selectionSource;
    QString selectionFailureReason;

    if (!selectLiveRelocalizationCentroid(
            cameraIndex,
            grayscale,
            &centroid,
            &peakValue,
            &selectionSource,
            &selectionFailureReason)) {
        if (targetCanvas) {
            targetCanvas->clearStarCandidateOverlays();
        }

        setStatusMessage(
            selectionFailureReason.isEmpty()
                ? QStringLiteral(
                      "状态: 相机%1全画幅找星未找到有效目标，"
                      "等待下一帧")
                      .arg(cameraIndex + 1)
                : QStringLiteral("状态: %1")
                      .arg(selectionFailureReason),
            UiStatusLevel::Warning);
        return false;
    }

    runtime.lastTargetPosition[cameraIndex] = centroid;
    runtime.hasLastTargetPosition[cameraIndex] = true;

    if (runtime.hasConfirmedPolarisPosition[cameraIndex]) {
        runtime.confirmedPolarisPosition[cameraIndex] =
            centroid;
    }

    runtime.liveRelocalizationPreviewFrame[cameraIndex] =
        frame.clone();
    runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
        false;

    setStatusMessage(
        QStringLiteral(
            "状态: 相机%1全画幅定位找到星点 "
            "(%2, %3)，峰值%4，来源: %5，第%6帧")
            .arg(cameraIndex + 1)
            .arg(centroid.x(), 0, 'f', 1)
            .arg(centroid.y(), 0, 'f', 1)
            .arg(peakValue, 0, 'f', 1)
            .arg(selectionSource.isEmpty()
                     ? QStringLiteral("未知")
                     : selectionSource)
            .arg(runtime.frameCountPerCamera[cameraIndex]),
        UiStatusLevel::Info);
}
```

---

## 3.1 为什么成功定位后立即写入 `lastTargetPosition`

原有流程通常要等到切换 ROI、开始质心跟踪并收到有效质心后，才更新：

```cpp
lastTargetPosition
```

这中间存在一个时间窗口：

```text
全画幅已经选中目标
    ↓
尚未收到 ROI 有效质心
    ↓
如果重新进入全画幅定位
    ↓
可能没有本次实时目标位置
```

因此成功选中全画幅候选后应立即写入：

```cpp
runtime.lastTargetPosition[cameraIndex] = centroid;
runtime.hasLastTargetPosition[cameraIndex] = true;
```

这样本次实时采集从第一轮全画幅定位开始就拥有稳定目标身份。

---

## 3.2 为什么更新 `confirmedPolarisPosition`

如果人工确认位置为：

```text
(2100.0, 1800.0)
```

正式采集重新检测到同一目标的实际中心为：

```text
(2102.4, 1798.7)
```

应更新：

```cpp
confirmedPolarisPosition = centroid;
```

原因：

- 全画幅间可能有少量位置变化；
- 候选中心计算可能有亚像素差异；
- 下一次重定位应使用最新实际位置；
- 只在原本存在人工确认目标时更新，不把自动选星伪装成人工确认。

不得无条件执行：

```cpp
runtime.hasConfirmedPolarisPosition[cameraIndex] = true;
```

自动选星时仍然不应被标记为人工确认。

---

## 3.3 保持双相机独立

所有访问必须包含：

```cpp
[cameraIndex]
```

不得写成固定的：

```cpp
[0]
```

或：

```cpp
[1]
```

不得把相机1选择结果复制给相机2。

---

# Task 4：为状态保留逻辑增加解释性注释

## 文件

```text
src自动采集/DIMM.cpp
```

找到：

```cpp
void DIMM::resetMeasurementState()
{
    auto& runtime = activeRuntime();
    const QPointF preservedConfirmedPolarisPosition[2] = {
```

在 `auto& runtime = activeRuntime();` 后加入：

```cpp
    // Preserve the target confirmed in alignment mode so the next live
    // full-frame localization can reacquire the same star. In contrast,
    // lastTargetPosition belongs to the previous live tracking session
    // and is intentionally reset below.
```

结果应为：

```cpp
void DIMM::resetMeasurementState()
{
    auto& runtime = activeRuntime();

    // Preserve the target confirmed in alignment mode so the next live
    // full-frame localization can reacquire the same star. In contrast,
    // lastTargetPosition belongs to the previous live tracking session
    // and is intentionally reset below.
    const QPointF preservedConfirmedPolarisPosition[2] = {
        runtime.confirmedPolarisPosition[0],
        runtime.confirmedPolarisPosition[1]
    };
```

只增加注释。

不要：

- 取消 `lastTargetPosition` 清理；
- 新增状态字段；
- 修改 `CaptureRuntimeContext`；
- 改变模拟采集状态；
- 改变测量数据重置流程。

---

# Task 5：新增静态防回归测试

## 文件

```text
src自动采集/tests/test_alignment_target_handoff_static.py
```

如果 `tests` 目录不存在，创建该目录。

创建以下内容：

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(
        encoding="utf-8-sig"
    )


def function_block(
    text: str,
    start_token: str,
    end_token: str,
) -> str:
    start = text.find(start_token)
    if start < 0:
        raise AssertionError(
            f"missing function start: {start_token}"
        )

    end = text.find(end_token, start)
    if end < 0:
        raise AssertionError(
            f"missing function end token: {end_token}"
        )

    return text[start:end]


class AlignmentTargetHandoffStaticTest(
    unittest.TestCase
):
    def test_header_exposes_selection_diagnostics(self):
        header = read("DIMM.h")

        self.assertIn(
            "bool selectLiveRelocalizationCentroid(",
            header,
        )
        self.assertIn(
            "QString* selectionSource = nullptr",
            header,
        )
        self.assertIn(
            "QString* failureReason = nullptr",
            header,
        )

    def test_live_selection_priority_is_correct(self):
        cpp = read("DIMM.LiveRoi.cpp")
        block = function_block(
            cpp,
            "bool DIMM::selectLiveRelocalizationCentroid(",
            "bool DIMM::maybeSeedRoiFromFrame(",
        )

        last_target = block.find(
            "runtime.hasLastTargetPosition"
        )
        alignment_target = block.find(
            "runtime.hasConfirmedPolarisPosition"
        )
        automatic_target = block.find(
            "chooseAutomaticInitialStarCandidate"
        )

        self.assertGreaterEqual(last_target, 0)
        self.assertGreaterEqual(alignment_target, 0)
        self.assertGreaterEqual(automatic_target, 0)

        self.assertLess(
            last_target,
            alignment_target,
        )
        self.assertLess(
            alignment_target,
            automatic_target,
        )

    def test_preferred_target_failure_does_not_switch_star(self):
        cpp = read("DIMM.LiveRoi.cpp")
        block = function_block(
            cpp,
            "bool DIMM::selectLiveRelocalizationCentroid(",
            "bool DIMM::maybeSeedRoiFromFrame(",
        )

        self.assertIn(
            "上次实时跟踪目标附近未找到候选星",
            block,
        )
        self.assertIn(
            "人工确认星点附近未找到候选星",
            block,
        )
        self.assertIn(
            "不切换到其他亮星",
            block,
        )

        confirmed_start = block.find(
            "if (runtime.hasConfirmedPolarisPosition"
        )
        automatic_start = block.find(
            "chooseAutomaticInitialStarCandidate"
        )
        confirmed_block = block[
            confirmed_start:automatic_start
        ]

        self.assertIn(
            "return false;",
            confirmed_block,
        )

    def test_live_localization_records_selected_target(self):
        cpp = read("DIMM.LiveRoi.cpp")
        block = function_block(
            cpp,
            "bool DIMM::maybeSeedRoiFromFrame(",
            "void DIMM::handleLiveRelocalizationWatchdog(",
        )

        self.assertIn(
            "QString selectionSource;",
            block,
        )
        self.assertIn(
            "QString selectionFailureReason;",
            block,
        )
        self.assertIn(
            "runtime.lastTargetPosition[cameraIndex] = "
            "centroid;",
            block,
        )
        self.assertIn(
            "runtime.hasLastTargetPosition[cameraIndex] = "
            "true;",
            block,
        )
        self.assertIn(
            "runtime.confirmedPolarisPosition[cameraIndex] =",
            block,
        )
        self.assertIn(
            "来源: %5",
            block,
        )

    def test_measurement_reset_preserves_alignment_target_only(self):
        cpp = read("DIMM.cpp")
        block = function_block(
            cpp,
            "void DIMM::resetMeasurementState()",
            "void DIMM::updateCaptureState(",
        )

        self.assertIn(
            "preservedConfirmedPolarisPosition",
            block,
        )
        self.assertIn(
            "preservedHasConfirmedPolarisPosition",
            block,
        )
        self.assertIn(
            "runtime.hasLastTargetPosition[0] = false;",
            block,
        )
        self.assertIn(
            "runtime.hasLastTargetPosition[1] = false;",
            block,
        )
        self.assertIn(
            "lastTargetPosition belongs to the previous "
            "live tracking session",
            block,
        )

    def test_source_labels_exist(self):
        cpp = read("DIMM.LiveRoi.cpp")

        self.assertIn(
            'QStringLiteral("上次实时跟踪目标")',
            cpp,
        )
        self.assertIn(
            'QStringLiteral("对准模式人工确认目标")',
            cpp,
        )
        self.assertIn(
            'QStringLiteral("自动最强候选")',
            cpp,
        )

    def test_no_build_system_change_is_required(self):
        cmake = read("CMakeLists.txt")

        self.assertNotIn(
            "test_alignment_target_handoff_static.py",
            cmake,
        )


if __name__ == "__main__":
    unittest.main()
```

---

# Task 6：运行静态测试

只允许运行：

```powershell
cd src自动采集
python -m unittest tests.test_alignment_target_handoff_static
```

预期：

```text
.......
----------------------------------------------------------------------
Ran 7 tests

OK
```

这是 Python 源码静态检查，不是 C++ 构建。

如果测试失败：

1. 根据断言修复字段、顺序或字符串；
2. 不删除测试；
3. 不弱化断言；
4. 不通过修改测试来掩盖实现缺失；
5. 不运行 C++ 构建。

---

# Task 7：文本和编码检查

## 7.1 检查接口参数

执行：

```powershell
Select-String `
  -Path "DIMM.h","DIMM.LiveRoi.cpp" `
  -Pattern "selectionSource|failureReason"
```

预期：

- `DIMM.h` 中出现声明；
- `DIMM.LiveRoi.cpp` 中出现实现和调用。

---

## 7.2 检查目标优先级符号

执行：

```powershell
Select-String `
  -Path "DIMM.LiveRoi.cpp" `
  -Pattern "hasLastTargetPosition|hasConfirmedPolarisPosition|chooseAutomaticInitialStarCandidate"
```

人工检查顺序：

```text
hasLastTargetPosition
先于
hasConfirmedPolarisPosition
先于
chooseAutomaticInitialStarCandidate
```

只检查 `selectLiveRelocalizationCentroid()` 函数内部。

---

## 7.3 检查中文替换字符

执行：

```powershell
@'
from pathlib import Path

files = [
    "DIMM.h",
    "DIMM.LiveRoi.cpp",
    "DIMM.cpp",
    "tests/test_alignment_target_handoff_static.py",
]

failed = False

for rel in files:
    text = Path(rel).read_text(
        encoding="utf-8-sig"
    )
    count = text.count(chr(0xfffd))
    print(
        f"{rel}: replacement-char count = {count}"
    )
    if count:
        failed = True

if failed:
    raise SystemExit(1)
'@ | python -
```

预期：

```text
DIMM.h: replacement-char count = 0
DIMM.LiveRoi.cpp: replacement-char count = 0
DIMM.cpp: replacement-char count = 0
tests/test_alignment_target_handoff_static.py: replacement-char count = 0
```

如果出现非零：

- 停止继续修改；
- 恢复损坏文件；
- 使用小范围 UTF-8 补丁重新执行；
- 不提交乱码文件。

---

## 7.4 检查补丁格式

执行：

```powershell
git diff --check
```

预期：无输出。

---

## 7.5 检查修改文件范围

执行：

```powershell
git diff --name-only
```

本任务允许出现：

```text
src自动采集/DIMM.h
src自动采集/DIMM.LiveRoi.cpp
src自动采集/DIMM.cpp
src自动采集/tests/test_alignment_target_handoff_static.py
```

如果同时在执行之前已经存在其他未提交修改，不要删除用户已有修改；但必须在最终报告中区分：

```text
本任务修改
已有修改
```

不得把其他已有修改声称为本任务完成。

---

# Task 8：人工代码审查清单

## 8.1 状态传递

- [ ] 人工确认时仍写入 `confirmedPolarisPosition`。
- [ ] 退出对准模式后确认位置未被清除。
- [ ] 开始采集时确认位置仍被保留。
- [ ] 开始采集时旧的 `lastTargetPosition` 仍会清空。
- [ ] 全画幅定位会读取 `confirmedPolarisPosition`。
- [ ] 成功定位后会立即写入新的 `lastTargetPosition`。
- [ ] 正式采集后续重定位优先使用 `lastTargetPosition`。

---

## 8.2 选择优先级

- [ ] `hasLastTargetPosition` 是第一优先级。
- [ ] `hasConfirmedPolarisPosition` 是第二优先级。
- [ ] 自动最强候选是第三优先级。
- [ ] 有上次实时目标但匹配失败时不会自动换星。
- [ ] 有人工确认目标但匹配失败时不会自动换星。
- [ ] 无任何偏好时保持原有自动选星。
- [ ] 相机1和相机2独立选择。

---

## 8.3 坐标更新

- [ ] 成功匹配人工确认目标后，更新 `confirmedPolarisPosition` 到当前检测中心。
- [ ] 自动选星时不会把 `hasConfirmedPolarisPosition` 设置为 `true`。
- [ ] 所有数组访问均使用 `[cameraIndex]`。
- [ ] 不使用相机1位置初始化相机2。
- [ ] 不使用 ROI 局部坐标覆盖全画幅坐标。
- [ ] 写入的是候选星全画幅中心坐标。

---

## 8.4 状态提示

- [ ] 成功时显示来源。
- [ ] 来源可能是“上次实时跟踪目标”。
- [ ] 来源可能是“对准模式人工确认目标”。
- [ ] 来源可能是“自动最强候选”。
- [ ] 人工目标附近没有候选时明确提示不换星。
- [ ] 没检测到候选时明确提示等待下一帧。
- [ ] 状态文本中的相机编号使用 `cameraIndex + 1`。

---

## 8.5 不应发生的副作用

- [ ] 未修改自动曝光。
- [ ] 未修改硬件触发。
- [ ] 未修改脉冲板控制。
- [ ] 未修改 ROI 更新门限。
- [ ] 未修改质心算法。
- [ ] 未修改粗对准。
- [ ] 未修改北极星求解器。
- [ ] 未修改 CMake。
- [ ] 未增加线程。
- [ ] 未增加锁。
- [ ] 未增加每帧大量日志。
- [ ] 未构建项目。

---

# Task 9：用户本地构建后的实验验收

以下实验由用户自己构建后执行。Agent 不执行。

---

## 场景 A：单颗星，人工确认

条件：

```text
每台相机画面只有一颗星
关闭北极星自动识别
分别人工确认该星
退出对准模式
开始正式采集
```

期望：

```text
状态来源：对准模式人工确认目标
两台相机均建立该星的 64×64 ROI
随后持续跟踪
```

---

## 场景 B：人工目标不是最亮星

条件：

```text
画面中至少两颗星
候选1比候选2亮
人工确认候选2
退出对准模式
开始正式采集
```

期望：

```text
正式采集选择候选2
状态来源：对准模式人工确认目标
不得选择候选1
```

这是本任务最关键的验收场景。

---

## 场景 C：人工目标附近暂时没有候选

条件：

```text
人工确认候选2
开始正式采集时遮挡候选2
画面仍存在更亮候选1
```

期望：

```text
不选择候选1
不切换 ROI 到候选1
提示：
人工确认星点附近未找到候选星，
不切换到其他亮星，等待下一帧
```

解除遮挡后：

```text
重新检测到候选2
定位成功
建立 ROI
```

---

## 场景 D：只确认相机1

条件：

```text
相机1人工确认候选2
相机2不人工确认
退出对准模式
开始正式采集
```

期望：

```text
相机1：
来源为对准模式人工确认目标

相机2：
来源为自动最强候选
```

两台相机互不影响。

---

## 场景 E：采集中丢失 ROI 后重定位

条件：

```text
人工确认目标
正式采集成功锁定
目标缓慢移动
ROI 跟踪一段时间后让星点离开 ROI
程序返回全画幅重定位
```

期望：

```text
重定位来源：上次实时跟踪目标
不是旧的人工确认原始位置
不是自动最强候选
```

因为：

```text
lastTargetPosition
优先于
confirmedPolarisPosition
```

---

## 场景 F：没有进入对准模式

条件：

```text
直接开始正式采集
没有人工确认坐标
```

期望：

```text
保持原有自动定位
来源：自动最强候选
```

说明本修复没有破坏原来的直接采集流程。

---

## 场景 G：重新进入对准模式

条件：

```text
之前曾人工确认目标A
停止采集
重新进入对准模式
```

现有 `resetCameraForStart()` 会清除旧确认状态。

期望：

```text
旧目标A不应自动成为新对准会话确认目标
需要重新识别或人工确认
```

本任务不得破坏该行为。

---

# Task 10：边界情况

## 10.1 人工确认目标移动太远

如果目标从人工确认位置移动距离超过现有候选偏好门限：

```text
匹配失败
等待下一帧
不自动换星
```

这是身份保护，不是程序错误。

本任务不修改距离门限。

---

## 10.2 人工确认目标离开画面

如果人工目标完全离开传感器：

```text
持续匹配失败
正式采集停留在全画幅定位阶段
不切换到错误星点
```

用户应重新进入对准模式确认目标，或清除人工确认状态。

本任务不新增“忽略人工确认并自动换星”按钮。

---

## 10.3 多颗星距离人工目标都很近

继续使用现有：

```cpp
selectInitialStarCandidate(...)
```

决定候选。

本任务不定义新的多目标匹配算法。

---

## 10.4 自动采集计划

自动采集调用相同的正式采集流程，因此：

- 如果内存中存在有效人工确认目标，会优先继承；
- 如果不存在，则自动选星。

本任务不增加人工确认结果的跨程序重启持久化。

关闭程序重新启动后，不保证保留人工目标。

---

# Task 11：最终差异检查

执行：

```powershell
git diff -- src自动采集/DIMM.h
git diff -- src自动采集/DIMM.LiveRoi.cpp
git diff -- src自动采集/DIMM.cpp
git diff -- src自动采集/tests/test_alignment_target_handoff_static.py
```

人工确认：

1. `DIMM.h` 只修改函数声明；
2. `DIMM.LiveRoi.cpp` 只修改目标选择函数和实时定位分支；
3. `DIMM.cpp` 只增加注释；
4. 新增一个静态测试文件；
5. 没有大范围格式化；
6. 没有中文乱码；
7. 没有 CMake 修改；
8. 没有构建产物。

---

# Task 12：Agent 最终报告模板

Agent 完成后只报告：

```text
已完成“对准人工选星到正式采集目标继承”修改。

修改文件：
1. src自动采集/DIMM.h
2. src自动采集/DIMM.LiveRoi.cpp
3. src自动采集/DIMM.cpp
4. 新增 src自动采集/tests/test_alignment_target_handoff_static.py

实现结果：
- 正式采集优先使用本次实时跟踪位置；
- 没有实时位置时使用对准模式人工确认位置；
- 两者都不存在时才自动选择最强候选；
- 有偏好目标但匹配失败时不会静默切换其他星；
- 全画幅成功定位后立即记录 lastTargetPosition；
- 状态栏显示目标选择来源。

静态测试：
- test_alignment_target_handoff_static.py：通过/失败

文本检查：
- git diff --check：通过/失败
- UTF-8 replacement character：0/存在问题

明确未执行：
- 未运行 CMake
- 未运行 MSBuild
- 未运行 Visual Studio Build
- 未运行 Qt Build
- 未执行任何 C++ 构建
- 未进行相机实验

需要用户本地构建，并重点验证场景B、C、D、E、F。
```

不得报告：

```text
编译成功
程序运行正常
相机跟踪验证成功
```

因为本任务明确不允许构建和实验。

---

# 13. 修改完成后的状态链

## 修改前

```text
对准模式人工确认星点
    ↓
confirmedPolarisPosition 已保存
    ↓
退出对准模式
    ↓
开始正式采集
    ↓
lastTargetPosition 被清空
    ↓
全画幅定位只检查 lastTargetPosition
    ↓
自动选择最强候选
    ↓
可能跟踪错误星点
```

## 修改后

```text
对准模式人工确认星点
    ↓
confirmedPolarisPosition 已保存
    ↓
退出对准模式
    ↓
开始正式采集
    ↓
旧 lastTargetPosition 被清空
    ↓
全画幅定位检查：
    1. 本次实时 lastTargetPosition
    2. 人工 confirmedPolarisPosition
    3. 自动最强候选
    ↓
人工目标附近匹配成功
    ↓
立即写入新的 lastTargetPosition
    ↓
建立 64×64 ROI
    ↓
持续跟踪同一颗星
```

---

# 14. 核心安全原则

本任务最重要的原则不是“尽量找到一颗星”，而是：

> **当用户已经人工指定目标身份后，宁可等待该目标重新出现，也不能静默切换到另一颗更亮的星。**

这保证：

- 人工选择具有实际约束力；
- 对准模式与正式采集语义一致；
- ROI 跟踪目标可追溯；
- 多星场景不会因亮度排序改变目标；
- 后续测量对应用户明确选定的星点。
