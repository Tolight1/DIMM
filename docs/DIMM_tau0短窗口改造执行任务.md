# DIMM `tau0` 短窗口与欠分辨状态改造执行任务

## 1. 任务信息

- 仓库：`Tolight1/DIMM`
- 目标目录：`src1分钟窗口/`
- 参考分支：`master`
- 编写任务时检查到的最新提交：`953cb87a1ce540d83892e58a81d11d207ce84b83`
- 主要语言：C++17、Qt、OpenCV
- 本任务只修改源码。
- **不要构建、不要运行程序、不要运行测试、不要修改工程生成文件。**
- **不要增加 `qDebug()`、日志文件、诊断信号或其他诊断输出。**

---

## 2. 问题背景

当前程序使用差分质心序列的归一化自相关函数估计相干时间 `tau0`。程序寻找自相关第一次下降到：

\[
R(\tau)=1/e\approx 0.3679
\]

当前实现存在以下现象：

- 200 Hz 时，`tau0` 始终显示约 `5.00 ms`；
- 150 Hz 时，`tau0` 始终显示约 `6.67 ms`；
- 100 Hz 时，`tau0` 始终显示约 `10.00 ms`。

这些数值分别等于一个采样周期：

\[
\Delta t=1000/f_s
\]

根因是当前代码在 `lag == 1` 时，只要一阶自相关已经低于 `1/e`，就直接返回：

```cpp
return static_cast<double>(lag) * sampleIntervalMs;
```

因此程序把“真实相关时间小于当前时间分辨率”错误显示成“真实相关时间等于一个采样周期”。

当前程序还让 `tau0` 对完整 60 秒历史序列计算自相关，并设置：

```cpp
const int maxLag = sampleCount / 2;
```

这会引入不必要的大量计算，并让短时相关特征受到一分钟内慢漂移、曝光调整、ROI 变化和非平稳数据的影响。

---

## 3. 改造目标

完成以下改造：

1. `r0`、Seeing、`theta0` 继续使用现有 60 秒滚动窗口；
2. `tau0` 只使用 60 秒历史序列中最近 3 秒、且 Frame ID 连续的样本；
3. `tau0` 的最大自相关搜索延迟限制为 200 ms；
4. 采样周期使用相邻样本时间间隔的中位数，不使用简单平均值；
5. 当 `lag == 1` 已经低于 `1/e` 时，不再把一个采样周期当作精确测量值；
6. 增加明确的 `tau0` 状态字段：有效、欠分辨、时间分辨率；
7. 欠分辨时，界面显示：

   ```text
   < 5.00
   < 6.67
   < 10.00
   ```

   而不是显示等号意义上的 `5.00`、`6.67`、`10.00`；
8. CSV 中增加 `tau0` 状态与时间分辨率字段；
9. 不修改现有通信二进制协议，避免破坏外部接收端；
10. 不增加诊断输出，不构建项目。

---

## 4. 非目标与禁止事项

本任务不要进行以下操作：

- 不要缩短 `ATMOSPHERE_HISTORY_WINDOW_SECONDS = 60.0`；
- 不要把整个 `m_differentialHistory` 改成 3 秒；
- 不要改变 `r0`、Seeing、`theta0` 的公式；
- 不要改变双相机质心配对逻辑；
- 不要改变 Frame ID 对齐逻辑；
- 不要改变相机时间戳 Tick 常量；
- 不要修改 `CommManager` 的数据包字段数量或顺序；
- 不要为 `lag == 1` 做“伪精确”的线性插值；
- 不要用负数、特殊极大值或字符串偷偷编码欠分辨状态；
- 不要用 `NaN` 作为正常的内部状态传递方式；
- 不要添加新的设置界面；
- 不要增加配置文件字段；
- 不要添加日志、调试打印或性能计时；
- 不要构建、运行或测试。

---

## 5. 预计修改文件

必须检查并修改：

```text
src1分钟窗口/ImageProcessor.h
src1分钟窗口/ImageProcessor.cpp
src1分钟窗口/DIMM.cpp
```

根据实际代码组织，可能还需要检查：

```text
src1分钟窗口/DIMM.h
```

一般情况下，`DIMM.h` 中的运行时对象直接保存 `AtmosphericParams`，新增字段会自动随结构体存在，不需要额外修改。

不要修改其他历史目录，例如：

```text
src82/
src731质心算法/
```

---

# 6. 详细修改步骤

## 步骤 1：在 `ImageProcessor.h` 中增加 `Tau0Estimate`

找到 `AtmosphericParams` 定义。在它前面增加：

```cpp
struct Tau0Estimate {
    // 对于正常解析结果，valueMs 是测得的 1/e 相关时间。
    // 对于欠分辨结果，valueMs 保存当前采样周期上界，
    // 但必须同时设置 underResolved=true，不能把它解释为精确值。
    double valueMs = 0.0;

    // 当前短窗口的时间分辨率，通常接近 1000 / 实际采样频率。
    double resolutionMs = 0.0;

    bool valid = false;
    bool underResolved = false;
};
```

不要使用枚举，保持结构简单，减少调用链修改复杂度。

---

## 步骤 2：扩展 `AtmosphericParams`

当前结构大致为：

```cpp
struct AtmosphericParams {
    double r0 = 0.0;
    double seeing = 0.0;
    double theta0 = 0.0;
    double tau0 = 0.0;
    // ...
};
```

在 `tau0` 后增加：

```cpp
    bool tau0Valid = false;
    bool tau0UnderResolved = false;
    double tau0ResolutionMs = 0.0;
```

修改后相关部分应为：

```cpp
struct AtmosphericParams {
    double r0 = 0.0;
    double seeing = 0.0;
    double theta0 = 0.0;
    double tau0 = 0.0;
    bool tau0Valid = false;
    bool tau0UnderResolved = false;
    double tau0ResolutionMs = 0.0;

    double longitudinalVariancePx2 = 0.0;
    double transverseVariancePx2 = 0.0;
    double longitudinalVarianceRad2 = 0.0;
    double transverseVarianceRad2 = 0.0;
    double r0LongitudinalCm = 0.0;
    double r0TransverseCm = 0.0;
    quint64 sampleCount = 0;
};
```

---

## 步骤 3：增加 `tau0` 专用常量

在 `ImageProcessorWorker` 的私有静态常量区域，保留原有：

```cpp
static constexpr double ATMOSPHERE_HISTORY_WINDOW_SECONDS = 60.0;
```

在其后增加：

```cpp
static constexpr double TAU0_HISTORY_WINDOW_SECONDS = 3.0;
static constexpr double TAU0_MAX_LAG_MS = 200.0;
static constexpr int TAU0_MIN_SAMPLES = 30;
```

最终应同时存在：

```cpp
static constexpr double ATMOSPHERE_HISTORY_WINDOW_SECONDS = 60.0;
static constexpr double TAU0_HISTORY_WINDOW_SECONDS = 3.0;
static constexpr double TAU0_MAX_LAG_MS = 200.0;
static constexpr int TAU0_MIN_SAMPLES = 30;
```

不要删除或修改现有 60 秒窗口常量。

---

## 步骤 4：修改 `ImageProcessorWorker` 私有函数声明

找到现有声明：

```cpp
double estimateDifferentialAutocorrelationTimeMs(
    const QList<DifferentialSample>& samples) const;

double estimateScalarAutocorrelationCrossingMs(
    const QList<DifferentialSample>& samples,
    bool useLongitudinal) const;
```

替换为：

```cpp
QList<DifferentialSample> tau0WindowSamples(
    const QList<DifferentialSample>& samples) const;

Tau0Estimate estimateDifferentialAutocorrelationTimeMs(
    const QList<DifferentialSample>& samples) const;

Tau0Estimate estimateScalarAutocorrelationCrossingMs(
    const QList<DifferentialSample>& samples,
    bool useLongitudinal) const;
```

不要保留同名的 `double` 版本，避免重载和调用歧义。

---

## 步骤 5：扩展两个 `atmosphereReady` 信号

`ImageProcessor.h` 中通常有两处 `atmosphereReady`：

1. `ImageProcessorWorker` 的信号；
2. `ImageProcessor` 外层对象转发的信号。

找到两处现有签名，例如：

```cpp
void atmosphereReady(double r0,
                     double seeing,
                     double theta0,
                     double tau0,
                     double longitudinalVariancePx2,
                     double transverseVariancePx2,
                     double longitudinalVarianceRad2,
                     double transverseVarianceRad2,
                     double r0LongitudinalCm,
                     double r0TransverseCm,
                     quint64 sampleCount);
```

两处都统一改成：

```cpp
void atmosphereReady(double r0,
                     double seeing,
                     double theta0,
                     double tau0,
                     bool tau0Valid,
                     bool tau0UnderResolved,
                     double tau0ResolutionMs,
                     double longitudinalVariancePx2,
                     double transverseVariancePx2,
                     double longitudinalVarianceRad2,
                     double transverseVarianceRad2,
                     double r0LongitudinalCm,
                     double r0TransverseCm,
                     quint64 sampleCount);
```

参数顺序必须完全一致。后续所有 `emit` 和 lambda 都使用这个顺序。

---

# 7. `ImageProcessor.cpp` 算法修改

## 步骤 6：增加最近 3 秒连续样本提取函数

在 `calculateAtmosphere()` 后、`estimateDifferentialAutocorrelationTimeMs()` 前增加以下完整函数：

```cpp
QList<DifferentialSample> ImageProcessorWorker::tau0WindowSamples(
    const QList<DifferentialSample>& samples) const
{
    if (samples.isEmpty()) {
        return {};
    }

    int startIndex = 0;
    const qint64 newestTimestampMs = samples.last().timestampMs;

    if (newestTimestampMs > 0) {
        const qint64 cutoffTimestampMs =
            newestTimestampMs -
            static_cast<qint64>(TAU0_HISTORY_WINDOW_SECONDS * 1000.0);

        while (startIndex < samples.size() - 1 &&
               samples[startIndex].timestampMs > 0 &&
               samples[startIndex].timestampMs < cutoffTimestampMs) {
            ++startIndex;
        }
    } else {
        // 只有在主机时间戳不可用时，才使用目标帧率估算最近 3 秒样本数。
        double targetFrameRateHz = 0.0;
        {
            QMutexLocker locker(&m_mutex);
            targetFrameRateHz = m_targetFrameRateHz;
        }

        const int desiredSampleCount = std::max(
            TAU0_MIN_SAMPLES,
            static_cast<int>(std::lround(
                targetFrameRateHz * TAU0_HISTORY_WINDOW_SECONDS)));

        startIndex = std::max(0, samples.size() - desiredSampleCount);
    }

    // tau0 对时间连续性敏感。
    // 只保留最近一次 Frame ID 跳变之后的连续区间。
    for (int i = samples.size() - 1; i > startIndex; --i) {
        const DifferentialSample& previous = samples[i - 1];
        const DifferentialSample& current = samples[i];

        const bool camera1Gap =
            previous.frameId1 > 0 &&
            current.frameId1 > 0 &&
            current.frameId1 != previous.frameId1 + 1;

        const bool camera2Gap =
            previous.frameId2 > 0 &&
            current.frameId2 > 0 &&
            current.frameId2 != previous.frameId2 + 1;

        if (camera1Gap || camera2Gap) {
            startIndex = i;
            break;
        }
    }

    return samples.mid(startIndex);
}
```

### 实现要求

- 使用实际 `timestampMs` 选择最近 3 秒；
- 只有时间戳不可用时才使用 `m_targetFrameRateHz` 回退；
- 从最新样本向前检查 Frame ID；
- 发现跳变后，只使用跳变后的最新连续段；
- 不修改原始 `samples`；
- 不修改 `m_differentialHistory`；
- 不添加日志。

---

## 步骤 7：修改 `calculateAtmosphere()` 中的 `tau0` 赋值

找到：

```cpp
params.tau0 = estimateDifferentialAutocorrelationTimeMs(samples);
```

替换为：

```cpp
const Tau0Estimate tau0Estimate =
    estimateDifferentialAutocorrelationTimeMs(samples);

params.tau0 = tau0Estimate.valueMs;
params.tau0Valid = tau0Estimate.valid;
params.tau0UnderResolved = tau0Estimate.underResolved;
params.tau0ResolutionMs = tau0Estimate.resolutionMs;
```

完整逻辑应仍然位于：

```cpp
if (r0Zenith > 0.0) {
    // r0、seeing、theta0 原公式保持不变
    // tau0 改为结构化结果
}
```

不要改变其余三个参数的公式。

---

## 步骤 8：完整替换 `estimateDifferentialAutocorrelationTimeMs()`

将原来的 `double` 返回版本完整替换为：

```cpp
Tau0Estimate ImageProcessorWorker::estimateDifferentialAutocorrelationTimeMs(
    const QList<DifferentialSample>& allSamples) const
{
    const QList<DifferentialSample> samples = tau0WindowSamples(allSamples);

    if (samples.size() < TAU0_MIN_SAMPLES) {
        return {};
    }

    const Tau0Estimate longitudinalEstimate =
        estimateScalarAutocorrelationCrossingMs(samples, true);
    const Tau0Estimate transverseEstimate =
        estimateScalarAutocorrelationCrossingMs(samples, false);

    if (longitudinalEstimate.valid && transverseEstimate.valid) {
        // 任意一个方向欠分辨时，不再把两个方向平均成一个伪精确值。
        // 采用保守状态：整体结果标记为欠分辨。
        if (longitudinalEstimate.underResolved ||
            transverseEstimate.underResolved) {
            Tau0Estimate result;
            result.valid = true;
            result.underResolved = true;
            result.resolutionMs = std::max(
                longitudinalEstimate.resolutionMs,
                transverseEstimate.resolutionMs);
            result.valueMs = result.resolutionMs;
            return result;
        }

        Tau0Estimate result;
        result.valid = true;
        result.underResolved = false;
        result.valueMs = 0.5 *
            (longitudinalEstimate.valueMs + transverseEstimate.valueMs);
        result.resolutionMs = 0.5 *
            (longitudinalEstimate.resolutionMs +
             transverseEstimate.resolutionMs);
        return result;
    }

    if (longitudinalEstimate.valid) {
        return longitudinalEstimate;
    }

    if (transverseEstimate.valid) {
        return transverseEstimate;
    }

    return {};
}
```

### 合并规则说明

- 两个方向都正常解析：取平均；
- 两个方向中任意一个欠分辨：整体标记为欠分辨；
- 只有一个方向有效：使用该方向；
- 两个方向都无效：返回默认无效结果。

不要恢复旧的“两个正数就无条件平均”逻辑。

---

## 步骤 9：完整替换标量自相关函数

将原来的：

```cpp
double ImageProcessorWorker::estimateScalarAutocorrelationCrossingMs(...)
```

完整替换为下面版本：

```cpp
Tau0Estimate ImageProcessorWorker::estimateScalarAutocorrelationCrossingMs(
    const QList<DifferentialSample>& samples,
    bool useLongitudinal) const
{
    if (samples.size() < TAU0_MIN_SAMPLES) {
        return {};
    }

    std::vector<double> intervalsMs;
    intervalsMs.reserve(static_cast<std::size_t>(samples.size() - 1));

    for (int i = 1; i < samples.size(); ++i) {
        double dtMs = 0.0;

        if (samples[i].cameraTimestamp1 >
                samples[i - 1].cameraTimestamp1 &&
            samples[i - 1].cameraTimestamp1 > 0) {
            const quint64 dtTicks =
                samples[i].cameraTimestamp1 -
                samples[i - 1].cameraTimestamp1;

            dtMs = static_cast<double>(dtTicks) *
                   MARS_GIGE_TIMESTAMP_TICK_US /
                   1000.0;
        } else if (samples[i].cameraTimestamp2 >
                       samples[i - 1].cameraTimestamp2 &&
                   samples[i - 1].cameraTimestamp2 > 0) {
            const quint64 dtTicks =
                samples[i].cameraTimestamp2 -
                samples[i - 1].cameraTimestamp2;

            dtMs = static_cast<double>(dtTicks) *
                   MARS_GIGE_TIMESTAMP_TICK_US /
                   1000.0;
        } else if (samples[i].timestampMs >
                   samples[i - 1].timestampMs) {
            dtMs = static_cast<double>(
                samples[i].timestampMs -
                samples[i - 1].timestampMs);
        }

        if (std::isfinite(dtMs) && dtMs > 0.0) {
            intervalsMs.push_back(dtMs);
        }
    }

    if (intervalsMs.empty()) {
        return {};
    }

    const std::size_t middleIndex = intervalsMs.size() / 2;
    std::nth_element(intervalsMs.begin(),
                     intervalsMs.begin() +
                         static_cast<std::ptrdiff_t>(middleIndex),
                     intervalsMs.end());

    const double sampleIntervalMs = intervalsMs[middleIndex];
    if (!std::isfinite(sampleIntervalMs) || sampleIntervalMs <= 0.0) {
        return {};
    }

    const int sampleCount = static_cast<int>(samples.size());

    double mean = 0.0;
    for (const DifferentialSample& sample : samples) {
        mean += useLongitudinal
                    ? sample.longitudinal
                    : sample.transverse;
    }
    mean /= static_cast<double>(sampleCount);

    double varianceSum = 0.0;
    for (const DifferentialSample& sample : samples) {
        const double value = useLongitudinal
                                 ? sample.longitudinal
                                 : sample.transverse;
        const double centered = value - mean;
        varianceSum += centered * centered;
    }

    if (!std::isfinite(varianceSum) || varianceSum <= 0.0) {
        return {};
    }

    const int maxLagByTime = std::max(
        1,
        static_cast<int>(std::ceil(
            TAU0_MAX_LAG_MS / sampleIntervalMs)));

    const int maxLag = std::min(
        sampleCount / 2,
        maxLagByTime);

    if (maxLag < 1) {
        return {};
    }

    const double oneOverE = 1.0 / std::exp(1.0);
    double previousCorrelation = 1.0;

    for (int lag = 1; lag <= maxLag; ++lag) {
        double numerator = 0.0;

        for (int i = 0; i + lag < sampleCount; ++i) {
            const DifferentialSample& a = samples[i];
            const DifferentialSample& b = samples[i + lag];

            const double valueA = useLongitudinal
                                      ? a.longitudinal
                                      : a.transverse;
            const double valueB = useLongitudinal
                                      ? b.longitudinal
                                      : b.transverse;

            numerator += (valueA - mean) * (valueB - mean);
        }

        const double correlation =
            (numerator / static_cast<double>(sampleCount - lag)) /
            (varianceSum / static_cast<double>(sampleCount));

        if (!std::isfinite(correlation)) {
            continue;
        }

        if (correlation <= oneOverE) {
            Tau0Estimate result;
            result.valid = true;
            result.resolutionMs = sampleIntervalMs;

            if (lag == 1) {
                // 只能确定 tau0 小于或接近一个采样周期。
                // 不进行 lag=0 到 lag=1 的伪精确插值。
                result.underResolved = true;
                result.valueMs = sampleIntervalMs;
                return result;
            }

            const double denominator =
                previousCorrelation - correlation;

            if (!std::isfinite(denominator) ||
                std::abs(denominator) <= 1e-12) {
                result.underResolved = false;
                result.valueMs =
                    static_cast<double>(lag) * sampleIntervalMs;
                return result;
            }

            const double crossingLag =
                static_cast<double>(lag - 1) +
                (previousCorrelation - oneOverE) /
                    denominator;

            if (!std::isfinite(crossingLag) || crossingLag <= 0.0) {
                return {};
            }

            result.underResolved = false;
            result.valueMs = crossingLag * sampleIntervalMs;
            return result;
        }

        previousCorrelation = correlation;
    }

    // 200 ms 范围内没有找到 1/e 交点。
    // 当前任务不增加“超出最大搜索范围”状态，按无效结果处理。
    return {};
}
```

### 必须确认的头文件

`ImageProcessor.cpp` 当前通常已包含：

```cpp
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
```

如果缺少 `<cstddef>`，为 `std::ptrdiff_t` 增加：

```cpp
#include <cstddef>
```

不要添加其他无关头文件。

---

## 步骤 10：扩展 `emit atmosphereReady(...)`

在 `ImageProcessorWorker::processFrame()` 中找到：

```cpp
emit atmosphereReady(params.r0,
                     params.seeing,
                     params.theta0,
                     params.tau0,
                     params.longitudinalVariancePx2,
                     params.transverseVariancePx2,
                     params.longitudinalVarianceRad2,
                     params.transverseVarianceRad2,
                     params.r0LongitudinalCm,
                     params.r0TransverseCm,
                     params.sampleCount);
```

改成：

```cpp
emit atmosphereReady(params.r0,
                     params.seeing,
                     params.theta0,
                     params.tau0,
                     params.tau0Valid,
                     params.tau0UnderResolved,
                     params.tau0ResolutionMs,
                     params.longitudinalVariancePx2,
                     params.transverseVariancePx2,
                     params.longitudinalVarianceRad2,
                     params.transverseVarianceRad2,
                     params.r0LongitudinalCm,
                     params.r0TransverseCm,
                     params.sampleCount);
```

不要改变 `params.r0 > 0.0` 的原有发布判断。

---

## 步骤 11：确认 Worker 到外层的 connect

`ImageProcessor` 构造函数通常有：

```cpp
connect(m_worker,
        &ImageProcessorWorker::atmosphereReady,
        this,
        &ImageProcessor::atmosphereReady);
```

只要步骤 5 中两个信号的参数完全一致，这条 connect 不需要改写。

请静态检查两侧签名顺序一致，不要改成旧式字符串 connect。

---

# 8. `DIMM.cpp` 调用链修改

## 步骤 12：扩展 `setupAtmosphereProcessorConnection()` 的 lambda 参数

搜索：

```cpp
&ImageProcessor::atmosphereReady
```

找到接收大气参数的 lambda。当前参数开头大致为：

```cpp
[this](double r0,
       double seeing,
       double theta0,
       double tau0,
       double longitudinalVariancePx2,
       // ...
```

改成：

```cpp
[this](double r0,
       double seeing,
       double theta0,
       double tau0,
       bool tau0Valid,
       bool tau0UnderResolved,
       double tau0ResolutionMs,
       double longitudinalVariancePx2,
       double transverseVariancePx2,
       double longitudinalVarianceRad2,
       double transverseVarianceRad2,
       double r0LongitudinalCm,
       double r0TransverseCm,
       quint64 sampleCount)
```

在 lambda 中找到：

```cpp
runtime.latestAtmosphere.tau0 = tau0;
```

在其后增加：

```cpp
runtime.latestAtmosphere.tau0Valid = tau0Valid;
runtime.latestAtmosphere.tau0UnderResolved = tau0UnderResolved;
runtime.latestAtmosphere.tau0ResolutionMs = tau0ResolutionMs;
```

其余字段赋值保持原样。

---

## 步骤 13：修改界面 `tau0` 文本显示

搜索：

```cpp
ui->lblTauValue->setText
```

当前通常为：

```cpp
ui->lblTauValue->setText(
    QString::number(runtime.latestAtmosphere.tau0, 'f', 2));
```

替换为：

```cpp
const AtmosphericParams& atmosphere = runtime.latestAtmosphere;

if (!atmosphere.tau0Valid) {
    ui->lblTauValue->setText(QStringLiteral("--"));
} else if (atmosphere.tau0UnderResolved) {
    ui->lblTauValue->setText(
        QStringLiteral("< %1")
            .arg(atmosphere.tau0ResolutionMs, 0, 'f', 2));
} else {
    ui->lblTauValue->setText(
        QString::number(atmosphere.tau0, 'f', 2));
}
```

### 注意

- 不要使用目标脉冲频率重新计算显示值；
- 显示必须使用算法实际得到的 `tau0ResolutionMs`；
- 欠分辨状态显示 `< x.xx`；
- 无效状态显示 `--`；
- 正常解析状态仍显示数值。

如果当前函数中已经定义名为 `atmosphere` 的变量，换一个不冲突的名称，例如：

```cpp
const AtmosphericParams& latestAtmosphere = runtime.latestAtmosphere;
```

---

# 9. CSV 输出修改

## 步骤 14：扩展结果 CSV 表头

在 `DIMM.cpp` 中搜索表头片段：

```text
r0_cm,seeing_arcsec,theta0_arcsec,tau0_ms,
```

改为：

```text
r0_cm,seeing_arcsec,theta0_arcsec,tau0_ms,tau0_state,tau0_resolution_ms,
```

字段含义：

- `tau0_ms`
  - 正常解析时：解析值；
  - 欠分辨时：时间分辨率上界；
  - 无效时：空字符串；
- `tau0_state`
  - `resolved`
  - `under_resolved`
  - `invalid`
- `tau0_resolution_ms`
  - 算法实际使用的采样时间分辨率；
  - 无效时可留空。

---

## 步骤 15：扩展 CSV 数据行

在写入：

```cpp
QString::number(runtime.latestAtmosphere.tau0, 'f', 3),
```

之前或附近，先构造三个字符串。按照所在函数的局部变量风格添加：

```cpp
const AtmosphericParams& atmosphere = runtime.latestAtmosphere;

const QString tau0ValueText =
    atmosphere.tau0Valid
        ? QString::number(atmosphere.tau0, 'f', 3)
        : QString();

const QString tau0StateText =
    !atmosphere.tau0Valid
        ? QStringLiteral("invalid")
        : (atmosphere.tau0UnderResolved
               ? QStringLiteral("under_resolved")
               : QStringLiteral("resolved"));

const QString tau0ResolutionText =
    atmosphere.tau0Valid
        ? QString::number(atmosphere.tau0ResolutionMs, 'f', 3)
        : QString();
```

然后将原来单个 `tau0` 输出项：

```cpp
QString::number(runtime.latestAtmosphere.tau0, 'f', 3),
```

替换为连续三个字段：

```cpp
tau0ValueText,
tau0StateText,
tau0ResolutionText,
```

必须保证 CSV 表头字段数和每一行字段数同步增加 2 个。

如果文件中有多套 CSV 表头或多套结果写入路径，搜索所有：

```text
tau0_ms
latestAtmosphere.tau0
```

只修改属于测量结果 CSV 的路径。不要修改通信协议。

---

# 10. 网络通信保持兼容

当前 `CommManager::sendMeasurement(...)` 只发送数值型 `tau0`。

本任务中：

- 不修改 `CommManager` 函数签名；
- 不增加状态字段；
- 不改变二进制数据包长度和字段顺序；
- 正常解析时继续发送 `tau0`；
- 欠分辨时继续发送 `tau0` 中保存的时间分辨率上界。

原因：修改网络协议会影响外部接收端，超出本任务范围。

在最终修改说明中明确指出：

> UI 和 CSV 能区分欠分辨状态；现有网络协议为兼容性仍只发送数值上界。

---

# 11. 图表行为

如果现有图表使用：

```cpp
runtime.latestAtmosphere.tau0
```

则保持现有图表接口不变：

- 正常解析时绘制解析值；
- 欠分辨时绘制时间分辨率上界；
- 无效时应避免绘制无意义的 0；如果当前代码在 `hasValidAtmosphere` 下无条件绘制，不要求额外重构。

本任务不要增加新的图表状态样式、虚线、颜色或图例。

---

# 12. 保持 60 秒发布门槛不变

当前程序在 `processFrame()` 中通常有：

```cpp
if (m_differentialHistory.size() < minimumAtmosphereSamples()) {
    // return
}
```

而：

```cpp
int ImageProcessorWorker::minimumAtmosphereSamples() const
{
    return historyWindowSize();
}
```

本任务不要修改这一发布门槛。

这意味着：

- 四个参数仍然在完整 60 秒历史窗口准备好以后统一发布；
- `tau0` 虽然只使用最后 3 秒数据计算，但不会提前到第 3 秒单独显示；
- 这样可以避免增加独立信号、独立定时器和额外 UI 状态。

---

# 13. 静态检查清单

由于本任务禁止构建，完成修改后只做人工静态检查。

## 13.1 函数签名

确认以下函数没有同时存在旧版 `double` 和新版 `Tau0Estimate`：

```cpp
estimateDifferentialAutocorrelationTimeMs
estimateScalarAutocorrelationCrossingMs
```

确认声明与定义完全一致。

## 13.2 信号参数

确认以下位置的参数数量和顺序完全一致：

1. `ImageProcessorWorker::atmosphereReady`；
2. `ImageProcessor::atmosphereReady`；
3. `emit atmosphereReady(...)`；
4. `DIMM::setupAtmosphereProcessorConnection()` 的 lambda。

统一顺序必须为：

```text
r0
seeing
theta0
tau0
tau0Valid
tau0UnderResolved
tau0ResolutionMs
longitudinalVariancePx2
transverseVariancePx2
longitudinalVarianceRad2
transverseVarianceRad2
r0LongitudinalCm
r0TransverseCm
sampleCount
```

## 13.3 60 秒窗口

确认以下值未被修改：

```cpp
ATMOSPHERE_HISTORY_WINDOW_SECONDS = 60.0
```

确认：

```cpp
m_differentialHistory
```

仍然按 `historyWindowSize()` 保存 60 秒目标样本数。

## 13.4 `tau0` 短窗口

确认 `tau0` 计算调用链是：

```text
calculateAtmosphere(60 秒历史)
  → estimateDifferentialAutocorrelationTimeMs
  → tau0WindowSamples
  → 最近 3 秒连续样本
  → 两个方向的有限 lag 自相关
```

## 13.5 最大 lag

确认已删除：

```cpp
const int maxLag = sampleCount / 2;
```

作为唯一限制的旧实现。

新实现必须同时受以下限制：

```cpp
sampleCount / 2
TAU0_MAX_LAG_MS / sampleIntervalMs
```

## 13.6 欠分辨逻辑

确认不存在：

```cpp
if (lag == 1) {
    return static_cast<double>(lag) * sampleIntervalMs;
}
```

新逻辑必须设置：

```cpp
result.valid = true;
result.underResolved = true;
result.valueMs = sampleIntervalMs;
result.resolutionMs = sampleIntervalMs;
```

## 13.7 无诊断输出

搜索本次改动，确认没有新增：

```text
qDebug
qInfo
qWarning
std::cout
printf
日志文件字段
诊断信号
```

## 13.8 CSV

确认：

- 表头增加了 2 个字段；
- 每一行也增加了 2 个字段；
- 字段顺序一致；
- 欠分辨状态写为 `under_resolved`；
- 无效状态写为 `invalid`。

---

# 14. 预期行为

修改完成后，若第一阶自相关已经低于 `1/e`：

| 实际采样率 | 时间分辨率 | UI 显示 |
|---:|---:|---:|
| 200 Hz | 约 5.00 ms | `< 5.00` |
| 150 Hz | 约 6.67 ms | `< 6.67` |
| 100 Hz | 约 10.00 ms | `< 10.00` |

CSV 示例：

```csv
...,tau0_ms,tau0_state,tau0_resolution_ms,...
...,5.000,under_resolved,5.000,...
```

若自相关在后续 lag 才穿过 `1/e`，例如插值得到 `18.42 ms`：

UI：

```text
18.42
```

CSV：

```csv
...,18.420,resolved,5.000,...
```

若最近连续样本不足 30 个、方差无效或 200 ms 内没有交点：

UI：

```text
--
```

CSV：

```csv
...,,invalid,,...
```

---

# 15. 性能预期

以 200 Hz 为例：

- 60 秒历史：约 12000 个样本，继续用于方差参数；
- `tau0` 短窗口：约 600 个样本；
- 最大 lag：约 `200 / 5 = 40`；
- 每个方向的主要计算量约为 `600 × 40` 量级；
- 两个方向合计远小于原来对 12000 个样本搜索约 6000 个 lag 的计算量。

不要在任务中做进一步 FFT 优化。当前直接自相关在 3 秒、200 ms 最大 lag 条件下已经足够轻量。

---

# 16. 完成后需要提交的说明

Agent 完成源码修改后，只输出以下内容：

1. 修改了哪些文件；
2. 每个文件修改了什么；
3. 是否保持 60 秒 `r0/seeing/theta0` 窗口不变；
4. 是否将 `tau0` 改为最近 3 秒连续样本；
5. 是否限制最大 lag 为 200 ms；
6. 是否使用中位数采样周期；
7. 是否实现 `< 时间分辨率` 的欠分辨显示；
8. 是否同步扩展 CSV；
9. 明确说明没有增加诊断输出；
10. 明确说明没有构建、没有运行测试。

不要声称代码已经通过编译或实机验证。

---

# 17. 最终验收标准

满足以下全部条件才算任务完成：

- [ ] 只修改 `src1分钟窗口/`；
- [ ] 60 秒历史窗口保持不变；
- [ ] `r0`、Seeing、`theta0` 公式不变；
- [ ] `tau0` 使用最近 3 秒样本；
- [ ] `tau0` 只使用最近连续 Frame ID 段；
- [ ] 采样周期使用中位数；
- [ ] 最大自相关延迟为 200 ms；
- [ ] `lag == 1` 时标记为欠分辨；
- [ ] UI 显示 `< x.xx`；
- [ ] 无效结果显示 `--`；
- [ ] CSV 增加状态与时间分辨率字段；
- [ ] 网络协议未修改；
- [ ] 没有新增诊断输出；
- [ ] 没有构建；
- [ ] 没有运行测试；
- [ ] 最终说明不虚构编译或验证结果。
