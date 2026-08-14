# 自动曝光状态机设计说明

日期：2026-07-28
状态：规格草案，已采用方案 B：状态机保护型自动曝光

## 背景

本功能的目标不是追踪每一帧的亮度，而是保护 DIMM seeing 长时间测量不中断。实际观测中天气透明度会变化，星点也会因为大气闪烁频繁变亮或变暗。自动曝光应当防止两类失败：

- 光斑过曝，导致质心计算被饱和核心拉偏。
- 光斑过暗，导致星点被噪声淹没，质心失效。

用户已确认两台相机观测同一个目标，正常情况下两台相机亮度趋势不应有很大差异。因此自动曝光必须按两台相机的共同趋势决策，两台相机始终使用同一个曝光值和同一档热像素模板。

## 当前自动曝光逻辑检查

当前代码里已有一个很轻量的自动曝光入口：

- `src/AppConfig.h` 中 `AutoExposureConfig` 只有开关、峰值上下阈值、亮暗比例和曝光上下限。
- `src/SettingsDialog.cpp` 中已有自动曝光设置组。
- `src/DIMM.cpp` 中 `applyAutoExposure(int cameraIndex, double peakValue)` 在 ROI 质心可用之后收集峰值。
- 当前默认检查间隔为 4 小时，样本上限为 4096 帧。
- 当前逻辑只在 `centroidReady` 后采集自动曝光样本。也就是说，如果已经看不到星点、质心无效，自动曝光无法完整统计丢星状态。
- 当前调整逻辑使用两台相机峰值中位数，并根据上下阈值按比例寻找目标曝光模板。
- 当前在线切换使用 `CameraManager::setExposure()` 和 `ImageProcessor::configureHotPixelTemplates()`，采集流程没有显式停止，但结果 CSV 没有记录曝光、模板、自动曝光状态和调整序号。

当前逻辑的主要问题：

- 4 小时检查太慢，不能保护天气变化。
- 4096 帧样本在 200 fps 下只覆盖约 20 秒，不等于长期趋势。
- 单纯峰值阈值容易被闪烁触发。
- 没有状态机、迟滞、冷却和过暗报警。
- 没有无效质心帧的自动曝光统计，无法可靠判断 `STAR_LOST`。
- 调整期间的结果没有足够元数据，后处理无法识别曝光和模板切换附近的帧。

## 用户确认的硬约束

- 不使用相机 SDK 自带 `ExposureAuto` 作为正式测量中的自动曝光。
- 不停止采集。
- 曝光调整期间的帧继续进入 `r0/seeing` 计算。
- 曝光调整期间的帧继续保存到结果文件。
- 两台相机共享同一个曝光值。
- 两台相机共享同一档热像素模板。
- 自动曝光按两台相机共同趋势决策，不让某一台相机独立拉动曝光。
- 当天气太差、最大曝光仍无法稳定观测星点时，需要明确报错。
- 所有关键阈值、时间窗口、冷却和调整限制都需要做成可调接口。

## 非目标

- 不实现 SDK `ExposureAuto=Continuous`。
- 不让两台相机各自独立自动曝光。
- 不为了自动曝光暂停采集、重启采集或清空 seeing 历史。
- 不追求每帧亮度都处于最佳值。
- 不把短时闪烁当成立即调曝光的理由。
- 不在本功能中重新设计 r0/seeing 公式。

## 总体方案

自动曝光改为状态机保护器。它持续旁路监听 ROI 亮度和质量指标，根据一段时间窗口内的共同趋势决定是否调曝光。

核心思想：

```text
ROI frame stream
-> hot-pixel corrected ROI
-> centroid and brightness metrics
-> per-camera rolling window
-> two-camera common trend
-> auto-exposure state machine
-> online exposure/template adjustment
-> keep r0/seeing and saving uninterrupted
```

自动曝光只是一条旁路控制链路，不得阻断图像处理链路。

## 状态机

状态定义：

```text
NORMAL
BRIGHT_WARNING
BRIGHT_ADJUSTING
DARK_WARNING
DARK_ADJUSTING
COOLDOWN
STAR_LOST
TREND_CONFLICT
```

状态含义：

- `NORMAL`：亮度和质量处于安全区间，只监控。
- `BRIGHT_WARNING`：共同趋势显示接近饱和，但尚未满足调整条件。
- `BRIGHT_ADJUSTING`：持续过亮，正在执行一次调暗动作。
- `DARK_WARNING`：共同趋势显示偏暗，但仍能观测星点。
- `DARK_ADJUSTING`：持续偏暗，正在执行一次调亮动作。
- `COOLDOWN`：刚完成一次曝光和模板切换，暂时只监控不再次调整。
- `STAR_LOST`：天气过差，最大曝光仍无法稳定观测星点。
- `TREND_CONFLICT`：两台相机趋势明显冲突，暂缓自动曝光调整。

基本流转：

```text
NORMAL
  -> BRIGHT_WARNING      亮度窗口持续接近饱和
  -> DARK_WARNING        亮度窗口持续偏暗但仍有星点
  -> STAR_LOST           最大曝光下长时间丢星
  -> TREND_CONFLICT      两台相机趋势长期冲突

BRIGHT_WARNING
  -> BRIGHT_ADJUSTING    过亮持续时间达标
  -> NORMAL              回到安全区间并持续稳定

DARK_WARNING
  -> DARK_ADJUSTING      过暗持续时间达标
  -> STAR_LOST           最大曝光下仍长期不可见
  -> NORMAL              回到安全区间并持续稳定

BRIGHT_ADJUSTING
  -> COOLDOWN            完成一次调暗和模板切换

DARK_ADJUSTING
  -> COOLDOWN            完成一次调亮和模板切换

COOLDOWN
  -> NORMAL              冷却结束且安全
  -> BRIGHT_WARNING      冷却结束后仍偏亮
  -> DARK_WARNING        冷却结束后仍偏暗
  -> STAR_LOST           冷却结束后仍最大曝光丢星

TREND_CONFLICT
  -> NORMAL              两台趋势恢复一致且稳定
```

## 采样指标

每台相机每个 ROI 帧都应产生一条自动曝光采样。即使质心无效，也要尽量输出亮度和噪声指标，用于判断过暗和丢星。

每帧指标：

- `cameraIndex`
- `frameId`
- `timestampMs`
- `peakDn`：ROI 光斑峰值，使用热像素校正后的 Mono12 DN。
- `fitPeakDn`：拟合光斑强度峰值。如果当前未启用或拟合失败，则使用 `peakDn`。
- `backgroundDn`
- `noiseSigmaDn`
- `snr = (controlPeakDn - backgroundDn) / noiseSigmaDn`
- `thresholdDn`
- `signalPixelCount`
- `saturatedPixelCount`：raw ROI 中大于等于硬饱和阈值的像素数量。
- `centroidValid`
- `measurementUsable`

控制峰值：

```text
controlPeakDn = useFittedPeak && fitPeakDn finite && fitPeakDn > 0
              ? fitPeakDn
              : peakDn
```

过曝主要看固定 DN 和饱和像素。过暗主要看 SNR、有效质心比例和丢星比例。

## 两台相机共同趋势

两台相机各自维护滚动窗口，再合成共同趋势。

每台窗口统计：

- `p50PeakDn`
- `p90PeakDn`
- `p95PeakDn`
- `medianSnr`
- `validCentroidRatio`
- `measurementUsableRatio`
- `saturationFrameRatio`
- `lostFrameRatio`

两台相机一致时，合成规则：

```text
sharedPeakP90 = mean(cam0.p90PeakDn, cam1.p90PeakDn)
sharedPeakP50 = mean(cam0.p50PeakDn, cam1.p50PeakDn)
sharedSnr     = mean(cam0.medianSnr, cam1.medianSnr)
sharedValidRatio = min(cam0.validCentroidRatio, cam1.validCentroidRatio)
sharedSaturationRatio = max(cam0.saturationFrameRatio, cam1.saturationFrameRatio)
```

趋势冲突规则：

```text
relativeDifference = abs(cam0.p50PeakDn - cam1.p50PeakDn) / max(meanPeak, 1)
```

当 `relativeDifference > cameraAgreementRatio` 且持续超过 `trendConflictPersistenceSec`，进入 `TREND_CONFLICT`。在该状态下不调曝光，只记录报警，避免 ROI 偏移、热像素模板错误或单台相机异常导致曝光误调。

## 过曝判断

过曝分为接近饱和和硬饱和。

接近饱和：

```text
sharedPeakP90 >= nearSaturationDn
or sharedSaturationRatio >= brightFrameRatioThreshold
```

硬饱和：

```text
any camera saturatedPixelCount >= saturatedPixelCount
or any camera p95PeakDn >= hardSaturationDn
```

触发调暗：

```text
BRIGHT_WARNING 持续 >= brightPersistenceSec
并且不在 COOLDOWN
并且两台相机趋势不冲突
```

调暗目标：

```text
targetExposure = currentExposure * clamp(targetPeakHighDn / sharedPeakP90,
                                         maxExposureChangeRatioDown,
                                         1.0)
```

最终目标曝光必须映射到可用热像素模板档位，并受 `maxTemplateStepPerAdjust` 限制。

## 过暗判断和天气过差报错

过暗分三层。

第一层：偏暗但还能测。

```text
sharedSnr <= darkSnrWarning
or sharedValidRatio < minValidCentroidRatio
```

进入 `DARK_WARNING`，但不立即调曝光。

第二层：持续偏暗，需要调亮。

```text
DARK_WARNING 持续 >= darkPersistenceSec
并且 currentExposure < maxExposureUs
并且不在 COOLDOWN
```

调亮目标：

```text
targetExposure = currentExposure * clamp(targetPeakLowDn / max(sharedPeakP50, 1),
                                         1.0,
                                         maxExposureChangeRatioUp)
```

第三层：天气过差，已经观测不到星点。

```text
currentExposure >= maxExposureUs
and sharedValidRatio <= starLostValidRatio
and STAR_LOST 条件持续 >= starLostPersistenceSec
```

进入 `STAR_LOST` 并报错：

```text
WEATHER_TOO_DARK / STAR_LOST
```

该状态不停止采集。帧继续进入 `r0/seeing` 和保存，但结果中必须标记当前 AE 状态和过暗原因。

## 稳定性机制

为了避免星点闪烁导致频繁调曝光，状态机必须同时使用以下机制：

- 长窗口统计：默认 60 秒。
- 分位数而非单帧最大值：过曝主要看 p90/p95。
- 持续时间：过亮默认 30 秒，过暗默认 60 秒。
- 帧比例阈值：窗口内足够多的帧异常才触发。
- 冷却：每次调整后默认 180 秒不再次调整。
- 单步限幅：每次最多调一档热像素模板，或受曝光比例上下限约束。
- 迟滞退出：回到安全区间并持续 `safePersistenceSec` 才回到 `NORMAL`。

## 可调参数接口

所有参数放入 `AutoExposureConfig`，并在设置窗口提供接口。

| 参数 | 默认值 | 校验 | 用途 |
| --- | ---: | --- | --- |
| `enabled` | `false` | 布尔 | 自动曝光总开关 |
| `useFittedPeak` | `false` | 布尔 | 是否优先使用拟合峰值 |
| `targetPeakLowDn` | `3200` | `0..4095` | 目标安全峰值下界 |
| `targetPeakHighDn` | `3600` | `targetPeakLowDn..4095` | 目标安全峰值上界 |
| `nearSaturationDn` | `3800` | `targetPeakHighDn..4095` | 接近饱和阈值 |
| `hardSaturationDn` | `4090` | `nearSaturationDn..4095` | 硬饱和阈值 |
| `saturatedPixelCount` | `1` | `>= 1` | 判定硬饱和的像素数 |
| `darkSnrWarning` | `8.0` | `> darkSnrCritical` | 偏暗报警 SNR |
| `darkSnrCritical` | `5.0` | `> 0` | 严重偏暗 SNR |
| `minValidCentroidRatio` | `0.50` | `0..1` | 偏暗有效质心比例 |
| `starLostValidRatio` | `0.10` | `0..minValidCentroidRatio` | 丢星有效质心比例 |
| `brightFrameRatioThreshold` | `0.30` | `0..1` | 过亮帧比例阈值 |
| `darkFrameRatioThreshold` | `0.50` | `0..1` | 过暗帧比例阈值 |
| `sampleWindowSec` | `60` | `>= 10` | 滚动统计窗口 |
| `brightPersistenceSec` | `30` | `>= 1` | 过亮持续时间 |
| `darkPersistenceSec` | `60` | `>= 1` | 过暗持续时间 |
| `starLostPersistenceSec` | `120` | `>= darkPersistenceSec` | 丢星报警持续时间 |
| `trendConflictPersistenceSec` | `30` | `>= 1` | 两台趋势冲突持续时间 |
| `safePersistenceSec` | `60` | `>= 1` | 回到正常所需安全持续时间 |
| `cooldownSec` | `180` | `>= 0` | 调整后冷却时间 |
| `minExposureUs` | `500` | `> 0` | 最小曝光 |
| `maxExposureUs` | `20000` | `>= minExposureUs` | 最大曝光 |
| `maxTemplateStepPerAdjust` | `1` | `>= 1` | 单次最多跨越模板档数 |
| `maxExposureChangeRatioUp` | `1.30` | `>= 1` | 单次调亮比例上限 |
| `maxExposureChangeRatioDown` | `0.70` | `0..1` | 单次调暗比例下限 |
| `cameraAgreementRatio` | `0.50` | `> 0` | 两台峰值相对差异上限 |

## 曝光和热像素模板切换

自动曝光调整必须在线完成：

1. 根据目标曝光选择对应热像素模板档位。
2. 预检查两台相机的 mask/excess 文件是否存在。
3. 记录 `aeSequenceId`、目标曝光、调整方向和原因。
4. 对两台相机设置相同曝光。
5. 立即配置同一目标档位的热像素模板。
6. 进入 `COOLDOWN`。

不允许在该流程中调用停止采集、重启采集、清空 seeing 历史或丢弃调整期间帧。

曝光设置和模板切换不能保证在物理上同一帧完全同步。因此结果文件必须记录：

- 当前曝光。
- 当前热像素模板曝光档位。
- 自动曝光状态。
- 自动曝光调整序号。
- 距离最近一次调整的帧数或时间。

## 结果保存

结果 CSV 需要新增自动曝光诊断列。建议列：

```text
camera1_peak_dn
camera2_peak_dn
camera1_snr
camera2_snr
camera1_valid_ratio
camera2_valid_ratio
exposure_us
hot_pixel_template_exposure_us
ae_enabled
ae_state
ae_reason
ae_sequence_id
ae_target_exposure_us
ae_frames_since_adjust
```

`ae_reason` 写 CSV 前要移除英文逗号或替换为分号，因为当前 `ResultWriter` 是简单逗号拼接。

## UI 和状态显示

设置窗口应保留当前自动曝光组，并扩展为高级参数接口。参数较多时可以在同一 `QGroupBox` 内继续用 `QFormLayout`，但标签要明确区分：

- 峰值目标。
- 过曝判断。
- 过暗/丢星判断。
- 时间窗口。
- 调整限制。
- 双相机一致性。

主界面状态消息需要显示关键状态：

- 自动曝光已启用。
- 当前状态。
- 当前曝光和模板档位。
- 最近一次调整方向和目标曝光。
- `STAR_LOST` 或 `TREND_CONFLICT` 报警。

## 验收标准

- 正常亮度下长时间保持 `NORMAL`，不调整曝光。
- 短时闪烁偶尔触及阈值时不调整曝光。
- 持续接近饱和时进入 `BRIGHT_WARNING`，满足持续时间后调暗。
- 持续偏暗时进入 `DARK_WARNING`，满足持续时间后调亮。
- 最大曝光下仍长期无有效星点时进入 `STAR_LOST` 并报 `WEATHER_TOO_DARK / STAR_LOST`。
- 两台相机趋势明显冲突时进入 `TREND_CONFLICT`，不调曝光。
- 曝光调整期间采集不中断。
- 曝光调整期间帧继续进入 `r0/seeing` 计算和保存。
- 结果 CSV 能追踪 AE 状态、曝光、模板档位和调整原因。
- 不使用 SDK 自带自动曝光。
- 不修改 CMake。

