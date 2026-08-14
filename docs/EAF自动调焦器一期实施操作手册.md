# EAF 自动调焦器一期实施操作手册

> **给执行 agent 的强制要求：** 本文件只实现 EAF 硬件控制、双设备映射、状态监控和设置页 UI。不要实现 HFR/FWHM 闭环自动对焦，不要新增 `AutoFocusController` 或 `FocusMetricCalculator`。

**目标：** 在现有 DIMM Qt 程序中接入两台 ZWO EAF 调焦器，让用户能在“设置”窗口中枚举、映射、打开、查看状态、移动、停止和写入基础参数。

**架构：** 使用 `QLibrary` 动态加载 EAF SDK DLL；所有 SDK 调用由 `EafFocuserManager` 在单独 worker 线程串行执行；`FocuserControlWidget` 只负责 UI 和用户意图信号；`DIMM` 只负责创建对象、插入设置页、传递采集互锁状态。

**技术栈：** Windows x64、MSVC、C++17、Qt 6 Widgets、CMake、ZWO EAF SDK v1.8.1。

---

## 0. 真实 SDK 路径

当前机器 SDK 位置：

```text
E:\env\EAF_SDK_V1.8.1\EAF_Windows_SDK_V1.8.1
```

必须使用以下文件：

```text
E:\env\EAF_SDK_V1.8.1\EAF_Windows_SDK_V1.8.1\include\EAF_focuser.h
E:\env\EAF_SDK_V1.8.1\EAF_Windows_SDK_V1.8.1\lib\Windows\x64\Release\EAF_focuser.dll
```

本期使用 `QLibrary` 动态加载 DLL，不在 `target_link_libraries` 中链接 `EAF_focuser.lib`。

---

## 1. 文件清单

### 新增文件

```text
src/EafSdkLoader.h
src/EafSdkLoader.cpp
src/EafFocuserManager.h
src/EafFocuserManager.cpp
src/FocuserControlWidget.h
src/FocuserControlWidget.cpp
docs/README_EAF.md
```

### 修改文件

```text
CMakeLists.txt
src/DIMM.h
src/DIMM.cpp
```

### 禁止新增

```text
src/AutoFocusController.h
src/AutoFocusController.cpp
src/FocusMetricCalculator.h
src/FocusMetricCalculator.cpp
```

---

## 2. 禁止事项

- 不要在 GUI 线程直接调用 EAF SDK。
- 不要使用 `while + Sleep` 阻塞 Qt 事件循环。
- 不要依赖设备枚举顺序做永久映射。
- 不要把同一台物理 EAF 同时分配给望远镜1和望远镜2。
- 不要在 `CaptureState::Live`、`CaptureState::Simulation`、`CaptureState::Alignment` 中允许普通移动或写参数。
- 不要忽略任何 SDK 返回码。
- DLL 缺失时主程序必须能启动，只禁用 EAF 功能。
- 不要修改相机、ROI、r0、脉冲发生器的现有逻辑。
- 不要把大量 EAF 控件字段继续塞进 `SettingsDialog` 公有区。

---

## 3. CMake 修改

### 3.1 加入新增源码

在 `CMakeLists.txt` 的 `PROJECT_SOURCES` 中加入：

```cmake
    src/EafSdkLoader.h
    src/EafSdkLoader.cpp
    src/EafFocuserManager.h
    src/EafFocuserManager.cpp
    src/FocuserControlWidget.h
    src/FocuserControlWidget.cpp
```

### 3.2 加入 SDK 路径和 DLL 复制

在 OpenCV 配置后、`add_executable` 前加入：

```cmake
set(EAF_SDK_DIR
    "E:/env/EAF_SDK_V1.8.1/EAF_Windows_SDK_V1.8.1"
    CACHE PATH "ZWO EAF SDK directory")

include_directories("${EAF_SDK_DIR}/include")
```

在 OpenCV DLL copy 后加入：

```cmake
set(EAF_DLL "${EAF_SDK_DIR}/lib/Windows/x64/Release/EAF_focuser.dll")
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

---

## 4. Task A：实现 `EafSdkLoader`

### 4.1 职责

`EafSdkLoader` 只做三件事：

- 从运行目录或 SDK 路径加载 `EAF_focuser.dll`。
- 解析函数指针。
- 提供错误码中文转换。

不要在这里做设备状态管理、线程管理或 UI。

### 4.2 必须解析的核心符号

核心符号缺失时，SDK 不可用：

```text
EAFGetNum
EAFGetID
EAFOpen
EAFGetProperty
EAFMove
EAFStop
EAFIsMoving
EAFGetPosition
EAFResetPostion
EAFGetTemp
EAFSetMaxStep
EAFGetMaxStep
EAFStepRange
EAFSetReverse
EAFGetReverse
EAFSetBacklash
EAFGetBacklash
EAFClose
EAFGetSDKVersion
```

可选符号缺失时只禁用对应功能：

```text
EAFStopAndWait
EAFGetNumOfControls
EAFGetControlCaps
EAFSetBeep
EAFGetBeep
EAFGetFirmwareVersion
EAFGetSerialNumber
EAFGetType
EAFGetLedState
EAFSetLedState
EAFGetErrorCode
```

### 4.3 头文件接口要求

`src/EafSdkLoader.h` 至少包含：

```cpp
#pragma once

#include <QLibrary>
#include <QString>
#include <QStringList>

#include "EAF_focuser.h"

class EafSdkLoader {
public:
    using EAFGetNumFn = int (*)();
    using EAFGetIDFn = EAF_ERROR_CODE (*)(int, int*);
    using EAFOpenFn = EAF_ERROR_CODE (*)(int);
    using EAFGetPropertyFn = EAF_ERROR_CODE (*)(int, EAF_INFO*);
    using EAFMoveFn = EAF_ERROR_CODE (*)(int, int);
    using EAFStopFn = EAF_ERROR_CODE (*)(int);
    using EAFStopAndWaitFn = EAF_ERROR_CODE (*)(int, int);
    using EAFIsMovingFn = EAF_ERROR_CODE (*)(int, bool*, bool*);
    using EAFGetPositionFn = EAF_ERROR_CODE (*)(int, int*);
    using EAFResetPostionFn = EAF_ERROR_CODE (*)(int, int);
    using EAFGetTempFn = EAF_ERROR_CODE (*)(int, float*);
    using EAFSetBeepFn = EAF_ERROR_CODE (*)(int, bool);
    using EAFGetBeepFn = EAF_ERROR_CODE (*)(int, bool*);
    using EAFSetMaxStepFn = EAF_ERROR_CODE (*)(int, int);
    using EAFGetMaxStepFn = EAF_ERROR_CODE (*)(int, int*);
    using EAFStepRangeFn = EAF_ERROR_CODE (*)(int, int*);
    using EAFSetReverseFn = EAF_ERROR_CODE (*)(int, bool);
    using EAFGetReverseFn = EAF_ERROR_CODE (*)(int, bool*);
    using EAFSetBacklashFn = EAF_ERROR_CODE (*)(int, int);
    using EAFGetBacklashFn = EAF_ERROR_CODE (*)(int, int*);
    using EAFCloseFn = EAF_ERROR_CODE (*)(int);
    using EAFGetSDKVersionFn = const char* (*)();
    using EAFGetFirmwareVersionFn = EAF_ERROR_CODE (*)(int, unsigned char*, unsigned char*, unsigned char*);
    using EAFGetSerialNumberFn = EAF_ERROR_CODE (*)(int, EAF_SN*);
    using EAFGetTypeFn = EAF_ERROR_CODE (*)(int, EAF_TYPE*);
    using EAFGetLedStateFn = EAF_ERROR_CODE (*)(int, bool*);
    using EAFSetLedStateFn = EAF_ERROR_CODE (*)(int, bool);
    using EAFGetErrorCodeFn = EAF_ERROR_CODE (*)(int, EAF_ERROR_MSG*);
    using EAFGetNumOfControlsFn = EAF_ERROR_CODE (*)(int, int*);
    using EAFGetControlCapsFn = EAF_ERROR_CODE (*)(int, int, EAF_CONTROL_CAPS*);

    bool load();
    bool isLoaded() const;
    QString loadError() const;
    QString sdkVersion() const;
    QStringList attemptedPaths() const;

    static QString errorToString(EAF_ERROR_CODE code);
    static QString serialToHex(const EAF_SN& sn);

    EAFGetNumFn EAFGetNum = nullptr;
    EAFGetIDFn EAFGetID = nullptr;
    EAFOpenFn EAFOpen = nullptr;
    EAFGetPropertyFn EAFGetProperty = nullptr;
    EAFMoveFn EAFMove = nullptr;
    EAFStopFn EAFStop = nullptr;
    EAFStopAndWaitFn EAFStopAndWait = nullptr;
    EAFIsMovingFn EAFIsMoving = nullptr;
    EAFGetPositionFn EAFGetPosition = nullptr;
    EAFResetPostionFn EAFResetPostion = nullptr;
    EAFGetTempFn EAFGetTemp = nullptr;
    EAFSetBeepFn EAFSetBeep = nullptr;
    EAFGetBeepFn EAFGetBeep = nullptr;
    EAFSetMaxStepFn EAFSetMaxStep = nullptr;
    EAFGetMaxStepFn EAFGetMaxStep = nullptr;
    EAFStepRangeFn EAFStepRange = nullptr;
    EAFSetReverseFn EAFSetReverse = nullptr;
    EAFGetReverseFn EAFGetReverse = nullptr;
    EAFSetBacklashFn EAFSetBacklash = nullptr;
    EAFGetBacklashFn EAFGetBacklash = nullptr;
    EAFCloseFn EAFClose = nullptr;
    EAFGetSDKVersionFn EAFGetSDKVersion = nullptr;
    EAFGetFirmwareVersionFn EAFGetFirmwareVersion = nullptr;
    EAFGetSerialNumberFn EAFGetSerialNumber = nullptr;
    EAFGetTypeFn EAFGetType = nullptr;
    EAFGetLedStateFn EAFGetLedState = nullptr;
    EAFSetLedStateFn EAFSetLedState = nullptr;
    EAFGetErrorCodeFn EAFGetErrorCode = nullptr;
    EAFGetNumOfControlsFn EAFGetNumOfControls = nullptr;
    EAFGetControlCapsFn EAFGetControlCaps = nullptr;

private:
    template <typename Fn>
    bool resolveRequired(const char* name, Fn* out);

    template <typename Fn>
    void resolveOptional(const char* name, Fn* out);

    QLibrary m_library;
    QString m_loadError;
    QStringList m_attemptedPaths;
};
```

### 4.4 加载路径顺序

`load()` 必须按顺序尝试：

```text
QCoreApplication::applicationDirPath()/EAF_focuser.dll
QCoreApplication::applicationDirPath()/plugins/eaf/EAF_focuser.dll
E:/env/EAF_SDK_V1.8.1/EAF_Windows_SDK_V1.8.1/lib/Windows/x64/Release/EAF_focuser.dll
EAF_focuser.dll
```

加载失败时，`loadError()` 必须包含尝试过的路径。

### 4.5 错误码中文

`errorToString()` 至少覆盖：

```text
EAF_SUCCESS -> 成功
EAF_ERROR_INVALID_INDEX -> 设备索引无效
EAF_ERROR_INVALID_ID -> 设备 ID 无效
EAF_ERROR_INVALID_VALUE -> 参数值无效
EAF_ERROR_REMOVED -> 设备已移除
EAF_ERROR_MOVING -> 设备正在移动
EAF_ERROR_ERROR_STATE -> 设备处于错误状态
EAF_ERROR_GENERAL_ERROR -> 通用错误
EAF_ERROR_NOT_SUPPORTED -> 功能不支持
EAF_ERROR_CLOSED -> 设备未打开
EAF_ERROR_BATTER_INFO -> 电池信息异常
EAF_ERROR_INVALID_LENGTH -> 数据长度无效
```

---

## 5. Task B：实现 `EafFocuserManager`

### 5.1 数据模型

在 `src/EafFocuserManager.h` 中定义：

```cpp
enum class TelescopeSlot {
    Telescope1 = 0,
    Telescope2 = 1
};

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

struct FocuserAssignment {
    TelescopeSlot slot = TelescopeSlot::Telescope1;
    QString preferredSerialHex;
    int activeDeviceId = -1;
    bool assigned = false;
};
```

### 5.2 类接口

`EafFocuserManager` 必须继承 `QObject`：

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

### 5.3 线程规则

实现时可以采用内部 worker 对象，也可以让 manager 自己 `moveToThread`，但必须满足：

- `initialize()` 创建一个专用 `QThread`。
- 所有 EAF SDK 调用都在该线程中串行执行。
- 析构或 `shutdown()` 时先停止轮询，再关闭已打开设备，再退出线程。
- 不能从析构线程直接调用 SDK。

建议实现方式：

```text
EafFocuserManager lives in GUI thread
EafFocuserWorker lives in worker thread
Manager public slots -> QMetaObject::invokeMethod(worker, ..., Qt::QueuedConnection)
Worker emits signals -> Manager forwards to UI
```

如果执行 agent 能力较弱，可以把 worker 写在 `EafFocuserManager.cpp` 的匿名内部类中，不额外新增文件。

### 5.4 枚举流程

`refreshDevices()` 必须执行：

```text
sdk.load()
EAFGetNum()
for index in 0..count-1:
    EAFGetID(index, &id)
    EAFOpen(id)
    EAFGetProperty(id, &info)
    optional EAFGetSerialNumber(id, &sn)
    optional EAFGetType(id, &type)
    optional EAFGetFirmwareVersion(id, &major, &minor, &build)
    EAFClose(id)
emit deviceListChanged(...)
```

枚举时临时打开设备读取信息，读取完关闭。用户点击“打开”后再正式打开 assigned device。

### 5.5 映射规则

- `Telescope1` 和 `Telescope2` 不能使用同一个 `serialHex`。
- 使用 `QSettings` 保存：

```text
focuser/telescope1/serial
focuser/telescope2/serial
focuser/telescope1/defaultStep
focuser/telescope2/defaultStep
```

- 启动或刷新设备后，如果发现保存的 SN，允许自动恢复映射。
- 如果 `EAFGetSerialNumber` 不可用，用 `id` 和 `name` 显示设备，但不要承诺重启后稳定映射。

### 5.6 状态轮询

正式打开设备后轮询状态：

- 移动中：每 250 ms。
- 停止时：每 1000 ms。
- 设置窗口关闭但设备仍打开：可降到 2000 ms。

状态读取至少包含：

```text
EAFIsMoving
EAFGetPosition
EAFGetMaxStep
EAFStepRange
EAFGetReverse
EAFGetBacklash
EAFGetTemp
EAFGetBeep, if available
EAFGetLedState, if available
EAFGetErrorCode, if available
```

`EAFGetTemp` 返回错误或温度 `<= -200` 时，`temperatureValid = false`，UI 显示 `--`。

### 5.7 移动和停止

`moveAbsolute(slot, target)`：

```text
检查 motionAllowed
检查 assigned/opened
检查非 handControl
读取 maxStep
target clamp 到 [0, maxStep]
如果 moving，拒绝普通移动
EAFMove(id, target)
成功后 commandedTarget = target
```

`moveRelative(slot, delta)`：

```text
读取当前位置
target = clamp(current + delta, 0, maxStep)
调用 moveAbsolute
```

`stopMotion(slot)`：

```text
读取 EAFIsMoving
如果 handControl == true，返回“手柄移动无法由软件停止”
如果 EAFStopAndWait 可用，调用 EAFStopAndWait(id, 2000)
否则调用 EAFStop(id)，依靠轮询确认停止
```

Stop 不受 `motionAllowed` 限制，Live 中也允许。

### 5.8 参数写入

写入前必须检查：

- 设备已打开。
- 设备未移动。
- `motionAllowed == true`。

边界：

```text
Backlash: 0..255
MaxStep: 必须 >= 当前 position
ResetPosition: 0..maxStep
```

`resetPosition` 和 `setMaxStep` 属于高风险动作，UI 层必须二次确认。

---

## 6. Task C：实现 `FocuserControlWidget`

### 6.1 职责

`FocuserControlWidget` 只做：

- 显示 SDK 状态。
- 显示设备列表和映射。
- 显示选中望远镜状态。
- 发出用户操作信号或直接连接 manager slots。
- 根据 `setMotionAllowed` 禁用普通移动和写参数。

不要直接 include 或调用 SDK。

### 6.2 基础 UI

页面标题为“自动调焦”。建议控件分为 5 个 `QGroupBox`：

```text
1. SDK 与设备
2. 望远镜映射
3. 实时状态
4. 手动移动
5. 设备参数
```

### 6.3 必须有的控件

SDK 与设备：

```text
SDK 状态 QLabel
刷新设备 QPushButton
设备列表 QComboBox
```

映射：

```text
当前望远镜 QComboBox：望远镜1 / 望远镜2
应用映射 QPushButton
交换映射 QPushButton
打开设备 QPushButton
关闭设备 QPushButton
```

实时状态：

```text
设备名、ID、SN、类型、固件、当前位置、目标位置、最大位置、步进范围、温度、移动状态、手柄状态、反向、回差、蜂鸣器、LED、错误码
```

手动移动：

```text
步长 QSpinBox，默认 100，范围 1..10000
位置减小 QPushButton
位置增加 QPushButton
目标位置 QSpinBox
移动到目标 QPushButton
停止 QPushButton
```

设备参数：

```text
Reverse QCheckBox + 应用按钮
Backlash QSpinBox 0..255 + 应用按钮
Beep QCheckBox + 应用按钮
LED QCheckBox + 应用按钮
Reset Position QSpinBox + 设置按钮
MaxStep QSpinBox + 写入按钮
```

### 6.4 UI 文案

所有用户可见文本用中文。按钮不要写“向内/向外”，写：

```text
位置减小
位置增加
```

因为机械安装和 Reverse 可能不同。

### 6.5 互锁显示

提供：

```cpp
void setMotionAllowed(bool allowed, const QString& reason);
```

当 `allowed == false`：

- 禁用普通移动按钮。
- 禁用参数写入按钮。
- 停止按钮保持可用。
- 显示原因，例如：

```text
实时 DIMM 测量中禁止移动焦点。请先暂停或停止采集。
```

---

## 7. Task D：修改 `SettingsDialog`

### 7.1 提供通用插页方法

当前 `SettingsDialog::SettingsDialog` 内部有局部变量：

```cpp
auto* tabWidget = new QTabWidget(this);
const auto addScrollableTab = [tabWidget](QWidget* page, const QString& title) { ... };
```

需要改成成员：

在 `src/DIMM.h` 的 `SettingsDialog` public 区加入：

```cpp
void addSettingsPage(QWidget* page, const QString& title);
```

private 区加入：

```cpp
QTabWidget* m_tabWidget = nullptr;
```

需要在 `DIMM.h` 顶部前向声明：

```cpp
class QTabWidget;
```

在 `SettingsDialog::SettingsDialog` 中替换：

```cpp
auto* tabWidget = new QTabWidget(this);
tabWidget->setDocumentMode(true);
```

为：

```cpp
m_tabWidget = new QTabWidget(this);
m_tabWidget->setDocumentMode(true);
```

把所有 `addScrollableTab(page, title)` 调用替换为：

```cpp
addSettingsPage(page, title);
```

把：

```cpp
mainLayout->addWidget(tabWidget);
```

替换为：

```cpp
mainLayout->addWidget(m_tabWidget);
```

新增函数实现：

```cpp
void SettingsDialog::addSettingsPage(QWidget* page, const QString& title)
{
    if (!m_tabWidget || !page) {
        return;
    }
    auto* scrollArea = new QScrollArea(m_tabWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(page);
    m_tabWidget->addTab(scrollArea, title);
}
```

注意：`DIMM.cpp` 已经 include 了 `QScrollArea` 和 `QFrame`，不要重复制造 include 问题。

---

## 8. Task E：修改 `DIMM`

### 8.1 头文件成员

在 `src/DIMM.h` 前向声明：

```cpp
class EafFocuserManager;
class FocuserControlWidget;
```

在 `DIMM` private 成员区加入：

```cpp
EafFocuserManager* m_focuserManager = nullptr;
FocuserControlWidget* m_focuserControlWidget = nullptr;
```

### 8.2 include

在 `src/DIMM.cpp` 顶部加入：

```cpp
#include "EafFocuserManager.h"
#include "FocuserControlWidget.h"
```

### 8.3 构造函数创建

在 `DIMM::DIMM` 中，创建 `SettingsDialog` 后创建：

```cpp
m_focuserManager = new EafFocuserManager(this);
m_focuserControlWidget = new FocuserControlWidget();
m_focuserControlWidget->setManager(m_focuserManager);
m_settingsDialog->addSettingsPage(m_focuserControlWidget, QStringLiteral("自动调焦"));
m_focuserManager->initialize();
```

`FocuserControlWidget` 的 parent 可以通过 `addSettingsPage` 的 `QScrollArea` 接管。若执行 agent 不放心，也可以创建时传 `m_settingsDialog` 作为 parent。

### 8.4 析构安全关闭

在 `DIMM::~DIMM()` 中，在释放主 UI 前调用：

```cpp
if (m_focuserManager) {
    m_focuserManager->shutdown();
}
```

### 8.5 采集状态互锁

新增一个小函数或在 `updateCaptureState` 中同步：

```cpp
const bool focuserMotionAllowed =
    m_captureState == CaptureState::Idle || m_captureState == CaptureState::Paused;
const QString reason = focuserMotionAllowed
    ? QString()
    : QStringLiteral("实时采集、模拟或对准模式中禁止移动焦点。请先暂停或停止采集。");
if (m_focuserManager) {
    m_focuserManager->setMotionAllowed(focuserMotionAllowed, reason);
}
if (m_focuserControlWidget) {
    m_focuserControlWidget->setMotionAllowed(focuserMotionAllowed, reason);
}
```

必须在以下地方调用一次：

- `DIMM` 构造完成后。
- `updateCaptureState(...)` 状态变化后。

---

## 9. Task F：文档 `docs/README_EAF.md`

内容必须包含：

```markdown
# EAF 自动调焦器一期说明

## 功能范围
- 支持 ZWO EAF USB 设备枚举、映射、打开、关闭、状态监控、移动和基础参数写入。
- 不包含基于图像 HFR/FWHM 的闭环自动对焦。

## SDK 放置
开发机 SDK 路径：
`E:\env\EAF_SDK_V1.8.1\EAF_Windows_SDK_V1.8.1`

运行时需要：
`EAF_focuser.dll`

构建会尝试从：
`lib\Windows\x64\Release\EAF_focuser.dll`
复制到程序输出目录。

## 采集互锁
Live、Simulation、Alignment 状态禁止普通焦点移动和参数写入。
紧急停止仍允许。

## 测试记录
- 无 DLL
- 有 DLL 无设备
- 单设备
- 双设备
- 设备拔插
- Live 状态互锁
```

---

## 10. 静态测试建议

如果执行 agent 会写测试，新增：

```text
tests/test_eaf_integration_static.py
```

最低限度检查：

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")

class EafIntegrationStaticTest(unittest.TestCase):
    def test_cmake_contains_eaf_sources_and_sdk_path(self):
        cmake = read("CMakeLists.txt")
        for source in [
            "src/EafSdkLoader.cpp",
            "src/EafFocuserManager.cpp",
            "src/FocuserControlWidget.cpp",
        ]:
            self.assertIn(source, cmake)
        self.assertIn("EAF_SDK_DIR", cmake)
        self.assertIn("EAF_focuser.dll", cmake)

    def test_settings_dialog_supports_external_pages(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        self.assertIn("void addSettingsPage(QWidget* page, const QString& title)", header)
        self.assertIn("QTabWidget* m_tabWidget", header)
        self.assertIn("SettingsDialog::addSettingsPage", cpp)

    def test_dimm_owns_focuser_manager_and_widget(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        self.assertIn("EafFocuserManager* m_focuserManager", header)
        self.assertIn("FocuserControlWidget* m_focuserControlWidget", header)
        self.assertIn("m_focuserManager->initialize()", cpp)
        self.assertIn("m_focuserManager->shutdown()", cpp)

if __name__ == "__main__":
    unittest.main()
```

运行：

```powershell
python tests/test_eaf_integration_static.py
```

---

## 11. 构建验收

执行 agent 完成后，必须至少做以下检查。

### 11.1 静态检查

```powershell
rg -n "AutoFocusController|FocusMetricCalculator" src CMakeLists.txt
```

预期：没有输出。

```powershell
rg -n "EafSdkLoader|EafFocuserManager|FocuserControlWidget|EAF_SDK_DIR|EAF_focuser.dll" src CMakeLists.txt
```

预期：能看到新增文件和 CMake 配置。

### 11.2 CMake 构建

使用项目已有 VS/CMake 配置构建 Debug。

如果出现 `EAF_focuser.dll not found`，只能是 warning，不能导致配置失败。

### 11.3 无硬件验收

没有 EAF 设备时：

- 主程序能启动。
- 设置窗口能打开。
- 自动调焦页显示 SDK 状态。
- 设备列表为空或显示无设备。
- 其他相机、ROI、脉冲发生器页面不受影响。

### 11.4 有硬件验收

一台 EAF：

- 刷新能看到设备。
- 能映射到望远镜1。
- 能打开、读取位置、最大位置、温度。
- 绝对移动到合法位置。
- 相对移动时 target 被 clamp。
- Stop 可用。

两台 EAF：

- 能分别映射到望远镜1/2。
- 同一设备不能映射两次。
- 交换映射前必须确认。
- 拔掉一台不影响另一台状态显示。

采集互锁：

- Idle 可以移动。
- Live 禁止普通移动。
- Alignment 禁止普通移动。
- Simulation 禁止普通移动。
- Live 中 Stop 仍可点击。

---

## 12. 常见坑

### 12.1 `EAFResetPostion` 拼写

SDK 头文件函数名就是：

```cpp
EAFResetPostion
```

不要改成 `EAFResetPosition`。

### 12.2 `_WINDOWS` 宏

不要在项目里为了 include 厂商头文件随意定义 `_WINDOWS`。本期动态加载不依赖导入库，直接 include 类型定义即可。

### 12.3 DLL 位数

项目是 x64，必须复制：

```text
lib\Windows\x64\Release\EAF_focuser.dll
```

不要复制 Win32 DLL。

### 12.4 手柄移动

`EAFIsMoving(id, &moving, &handControl)` 中 `handControl == true` 时，不要承诺软件能停止手柄移动。

### 12.5 设备移动中

`EAFGetMaxStep`、`EAFSetMaxStep`、`EAFStepRange` 可能返回 `EAF_ERROR_MOVING`。不要把它显示成“成功”。

### 12.6 UI 父对象

`FocuserControlWidget` 插入 `SettingsDialog::addSettingsPage` 后由 scroll area 接管。不要手动 delete。

---

## 13. 推荐提交顺序

如果执行 agent 会提交代码，按这个顺序：

```text
1. build: add EAF SDK paths and source placeholders
2. feat: add EAF SDK dynamic loader
3. feat: add EAF focuser manager
4. feat: add focuser control settings page
5. feat: wire EAF controls into DIMM settings dialog
6. docs: add EAF setup and test notes
```

每个提交后至少保证项目能配置，最后一个提交后保证 Debug 能构建。
