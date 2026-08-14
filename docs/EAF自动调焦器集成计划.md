# DIMM「src观星版」EAF 自动调焦器集成计划

> 目标读者：Codex / C++ 开发者  
> 目标工程：`Tolight1/DIMM` 仓库中的 `src观星版`  
> 技术栈：Windows x64、MSVC 2022、C++17、Qt 6 Widgets、CMake  
> SDK：ZWO EAF Focuser SDK v1.8.1

---

## 1. 项目目标

在现有 DIMM 软件的“设置”窗口中新增一个 **“自动调焦”** 页签，为两套望远镜分别管理一台 ZWO EAF 调焦器，实现：

1. 枚举并识别最多两台 USB EAF；
2. 将物理 EAF 稳定映射到“望远镜1 / 相机1”和“望远镜2 / 相机2”；
3. 查看当前位置、目标位置、最大位置、步进范围、温度、运动状态、手柄状态、错误状态等；
4. 执行绝对移动、相对步进、停止、位置重置；
5. 设置反向、回差、最大位置、蜂鸣器、LED；
6. 在设备拔出、SDK 缺失、设备忙、手柄控制、越界等情况下安全降级；
7. 不阻塞 Qt GUI，不影响现有双相机采集、对准、ROI 跟踪和 r0 计算；
8. 为后续“基于星点 HFR/FWHM 的闭环自动对焦”预留接口。

### 1.1 本次必须完成的范围

本次 Codex 必须完成的是 **EAF 设备控制、双设备映射、状态监控和设置页 UI**。

这里的“自动调焦器”首先指 EAF 电动调焦硬件。SDK 本身提供的是位置和设备控制接口，没有暴露一个可直接调用的“自动寻找最佳焦点”接口。因此不要把简单的电机移动伪装成闭环自动对焦。

### 1.2 第二阶段范围

基于相机星点图像进行焦点评分、扫描和拟合的闭环自动对焦，作为第二阶段实现，详见第 13 节。第一阶段完成时必须预留清晰接口，但不得让未完成的自动对焦按钮误导用户。

### 1.3 暂不实现

第一阶段不实现：

- 蓝牙扫描、连接、配对和断开；
- EAF 固件升级；
- 蓝牙名称修改；
- Shipping Mode；
- 同时对两台 EAF 执行自动焦点扫描；
- 实时 DIMM 测量过程中自动移动焦点。

第一阶段使用 USB 通信。蓝牙相关 API 保留在设计文档中，但不进入 UI 和运行逻辑。

---

## 2. 已确认的 SDK 行为

### 2.1 USB 建连顺序

必须按以下顺序使用：

```text
EAFGetNum()
  -> 对每个 index 调用 EAFGetID(index, &id)
  -> EAFOpen(id)
  -> 读取属性与状态
  -> 执行控制
  -> EAFClose(id)
```

关键规则：

- `EAFGetNum()` 是 USB 模式第一个调用的 API，同时会刷新设备列表；
- 多设备通过 `index = 0 ... count - 1` 获取各自 ID；
- 设备成功 `EAFOpen()` 后才能进行后续控制；
- 后续绝大多数 API 都以 ID 为目标；
- 不再使用设备时调用 `EAFClose()`。

### 2.2 运动相关接口

```cpp
EAF_ERROR_CODE EAFMove(int ID, int iStep);
EAF_ERROR_CODE EAFStop(int ID);
EAF_ERROR_CODE EAFStopAndWait(int ID, int timeoutMs = 1000);
EAF_ERROR_CODE EAFIsMoving(int ID, bool* moving, bool* handControl);
EAF_ERROR_CODE EAFGetPosition(int ID, int* position);
EAF_ERROR_CODE EAFResetPostion(int ID, int position);
```

注意：

- `EAFMove` 是 **绝对位置移动**，不是相对步进；
- 合法目标位置为 `0 <= target <= MaxStep`；
- 相对移动必须由程序读取当前位置后计算绝对目标；
- `EAFIsMoving` 同时返回是否由手柄控制；
- 手柄控制移动时，SDK 文档说明不能通过 `EAFStop()` 停止；
- `EAFStopAndWait` 是 v1.8.1 新增接口，超时仍未停止时返回 `EAF_ERROR_MOVING`。

### 2.3 位置与机械参数

```cpp
EAF_ERROR_CODE EAFGetMaxStep(int ID, int* maxStep);
EAF_ERROR_CODE EAFSetMaxStep(int ID, int maxStep);
EAF_ERROR_CODE EAFStepRange(int ID, int* stepRange);
EAF_ERROR_CODE EAFGetReverse(int ID, bool* reverse);
EAF_ERROR_CODE EAFSetReverse(int ID, bool reverse);
EAF_ERROR_CODE EAFGetBacklash(int ID, int* backlash);
EAF_ERROR_CODE EAFSetBacklash(int ID, int backlash);
```

约束：

- 回差合法范围为 `0 ~ 255`；
- 获取/设置最大位置和步进范围时，SDK 可能返回 `EAF_ERROR_MOVING`，必须等待设备停止；
- 修改最大位置前必须确认当前位置不超过新最大位置；
- 重置当前位置和最大位置都属于高风险操作，UI 必须二次确认。

### 2.4 状态与设备信息

需要使用：

```cpp
EAFGetProperty
EAFGetTemp
EAFGetBeep / EAFSetBeep
EAFGetLedState / EAFSetLedState
EAFGetFirmwareVersion
EAFGetSerialNumber
EAFGetType
EAFGetErrorCode
EAFGetNumOfControls
EAFGetControlCaps
EAFGetSDKVersion
```

补充规则：

- `EAFGetTemp` 在手柄移动期间可能返回不可用值，文档提到可能为 `-273` 并返回错误；UI 应显示 `--`，不能显示成真实温度；
- `EAFGetControlCaps` 用于判断 LED、电池、错误信息等能力是否受支持；
- 不支持的功能应隐藏或禁用，而不是不断弹出错误；
- `EAFGetErrorCode` 可返回电机堵转等设备错误，应直接显示在状态区。

### 2.5 SDK 错误处理

至少映射以下错误：

```text
EAF_SUCCESS
EAF_ERROR_INVALID_INDEX
EAF_ERROR_INVALID_ID
EAF_ERROR_INVALID_VALUE
EAF_ERROR_REMOVED
EAF_ERROR_MOVING
EAF_ERROR_ERROR_STATE
EAF_ERROR_GENERAL_ERROR
EAF_ERROR_NOT_SUPPORTED
EAF_ERROR_CLOSED
EAF_ERROR_BATTER_INFO
EAF_ERROR_INVALID_LENGTH
```

每次 SDK 调用必须检查返回值。禁止忽略错误后继续更新 UI 为“成功”。

---

## 3. 当前工程适配原则

### 3.1 现有结构

当前工程已经有：

- `CameraManager`：双相机管理；
- `ImageProcessor`：ROI、质心和大气参数计算；
- `PulseGeneratorManager`：硬件控制管理类；
- `SettingsDialog`：在 `DIMM.cpp` 中以代码动态创建多个 `QTabWidget` 页签；
- `DIMM`：拥有管理器、设置窗口、采集状态和 UI 状态。

EAF 集成应沿用“管理器 + 独立控件 + DIMM 负责总协调”的模式，不要继续把所有 EAF 控件和 SDK 调用直接堆入 `DIMM.cpp`。

### 3.2 源文件路径问题

仓库实际文件位于 `src观星版/` 根目录，但当前 `CMakeLists.txt` 的 `PROJECT_SOURCES` 使用了 `src/DIMM.cpp` 等路径。Codex 开始前必须先确认真实构建目录：

- 若实际源文件确实位于 `src观星版/` 根目录，则统一修正 CMake 路径；
- 若本地工作区另有 `src观星版/src/`，则以实际可编译布局为准；
- 不允许为了绕过问题复制出两套同名源文件。

### 3.3 现有功能不得回归

EAF 功能不得改动以下核心算法：

- 星点初始定位；
- 对准模式；
- ROI 跟踪；
- 双相机帧处理；
- r0、seeing、theta0、tau0 计算；
- 脉冲发生器协议；
- 相机 SDK 回调。

只允许增加必要的采集状态互锁和焦点移动后的处理状态重置。

---

## 4. SDK 文件和构建前置条件

### 4.1 当前缺失的 Windows 二进制

现有上传内容包括：

- `EAF_focuser.h`；
- Linux/macOS 示例 `Makefile`；
- 示例 `main.cpp`；
- 一个 Linux ELF 的 `test_console`。

这些不足以构建 Windows 版 DIMM。还必须从 ZWO Windows x64 SDK 包取得：

```text
EAF_focuser.dll
```

若选择传统链接方式，还需要：

```text
EAF_focuser.lib
```

本计划推荐通过 `QLibrary` 动态加载 DLL，因此第一阶段不强制链接 `.lib`。无论采用哪种方式，都必须使用与程序架构一致的 Windows x64 SDK，不能使用上传的 Linux `test_console` 或 `libEAFFocuser.so`。

### 4.2 推荐目录

```text
src观星版/
  third_party/
    eaf/
      include/
        EAF_focuser.h
      bin/
        win64/
          EAF_focuser.dll
```

不要修改厂商头文件。厂商二进制能否提交到仓库由许可证决定；如果不提交，则在 README 中说明复制位置。

---

## 5. 总体架构

新增四个模块：

```text
EafSdkLoader.h/.cpp
EafFocuserManager.h/.cpp
FocuserControlWidget.h/.cpp
AutoFocusController.h/.cpp       # 第二阶段
FocusMetricCalculator.h/.cpp     # 第二阶段
```

### 5.1 EafSdkLoader

职责：

- 使用 `QLibrary` 从应用程序目录加载 `EAF_focuser.dll`；
- 解析第一阶段所需函数；
- 提供 `isLoaded()`、`loadError()` 和函数指针；
- DLL 缺失时 DIMM 仍能正常启动，仅禁用“自动调焦”页；
- 不允许在业务代码中到处直接调用 `QLibrary::resolve()`。

必须解析的符号：

```text
EAFGetNum
EAFGetID
EAFOpen
EAFGetProperty
EAFGetNumOfControls
EAFGetControlCaps
EAFMove
EAFStop
EAFStopAndWait
EAFIsMoving
EAFGetPosition
EAFResetPostion
EAFGetTemp
EAFSetBeep
EAFGetBeep
EAFSetMaxStep
EAFGetMaxStep
EAFStepRange
EAFSetReverse
EAFGetReverse
EAFSetBacklash
EAFGetBacklash
EAFClose
EAFGetSDKVersion
EAFGetFirmwareVersion
EAFGetSerialNumber
EAFGetType
EAFGetLedState
EAFSetLedState
EAFGetErrorCode
```

可选符号解析失败时，只禁用对应功能；核心符号失败时将 SDK 标记为不可用。

### 5.2 EafFocuserManager

继承 `QObject`，由 `DIMM` 拥有，负责：

- SDK 生命周期；
- USB 设备枚举；
- 设备打开/关闭；
- 两个逻辑望远镜槽位映射；
- 运动和参数控制；
- 状态轮询；
- SDK 错误转换；
- 设备移除与重连；
- 向 UI 发出线程安全信号。

SDK 调用不得发生在 GUI 线程。使用一个专用 `QThread`，所有 EAF API 在该线程串行执行。由于厂商文档未承诺多线程安全，即使有两台 EAF，也不要从两个线程同时调用 SDK。

### 5.3 FocuserControlWidget

继承 `QWidget`，作为设置窗口中的独立页签。它只负责：

- 展示状态；
- 收集用户输入；
- 发出意图信号；
- 根据 Manager 状态启用/禁用控件。

禁止在控件类中直接调用 SDK。

### 5.4 DIMM

`DIMM` 负责：

- 创建和销毁 `EafFocuserManager`；
- 将 `FocuserControlWidget` 插入 `SettingsDialog`；
- 提供当前 `CaptureState` 给调焦模块；
- 执行相机/调焦互锁；
- 在焦点移动后重置必要的测量状态；
- 应用程序退出时安全关闭两台 EAF。

---

## 6. 数据模型

建议定义：

```cpp
struct EafDeviceDescriptor {
    int enumerationIndex = -1;
    int id = -1;
    QString name;
    QString serialHex;
    QString type;
    QString firmwareVersion;
    int propertyMaxStep = 0;
    bool serialSupported = false;
};

struct EafDeviceState {
    bool sdkLoaded = false;
    bool present = false;
    bool opened = false;
    bool moving = false;
    bool handControl = false;
    bool temperatureValid = false;
    int currentPosition = 0;
    int commandedTarget = 0;
    int maxStep = 0;
    int stepRange = 0;
    int backlash = 0;
    float temperatureC = 0.0f;
    bool reverse = false;
    bool beep = false;
    bool led = false;
    QString motorErrorCode;
    QString batteryErrorCode;
    QString lastError;
    qint64 lastUpdatedMs = 0;
};

enum class TelescopeSlot {
    Telescope1 = 0,
    Telescope2 = 1
};

struct FocuserAssignment {
    TelescopeSlot slot;
    QString preferredSerialHex;
    int activeDeviceId = -1;
    bool assigned = false;
};
```

### 6.1 稳定标识

优先使用 `EAFGetSerialNumber()` 返回的序列号作为持久化标识。

若固件不支持序列号：

1. 使用 `Name + Type + 当前 ID` 作为临时显示；
2. 不承诺重启后自动映射；
3. UI 明确提示用户重新选择；
4. 不得仅依赖枚举顺序把“第一个设备”永久当成望远镜1。

---

## 7. 双望远镜映射设计

逻辑关系：

```text
望远镜1 <-> 相机1 <-> EAF 槽位1
望远镜2 <-> 相机2 <-> EAF 槽位2
```

### 7.1 映射规则

- 同一物理 EAF 不能同时分配给两个望远镜；
- 设备列表显示：`设备名 | SN | ID | 打开状态`；
- 保存用户选择的 SN；
- 启动或刷新设备时按 SN 自动恢复映射；
- 设备缺失时保留“期望 SN”，状态显示“未连接”；
- 重新插入同一 SN 后可自动重新打开，但仅在设备不移动且没有进行关键采集切换时执行；
- 提供“交换望远镜1/2映射”按钮，但操作前二次确认。

### 7.2 持久化

使用 `QSettings` 保存：

```text
focuser/telescope1/serial
focuser/telescope2/serial
focuser/telescope1/defaultStep
focuser/telescope2/defaultStep
focuser/telescope1/approachDirection
focuser/telescope2/approachDirection
```

不要依赖尚未实现的“保存配置 JSON”菜单。以后实现统一配置导入/导出时，再将这些字段纳入 JSON。

---

## 8. “自动调焦”页签 UI 设计

页签标题：**自动调焦**

### 8.1 设备与望远镜

控件：

- `QComboBox`：控制对象，选项“望远镜1 / 望远镜2”；
- `QComboBox`：物理 EAF 设备；
- `刷新设备`；
- `连接/打开`；
- `断开/关闭`；
- `应用映射`；
- `交换1/2映射`；
- SDK 状态标签；
- 设备在线状态灯。

### 8.2 实时状态

只读字段：

- 设备名称；
- 设备 ID；
- 序列号；
- EAF 类型；
- SDK 版本；
- 固件版本；
- 当前位置；
- 最近命令目标位置；
- 最大位置；
- 步进范围；
- 温度；
- 运动状态：停止 / 软件移动 / 手柄移动；
- 反向状态；
- 回差；
- 蜂鸣器；
- LED；
- 电机错误码；
- 最近 SDK 错误。

状态刷新策略：

- 移动中每 `200~250 ms` 刷新；
- 停止时每 `1000 ms` 刷新；
- 设置窗口关闭后可降低为每 `2000 ms`，但设备移除仍需处理；
- 禁止使用 GUI 线程中的 `while + Sleep`。

### 8.3 手动移动

控件：

- `QSpinBox`：单次步进，默认 100；
- 快捷步长：1、10、50、100、500、1000；
- `位置减小 -`；
- `位置增加 +`；
- `QSpinBox`：绝对目标位置；
- `移动到目标位置`；
- `停止`；
- 当前位置进度条；
- 状态文本。

说明：

- UI 主按钮优先使用“位置减小/增加”，不要直接写死“向内/向外”，因为机械安装和 Reverse 状态可能不同；
- 可在设置中让用户为每套望远镜定义“位置增加对应向内还是向外”；
- 相对移动流程为：读取可靠当前位置 -> 加减步长 -> clamp -> `EAFMove`；
- 目标位置必须限制到 `[0, maxStep]`；
- 设备移动中禁止再次发送普通移动命令；
- `停止`始终保持可点击；
- 若 `handControl == true`，停止按钮旁显示“手柄移动无法由软件停止”。

### 8.4 位置校准

控件：

- 当前物理位置重置值；
- `将当前位置设为该值`；
- 新最大位置；
- `写入最大位置`。

安全规则：

- 设备移动中全部禁用；
- `EAFResetPostion` 和 `EAFSetMaxStep` 操作前弹出确认框；
- 新最大位置不得小于当前坐标；
- 成功后立即重新读取位置与最大位置；
- 失败时恢复 UI 为设备真实值。

### 8.5 机械与提示设置

控件：

- Reverse 开关；
- 回差 `0~255`；
- 蜂鸣器开关；
- LED 开关；
- 每项单独“应用”或统一“应用设备设置”。

能力判断：

- 使用 `EAFGetNumOfControls` 和 `EAFGetControlCaps`；
- 不支持 LED 的设备不显示可写开关；
- 只读能力显示但不可编辑；
- 不支持的 API 返回 `EAF_ERROR_NOT_SUPPORTED` 时，不要重复弹窗。

### 8.6 闭环自动对焦区

第一阶段显示为：

```text
图像闭环自动对焦：尚未启用
已完成 EAF 电机控制接口，后续将使用相机星点 HFR/FWHM 扫描最佳焦点。
```

“开始自动对焦”按钮第一阶段应隐藏或禁用，不能做空壳按钮。

---

## 9. 设备状态机

建议状态：

```cpp
enum class FocuserConnectionState {
    SdkUnavailable,
    NotPresent,
    Discovered,
    Opening,
    Ready,
    Moving,
    HandControlMoving,
    Error,
    Closing
};
```

合法转换：

```text
SdkUnavailable
NotPresent -> Discovered -> Opening -> Ready
Ready -> Moving -> Ready
Ready -> HandControlMoving -> Ready
Ready/Moving -> Error
任意已连接状态 -> Closing -> NotPresent/Discovered
```

### 9.1 设备拔出

任何调用返回 `EAF_ERROR_REMOVED` 时：

1. 停止该设备轮询；
2. 标记为未连接；
3. 禁用移动和设置控件；
4. 保留逻辑望远镜与 SN 映射；
5. 发出一次状态提示，不要每次轮询反复弹窗；
6. 用户刷新或定时低频重枚举后允许恢复。

### 9.2 SDK 不可用

DLL 缺失或符号解析失败时：

- DIMM 主程序必须能够启动；
- 自动调焦页显示 SDK 错误和预期 DLL 路径；
- 其他相机、采集、通信功能正常工作；
- 禁用所有 EAF 操作按钮。

---

## 10. 线程、命令和轮询

### 10.1 线程规则

- GUI 线程只发出请求和接收状态；
- 所有 EAF API 在唯一 worker 线程执行；
- 两台设备的 SDK 调用也串行化；
- 不允许从析构线程直接与 worker 同时调用 SDK；
- 应用退出时先停止轮询，再关闭设备，再退出线程。

### 10.2 命令优先级

```text
最高：Emergency Stop / StopAndWait
其次：设备移除处理 / Close
普通：Move / Reset / Set parameter
最低：状态轮询 / 枚举刷新
```

停止请求不能被长队列中的普通轮询阻塞。

### 10.3 移动命令

`moveAbsolute(slot, target)`：

1. 检查映射与打开状态；
2. 检查非手柄控制；
3. 读取或使用足够新的 `maxStep`；
4. 验证目标；
5. 调用 `EAFMove`；
6. 成功后进入 Moving；
7. 轮询位置和运动状态；
8. 停止后发出 `moveFinished`。

`moveRelative(slot, delta)`：

1. 若当前位置缓存超过 500 ms，先读取位置；
2. `target = clamp(current + delta, 0, maxStep)`；
3. 转为 `moveAbsolute`。

`stop(slot)`：

1. 先读取 `EAFIsMoving`；
2. 若手柄控制，返回明确提示；
3. 优先调用 `EAFStopAndWait(id, 2000)`；
4. 若符号不可用，降级为 `EAFStop` + 非阻塞轮询；
5. 超时显示警告但继续轮询真实状态。

---

## 11. 与 DIMM 采集状态的互锁

### 11.1 第一阶段规则

在以下状态中禁止普通焦点移动和参数写入：

```text
CaptureState::Live
CaptureState::Simulation
CaptureState::Alignment
```

允许：

- 查看状态；
- 刷新设备；
- 紧急停止；
- 关闭设备时的安全清理。

允许移动的状态：

```text
CaptureState::Idle
CaptureState::Paused
```

若用户在 Live 状态点击移动：

```text
“实时 DIMM 测量中禁止移动焦点。请先暂停或停止采集。”
```

不要自动静默暂停采集，因为这会改变用户测量流程。

### 11.2 移动完成后的处理

焦点移动完成后：

- 不自动启动正式采集；
- 清除该相机旧的星斑质量缓存；
- 若之后恢复 Live，仍走现有的双相机全画幅定位 -> ROI 流程；
- 不直接复用移动前的焦点评分或星斑尺寸。

---

## 12. CMake 与部署

### 12.1 新增源文件

把以下文件加入 `PROJECT_SOURCES`：

```text
EafSdkLoader.h
EafSdkLoader.cpp
EafFocuserManager.h
EafFocuserManager.cpp
FocuserControlWidget.h
FocuserControlWidget.cpp
```

第二阶段再加入：

```text
AutoFocusController.h/.cpp
FocusMetricCalculator.h/.cpp
```

### 12.2 头文件目录

```cmake
set(EAF_SDK_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eaf"
    CACHE PATH "ZWO EAF SDK directory")

target_include_directories(${PROJECT_NAME} PRIVATE
    "${EAF_SDK_DIR}/include"
)
```

### 12.3 动态加载方案

第一阶段使用 `QLibrary`，不在 `target_link_libraries` 中链接 EAF `.lib`。

构建后若 DLL 存在则复制：

```cmake
set(EAF_DLL "${EAF_SDK_DIR}/bin/win64/EAF_focuser.dll")

if(EXISTS "${EAF_DLL}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${EAF_DLL}"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying ZWO EAF SDK DLL..."
    )
else()
    message(WARNING "EAF_focuser.dll not found; focuser support will be unavailable at runtime")
endif()
```

### 12.4 运行时加载路径

按顺序尝试：

1. `QCoreApplication::applicationDirPath()/EAF_focuser.dll`；
2. `QCoreApplication::applicationDirPath()/plugins/eaf/EAF_focuser.dll`；
3. 系统 DLL 搜索路径。

错误信息中必须列出实际尝试过的路径。

---

## 13. 第二阶段：图像闭环自动对焦

第二阶段必须单独提交，不能与第一阶段硬件控制混在一个不可审查的大改动中。

### 13.1 核心原则

- 一次只对一套望远镜执行自动对焦；
- 望远镜1使用相机1图像和 EAF1；
- 望远镜2使用相机2图像和 EAF2；
- 正式 DIMM 测量中禁止开始自动对焦；
- 自动对焦失败必须返回起始位置；
- 每个测量点从同一方向逼近，以消除回差影响。

### 13.2 建议焦点评分

首选 HFR（Half Flux Radius），可同时输出 FWHM 辅助诊断。

新增：

```cpp
struct FocusMetricResult {
    bool valid = false;
    double hfr = 0.0;
    double fwhm = 0.0;
    double peak = 0.0;
    double background = 0.0;
    double snr = 0.0;
};
```

焦点评分必须：

- 使用热像素修正后的灰度 ROI；
- 背景扣除；
- 排除饱和、边缘截断、无星点和多星混合帧；
- 每个位置采集多帧，取中位数而不是单帧值。

### 13.3 扫描参数

UI 参数：

- 粗扫半范围；
- 粗扫步长；
- 精扫半范围；
- 精扫步长；
- 每点有效帧数；
- 到位稳定等待时间；
- 统一逼近方向；
- 最大总移动范围；
- 最大运行时间。

建议初始默认值：

```text
粗扫半范围：1000 step
粗扫步长：200 step
精扫半范围：300 step
精扫步长：50 step
每点有效帧：5
到位稳定等待：500 ms
```

这些只是默认 UI 值，必须受设备最大位置和用户配置限制。

### 13.4 扫描流程

```text
保存起始位置
  -> 验证相机和星点
  -> 生成不越界的粗扫位置序列
  -> 每个位置从统一方向逼近
  -> 每点采集 N 个有效 HFR，取中位数
  -> 找到粗扫最优区间
  -> 精扫
  -> 对最优附近 3~5 个点进行二次曲线拟合
  -> 校验曲线开口向上且顶点在扫描范围内
  -> 移动到拟合最佳位置
  -> 重新采样验证
  -> 成功则保存；失败则回到起始位置
```

### 13.5 自动对焦状态

```cpp
enum class AutoFocusState {
    Idle,
    Preparing,
    Moving,
    Settling,
    CollectingFrames,
    Fitting,
    Verifying,
    Returning,
    Completed,
    Aborted,
    Failed
};
```

必须支持用户中止。中止后停止 EAF 并返回起始位置，除非用户明确选择“停在当前位置”。

### 13.6 日志

每次自动对焦保存：

```text
timestamp
telescope_slot
eaf_serial
camera_serial
start_position
sample_position
hfr_median
fwhm_median
valid_frame_count
best_position
verified_metric
result
failure_reason
```

---

## 14. 建议类接口

### 14.1 EafFocuserManager

```cpp
class EafFocuserManager : public QObject {
    Q_OBJECT
public:
    explicit EafFocuserManager(QObject* parent = nullptr);
    ~EafFocuserManager() override;

    void initialize();
    void shutdown();

public slots:
    void refreshDevices();
    void assignDevice(TelescopeSlot slot, QString serialHex);
    void openAssignedDevice(TelescopeSlot slot);
    void closeAssignedDevice(TelescopeSlot slot);
    void requestStateRefresh(TelescopeSlot slot);
    void moveAbsolute(TelescopeSlot slot, int target);
    void moveRelative(TelescopeSlot slot, int delta);
    void stopMotion(TelescopeSlot slot);
    void resetPosition(TelescopeSlot slot, int value);
    void setMaxStep(TelescopeSlot slot, int value);
    void setReverse(TelescopeSlot slot, bool value);
    void setBacklash(TelescopeSlot slot, int value);
    void setBeep(TelescopeSlot slot, bool value);
    void setLed(TelescopeSlot slot, bool value);
    void setMotionAllowed(bool allowed, QString reason);

signals:
    void sdkAvailabilityChanged(bool available, QString detail);
    void deviceListChanged(QVector<EafDeviceDescriptor> devices);
    void assignmentChanged(TelescopeSlot slot, QString serialHex);
    void stateChanged(TelescopeSlot slot, EafDeviceState state);
    void commandStarted(TelescopeSlot slot, QString command);
    void commandFinished(TelescopeSlot slot, QString command);
    void commandFailed(TelescopeSlot slot, QString command, QString error);
    void deviceRemoved(TelescopeSlot slot, QString serialHex);
};
```

### 14.2 FocuserControlWidget

```cpp
class FocuserControlWidget : public QWidget {
    Q_OBJECT
public:
    explicit FocuserControlWidget(QWidget* parent = nullptr);
    void setManager(EafFocuserManager* manager);
    void setCaptureState(DIMM::CaptureState state);
};
```

为避免头文件循环依赖，可把采集互锁传成简单布尔值：

```cpp
void setMotionAllowed(bool allowed, const QString& reason);
```

不要让 `FocuserControlWidget` 依赖完整 `DIMM` 类。

---

## 15. 需要修改的现有文件

### 15.1 `CMakeLists.txt`

- 修正真实源文件路径；
- 加入 EAF 新模块；
- 加入 SDK include 路径；
- 构建后复制 DLL；
- DLL 缺失时仅 warning，不让 CMake 直接失败。

### 15.2 `DIMM.h`

新增前向声明和成员：

```cpp
class EafFocuserManager;
class FocuserControlWidget;

EafFocuserManager* m_focuserManager = nullptr;
FocuserControlWidget* m_focuserControlWidget = nullptr;
```

若 SettingsDialog 需要暴露插页方法，新增：

```cpp
void addSettingsPage(QWidget* page, const QString& title);
```

优先提供通用 `addSettingsPage`，不要把大量 EAF 控件字段继续塞进 `SettingsDialog` 公有区。

### 15.3 `DIMM.cpp`

- 构造函数创建 `EafFocuserManager`；
- 初始化 `FocuserControlWidget`；
- 添加到 SettingsDialog；
- 连接采集状态变化与 `setMotionAllowed`；
- 析构前调用 manager shutdown；
- Live/Simulation/Alignment 时禁用普通移动；
- 设备错误通过现有 `setStatusMessage` 显示，但不要反复覆盖关键相机错误。

### 15.4 `SettingsDialog`

建议增加通用页签插入能力：

```cpp
QTabWidget* m_tabWidget = nullptr;
void addSettingsPage(QWidget* page, const QString& title);
```

当前构造函数中的 `tabWidget` 是局部变量，需要提升为成员，便于外部模块添加独立设置页。

---

## 16. 实施阶段

### 阶段 A：SDK 加载和无设备安全启动

任务：

- 建立 `third_party/eaf` 目录结构；
- 实现 `EafSdkLoader`；
- 显示 SDK 版本；
- DLL 缺失时程序仍能启动；
- 添加错误码中文转换。

验收：

- 没有 DLL 时主程序正常打开；
- 自动调焦页显示“SDK 未加载”；
- 放入正确 DLL 后能显示 SDK 版本；
- 其他 DIMM 功能行为不变。

### 阶段 B：USB 多设备枚举和映射

任务：

- `EAFGetNum -> EAFGetID`；
- 打开设备；
- 读取名称、SN、类型、固件和能力；
- 两个望远镜槽位映射；
- QSettings 持久化；
- 防止重复分配。

验收：

- 0、1、2 台设备均不崩溃；
- 两台设备可以分别映射；
- 同一设备不能映射两次；
- 重启后按 SN 恢复；
- 拔出一台时另一台仍可使用。

### 阶段 C：状态轮询

任务：

- 位置、移动状态、手柄状态、温度；
- 最大位置、反向、回差、蜂鸣器、LED；
- 错误码；
- 动态轮询频率；
- 设备移除处理。

验收：

- UI 不冻结；
- 手动转动手柄时状态正确；
- 手柄期间温度不可用时显示 `--`；
- 拔线后不重复弹窗；
- 插回并刷新后可恢复。

### 阶段 D：移动和设置

任务：

- 绝对移动；
- 相对步进；
- StopAndWait；
- 位置重置；
- 最大位置；
- Reverse、Backlash、Beep、LED；
- 捕获状态互锁。

验收：

- 所有目标都被限制在合法范围；
- 移动中不能重复下发移动；
- Stop 可立即请求；
- 手柄移动时显示软件无法停止；
- Live 状态禁止移动；
- 参数写入后回读确认。

### 阶段 E：清理、日志和文档

任务：

- 应用退出安全关闭；
- 统一日志；
- 增加 README；
- 完成硬件测试清单；
- 清理临时代码和死代码。

验收：

- 关闭设置窗口不会关闭设备；
- 退出主程序会关闭所有已打开 EAF；
- 重复打开/关闭设置窗口无信号重复连接；
- Debug 和 Release 均能构建。

### 阶段 F：图像闭环自动对焦

在 A~E 全部验收后单独开始，按第 13 节执行。

---

## 17. 测试矩阵

### 17.1 无硬件

- DLL 不存在；
- DLL 存在但无 EAF；
- 缺少非核心可选符号；
- DLL 架构错误。

### 17.2 一台 EAF

- 分配给望远镜1；
- 改分配到望远镜2；
- 绝对移动；
- 正负相对步进；
- 目标为 0；
- 目标为 MaxStep；
- 越界输入；
- 移动中停止；
- 手柄移动；
- 拔出和插回。

### 17.3 两台 EAF

- 分别映射；
- 防止重复映射；
- 交替移动；
- 一台移动时读取另一台状态；
- 一台拔出不影响另一台；
- 重启后 SN 映射恢复；
- 交换映射。

### 17.4 DIMM 互锁

- Idle 可移动；
- Paused 可移动；
- Live 禁止移动；
- Alignment 禁止移动；
- Simulation 禁止移动；
- Live 中仍可紧急停止；
- 焦点移动后重新开始 Live 时重新定位星点。

### 17.5 参数边界

- 回差：-1、0、255、256；
- 最大位置小于当前位置；
- 重置位置越界；
- 设置过程中设备开始移动；
- API 返回 MOVING、REMOVED、CLOSED、NOT_SUPPORTED。

---

## 18. 日志要求

日志格式建议：

```text
[EAF][Scope1][SN=xxxxxxxx] Enumerated id=3 name=EAF
[EAF][Scope1] Open success
[EAF][Scope1] Move request current=12000 target=12500
[EAF][Scope1] Move finished position=12500 elapsed=1840ms
[EAF][Scope2] Device removed
[EAF][SDK] Load failed: EAF_focuser.dll not found
```

禁止每 250 ms 输出一条普通轮询日志。仅记录：

- 状态变化；
- 用户命令；
- 错误；
- 设备插拔；
- 映射变化；
- 自动对焦采样点。

---

## 19. Codex 开发约束

Codex 执行时必须遵守：

1. 只在 `src观星版` 范围内改动，除非构建系统确实要求调整仓库根目录文件；
2. 不修改厂商 `EAF_focuser.h`；
3. 不把 SDK 调用放入 GUI 线程；
4. 不使用 `while + Sleep` 阻塞 Qt 事件循环；
5. 不把同一物理设备映射给两个望远镜；
6. 不忽略任何 SDK 返回码；
7. 不在 Live 测量中移动焦点；
8. DLL 缺失时 DIMM 必须仍能启动；
9. 不实现空壳“自动对焦成功”；
10. 每个阶段完成后先构建和验证，再继续下一阶段；
11. 使用 C++17、RAII、Qt queued signal/slot；
12. 所有用户可见文本使用中文，源文件 UTF-8；
13. 新增代码应避免进一步扩大 `DIMM.cpp`，优先独立类；
14. 不删除或重写现有相机、ROI、r0 和脉冲发生器逻辑；
15. 最终提交需包含变更说明、构建步骤、DLL 放置说明和硬件测试结果。

---

## 20. Codex 最终交付物

```text
1. 可编译的 C++/Qt 源码
2. 新增自动调焦设置页
3. 两台 EAF 独立控制与稳定映射
4. SDK 动态加载和错误降级
5. CMake 配置和 DLL 复制规则
6. QSettings 映射持久化
7. README_EAF.md
8. EAF 错误码中文表
9. 无设备/单设备/双设备测试记录
10. 第二阶段 AutoFocusController 接口预留
```

完成标准不是“界面出现按钮”，而是：设备可被稳定识别、不会错控另一台望远镜、移动安全、错误可恢复、程序不阻塞，并且不破坏现有 DIMM 采集流程。
