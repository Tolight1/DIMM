# DIMM 项目线程优化设计

> 文档目的：规范当前项目的线程模型，解决帧队列积压、处理延迟增长、关闭阻塞和后台任务串行化问题。  


---

## 1. 优化背景

DIMM 当前同时涉及以下实时或准实时任务：

- 两台 Galaxy 相机同步采集；
- 双相机 ROI 图像处理；
- 星点质心计算；
- 双相机帧配对；
- 大气参数反演；
- 北极星检测和星图匹配；
- EAF 电调焦器控制；
- 脉冲发生器串口控制；
- GUI 显示、日志记录和结果文件写入。

当前项目已经使用了 `QThread`、`QMutex`、`QWaitCondition`、`std::atomic` 和 Qt 排队信号等机制，基本线程安全框架已经具备。

但目前主要问题不在于“缺少锁”，而在于：

1. 帧数据跨线程传输缺少背压；
2. 已实现的最新帧机制未接入主处理链路；
3. 图像处理任务使用无界 FIFO 队列；
4. 两台相机的重计算任务实际上串行执行；
5. 部分硬件操作仍阻塞 GUI；
6. 个别线程关闭流程没有验证线程是否真正退出。

---

## 2. 当前线程模型

### 2.1 线程拓扑

```text
┌───────────────────────────────────────────────────────────────┐
│                       GUI 主线程                              │
│                                                               │
│  DIMM 界面 / 状态机 / 相机控制 / 文件写入 / 网络通信          │
│  脉冲发生器串口控制 / 预览刷新 / ROI 调整                     │
└───────────────┬───────────────────┬───────────────────────────┘
                │                   │
                │ QueuedConnection  │ QueuedConnection
                ▼                   ▼
┌───────────────────────────┐   ┌───────────────────────────────┐
│ ImageProcessor QThread    │   │ PolarisSolver QThread         │
│                           │   │                               │
│ 两台相机 ROI 串行处理      │   │ 两台相机北极星求解串行处理    │
│ 质心计算                   │   │ 星点检测                      │
│ 双相机配对                 │   │ 星图匹配                      │
│ 大气参数反演               │   │                               │
└───────────────────────────┘   └───────────────────────────────┘

┌───────────────────────────┐
│ EAF Worker QThread        │
│                           │
│ EAF SDK 调用              │
│ 位置和温度轮询             │
│ 电机移动控制               │
└───────────────────────────┘

┌───────────────────────────────────────────────────────────────┐
│ Galaxy SDK 内部回调线程                                         │
│                                                               │
│ 相机1回调 / 相机2回调 / SDK 缓冲区复制 / 最新帧缓存             │
└───────────────────────────────────────────────────────────────┘
```

### 2.2 显式创建的工作线程

当前源码中至少有以下三个显式 `QThread`：

| 线程 | 类 | 当前职责 |
|---|---|---|
| 图像处理线程 | `ImageProcessor` | 两台相机 ROI、质心、配对和大气参数计算 |
| 北极星求解线程 | `PolarisSolverController` | 两台相机星点检测和星图匹配 |
| 电调焦器线程 | `EafFocuserManager` | EAF SDK 调用、设备状态轮询和移动控制 |

除此之外，Galaxy SDK 会自行管理采集回调线程。

---

## 3. 主要问题

## 3.1 相机最新帧合并机制没有接入主链路

`CameraManager` 已经实现了以下机制：

```cpp
camera.latestFramePacket = packet;

bool expected = false;
if (camera.frameNotificationPending.compare_exchange_strong(
        expected,
        true,
        std::memory_order_acq_rel)) {
    emit frameReady(cameraIndex);
}
```

其设计目标是：

- 每台相机只保留最新帧；
- GUI 忙碌时，新帧覆盖旧帧；
- GUI 事件队列中最多保留一个帧通知；
- 避免大量旧帧在事件队列中堆积。

但当前主界面连接的是：

```cpp
connect(m_cameraManager,
        &CameraManager::frameCaptured,
        this,
        &DIMM::onCapturedFramePacket,
        Qt::QueuedConnection);
```

`frameCaptured` 每次回调都会向 GUI 队列发送一个携带 `CameraFrame` 的事件，导致最新帧机制被绕过。

### 风险

在高帧率或满画幅模式下可能出现：

- GUI 事件队列持续增长；
- 显示和处理延迟越来越大；
- 旧帧继续被处理；
- 内存占用明显增加；
- 停止采集后仍继续处理旧事件；
- 自动曝光和 ROI 重定位依据过期图像执行。

### 满画幅内存估算

5120 × 5120 Mono16 单帧约为：

```text
5120 × 5120 × 2 Byte ≈ 50 MiB
```

只要 GUI 队列积压 10 帧，理论图像数据占用就可能接近 500 MiB。

---

## 3.2 ImageProcessor 使用无界排队任务

当前 `ImageProcessor::processFrame()` 每次调用都会：

```cpp
cv::Mat frameCopy = frame.clone();

QMetaObject::invokeMethod(
    m_worker,
    "processFrame",
    Qt::QueuedConnection,
    ...
);
```

这意味着每一帧都会：

1. 再复制一次图像；
2. 创建一个 Qt 排队任务；
3. 等待唯一的图像处理线程依次处理。

当前没有：

- 最大队列长度；
- worker busy 标志；
- 每台相机 pending 标志；
- 最新帧覆盖机制；
- 排队延迟监控；
- 丢弃旧帧机制。

当输入速度高于处理速度时，任务会持续堆积。

### 后果

- `frameProcessed` 中记录的只是算法执行时间，不包括排队时间；
- 实际端到端延迟可能远高于显示的处理延迟；
- 自动曝光处理旧图像；
- ROI 跟踪响应变慢；
- 停止采集后 worker 仍有大量任务；
- 析构中的 `wait()` 可能长时间阻塞。

---

## 3.3 双相机北极星求解实际串行

`PolarisSolverController` 对两台相机维护了独立状态：

```cpp
bool m_taskRunning[kCameraCount];
PendingSolveTask m_pendingLatestTask[kCameraCount];
```

但只有一个：

```cpp
QThread* m_workerThread;
PolarisSolverWorker* m_worker;
```

所以两台相机的求解任务都会投递到同一个线程事件队列。

这意味着：

```text
相机1星点检测
    ↓
相机1星图匹配
    ↓
相机2星点检测
    ↓
相机2星图匹配
```

而不是双相机真正并行。

### 当前设计的优点

每台相机只保存一个最新待求解任务：

```cpp
m_pendingLatestTask[cameraIndex] = latestTask;
```

不会像 ImageProcessor 一样无限堆积。

### 当前设计的问题

- 双相机自动识别耗时相加；
- 状态上两台相机可能都显示“正在求解”，但实际只有一条任务在运行；
- 长时间星图匹配会阻塞另一台相机；
- `quit()` 无法中断正在运行的计算。

---

## 3.4 北极星求解取消不够及时

当前取消标志主要在星点检测结束后检查。

但满画幅星点检测包含：

- 图像灰度转换；
- 热像素校正；
- 归一化；
- 阈值计算；
- 连通域分析；
- 全图像素统计；
- 候选星点筛选。

如果在这些操作过程中退出对准模式，线程仍可能继续计算较长时间。

### 建议增加取消检查的位置

- 热像素逐行校正循环；
- 连通域像素统计循环；
- 星点候选排序循环；
- 星表三角形构建循环；
- 候选变换测试循环；
- 匹配优化迭代循环。

---

## 3.5 EAF 线程关闭存在生命周期风险

当前关闭流程类似：

```cpp
QMetaObject::invokeMethod(
    m_worker,
    "doShutdown",
    Qt::BlockingQueuedConnection);

m_workerThread->quit();
m_workerThread->wait(3000);

m_workerThread = nullptr;
m_worker = nullptr;
```

问题是没有检查：

```cpp
m_workerThread->wait(3000)
```

是否成功。

如果 EAF SDK 调用卡住：

1. `wait(3000)` 返回 `false`；
2. worker 线程仍在运行；
3. 管理器把指针清空；
4. 后续析构删除 SDK loader；
5. worker 可能继续访问已经释放的 SDK 对象；
6. 可能出现 `QThread: Destroyed while thread is still running`。

---

## 3.6 脉冲发生器串口操作阻塞 GUI

`PulseGeneratorManager` 使用同步 Windows API：

```cpp
WriteFile(...);
ReadFile(...);
Sleep(10);
```

设备无应答时，单次读取等待可达到约 1 秒；一次设备配置需要连续写多个寄存器。

目前这些操作在 GUI 主线程调用，可能导致：

- 界面短时间冻结；
- 相机帧事件无法及时处理；
- 定时器延迟；
- 停止按钮响应变慢；
- 更严重的 GUI 帧队列积压。

---

## 3.7 相机连接和设备枚举阻塞 GUI

`DIMM::onConnectAll()` 在 GUI 线程同步执行：

```cpp
enumerateDevices();
openAll();
```

设备枚举本身可能等待约 1 秒，打开设备和配置 GigE 参数也可能耗时。

虽然这不是实时采集阶段的主要问题，但会造成连接相机期间界面卡顿。

---

## 3.8 文件写入仍在 GUI 线程

`ResultWriter` 使用同步：

```cpp
QFile
QTextStream
```

当前采用内存缓存并定时刷新，正常本地磁盘下一般可接受。

但在以下情况下仍可能阻塞 GUI：

- 数据目录位于机械硬盘；
- 数据目录位于网络盘；
- 磁盘空间不足；
- 杀毒软件扫描文件；
- 单次缓存内容较大；
- 用户主动导出文件。

---

## 4. 优化目标

线程优化应满足以下目标。

### 4.1 实时性目标

- 不处理明显过期的图像帧；
- 图像处理排队深度始终有界；
- 处理延迟不会随运行时间持续增长；
- GUI 不因硬件 I/O 长时间冻结；
- 双相机跟踪保持稳定响应。

### 4.2 稳定性目标

- 程序退出时所有工作线程可控停止；
- 不在线程仍运行时释放其依赖对象；
- 相机回调退出前不释放 callback handler；
- 停止采集后旧任务不能污染新采集状态；
- 长时间运行内存占用保持稳定。

### 4.3 可观测性目标

应能够实时获取：

- 相机接收帧数；
- GUI 接收帧数；
- 图像处理完成帧数；
- 主动丢弃帧数；
- 图像处理队列等待时间；
- 算法执行时间；
- 端到端处理延迟；
- 北极星求解执行时间；
- 工作线程关闭耗时。

---

## 5. 第一阶段：修复相机帧分发链路

## 5.1 使用 frameReady 作为主通知

将：

```cpp
connect(m_cameraManager,
        &CameraManager::frameCaptured,
        this,
        &DIMM::onCapturedFramePacket,
        Qt::QueuedConnection);
```

修改为：

```cpp
connect(m_cameraManager,
        &CameraManager::frameReady,
        this,
        &DIMM::onFrameReady,
        Qt::QueuedConnection);
```

常规实时链路不再直接连接 `frameCaptured`。

### 修改后的数据流

```text
Galaxy SDK 回调
    ↓
复制 SDK 图像缓冲区
    ↓
覆盖 latestFramePacket
    ↓
frameNotificationPending: false → true
    ↓
只向 GUI 发送一次 frameReady
    ↓
GUI 调用 takeLatestFramePacket()
    ↓
清除 frameNotificationPending
    ↓
处理当前最新帧
```

## 5.2 防止取帧与新帧到达之间丢通知

基础实现中存在一个小窗口：

1. GUI 取出最新帧；
2. GUI 清除 pending；
3. 回调可能在清除前后写入新帧。

建议采用“取帧后重新检查版本号”或者“帧序列号”方案。

### 推荐状态

```cpp
struct CameraData {
    CameraFrame latestFramePacket;
    quint64 latestPublishedSequence = 0;
    quint64 latestConsumedSequence = 0;
    std::atomic_bool frameNotificationPending = false;
    QMutex frameMutex;
};
```

回调中：

```cpp
{
    QMutexLocker locker(&camera.frameMutex);
    camera.latestFramePacket = packet;
    ++camera.latestPublishedSequence;
}

scheduleFrameNotification(cameraIndex);
```

GUI 取帧后：

```cpp
CameraFrame CameraManager::takeLatestFramePacket(int index)
{
    auto& camera = m_cameras[index];

    CameraFrame packet;
    bool hasNewerFrame = false;

    {
        QMutexLocker locker(&camera.frameMutex);
        packet = camera.latestFramePacket;
        camera.latestConsumedSequence = camera.latestPublishedSequence;
        camera.frameNotificationPending.store(false, std::memory_order_release);

        hasNewerFrame =
            camera.latestPublishedSequence != camera.latestConsumedSequence;
    }

    if (hasNewerFrame) {
        scheduleFrameNotification(index);
    }

    return packet;
}
```

更简单的实现也可以先保留当前原子标志结构，通过压力测试确认是否存在漏通知。

## 5.3 保留 frameCaptured 的用途

`frameCaptured` 可以保留，但只用于：

- 单元测试；
- 调试日志；
- 低频抓拍；
- 非实时录制；
- 明确需要每一帧的离线模式。

不能同时让 `frameCaptured` 和 `frameReady` 都进入相同处理函数，否则会导致重复处理。

---

## 6. 第二阶段：给 ImageProcessor 增加背压

## 6.1 推荐方案：每台相机最新帧槽

不要让每一帧都直接生成 worker 队列任务。

建议在 `ImageProcessor` 中增加：

```cpp
struct PendingFrameSlot {
    QMutex mutex;
    CameraFrame latestFrame;
    quint64 acquisitionGeneration = 0;
    bool scheduled = false;
    quint64 replacedFrameCount = 0;
};

PendingFrameSlot m_pendingFrames[2];
```

主线程提交帧：

```cpp
void ImageProcessor::submitLatestFrame(
    int cameraIndex,
    const CameraFrame& frame,
    quint64 acquisitionGeneration)
{
    if (cameraIndex < 0 || cameraIndex >= 2 || !frame.isValid()) {
        return;
    }

    bool needSchedule = false;

    {
        auto& slot = m_pendingFrames[cameraIndex];
        QMutexLocker locker(&slot.mutex);

        if (slot.scheduled && slot.latestFrame.isValid()) {
            ++slot.replacedFrameCount;
        }

        slot.latestFrame = frame;
        slot.acquisitionGeneration = acquisitionGeneration;

        if (!slot.scheduled) {
            slot.scheduled = true;
            needSchedule = true;
        }
    }

    if (needSchedule) {
        QMetaObject::invokeMethod(
            m_worker,
            [this, cameraIndex]() {
                processLatestFrameLoop(cameraIndex);
            },
            Qt::QueuedConnection);
    }
}
```

worker 侧：

```cpp
void ImageProcessor::processLatestFrameLoop(int cameraIndex)
{
    for (;;) {
        CameraFrame frame;
        quint64 generation = 0;

        {
            auto& slot = m_pendingFrames[cameraIndex];
            QMutexLocker locker(&slot.mutex);

            if (!slot.latestFrame.isValid()) {
                slot.scheduled = false;
                return;
            }

            frame = slot.latestFrame;
            generation = slot.acquisitionGeneration;
            slot.latestFrame = CameraFrame();
        }

        m_worker->processFrame(
            cameraIndex,
            frame.image,
            frame.frameId,
            frame.cameraTimestamp,
            generation);

        {
            auto& slot = m_pendingFrames[cameraIndex];
            QMutexLocker locker(&slot.mutex);

            if (!slot.latestFrame.isValid()) {
                slot.scheduled = false;
                return;
            }
        }
    }
}
```

### 实际实现注意事项

上面的示例展示的是逻辑结构。

如果 `processLatestFrameLoop()` 本身就在 worker 线程执行，不应再直接跨线程调用 `m_worker`。更清晰的方式是把最新帧槽也放到 worker 对象中，并通过一个轻量通知唤醒 worker。

## 6.2 更推荐的 worker 内部结构

```cpp
class ImageProcessorWorker : public QObject {
    Q_OBJECT

public slots:
    void submitLatestFrame(
        int cameraIndex,
        CameraFrame frame,
        quint64 generation);

private:
    struct CameraQueueState {
        CameraFrame latest;
        quint64 generation = 0;
        bool processing = false;
        quint64 dropped = 0;
    };

    CameraQueueState m_cameraState[2];

    void processNext(int cameraIndex);
};
```

```cpp
void ImageProcessorWorker::submitLatestFrame(
    int cameraIndex,
    CameraFrame frame,
    quint64 generation)
{
    auto& state = m_cameraState[cameraIndex];

    if (state.latest.isValid()) {
        ++state.dropped;
    }

    state.latest = std::move(frame);
    state.generation = generation;

    if (!state.processing) {
        state.processing = true;
        QMetaObject::invokeMethod(
            this,
            [this, cameraIndex]() {
                processNext(cameraIndex);
            },
            Qt::QueuedConnection);
    }
}
```

```cpp
void ImageProcessorWorker::processNext(int cameraIndex)
{
    auto& state = m_cameraState[cameraIndex];

    if (!state.latest.isValid()) {
        state.processing = false;
        return;
    }

    CameraFrame frame = std::move(state.latest);
    state.latest = CameraFrame();

    processFrame(
        cameraIndex,
        frame.image,
        frame.frameId,
        frame.cameraTimestamp,
        state.generation);

    QMetaObject::invokeMethod(
        this,
        [this, cameraIndex]() {
            processNext(cameraIndex);
        },
        Qt::QueuedConnection);
}
```

### 为什么使用下一次排队调用

如果使用无限 `while` 循环持续处理：

- 相机 0 帧持续到达时可能长期占用线程；
- 相机 1 的任务可能得不到执行；
- 参数更新事件也可能饥饿。

每处理一帧后重新投递一次，可以让 Qt 事件循环在两台相机和配置事件之间进行调度。

---

## 6.3 每台相机公平调度

单线程处理双相机时应避免某一路占满事件队列。

可以增加轮转调度：

```cpp
int m_nextCamera = 0;
```

每次处理后优先检查另一台相机：

```text
处理相机0
    ↓
优先检查相机1
    ↓
处理相机1
    ↓
优先检查相机0
```

对于双相机同步测量，比单纯按任务到达顺序更稳定。

---

## 6.4 保留 generation 机制

当前 acquisition generation 应继续保留。

用途：

- 切换全画幅和 ROI 时丢弃旧任务；
- 停止后重新开始时丢弃旧任务；
- 自动曝光调整后丢弃旧曝光帧；
- 避免上一轮采集结果污染下一轮采集。

建议在提交帧和真正开始计算时都检查 generation。

---

## 6.5 减少不必要的图像复制

当前可能存在以下复制链：

```text
SDK 缓冲区
    ↓ memcpy
CameraManager cv::Mat
    ↓ frameCaptured 排队参数
GUI CameraFrame
    ↓ cropFrameForRoiProcessing
ROI cv::Mat
    ↓ ImageProcessor::processFrame clone()
worker cv::Mat
    ↓ preprocess clone()
processed cv::Mat
```

建议原则：

1. SDK 缓冲区必须复制；
2. 跨线程传输后，只要源数据生命周期可靠，可依赖 `cv::Mat` 引用计数；
3. 裁剪 ROI 时仅在确实需要独立连续内存时 clone；
4. ImageProcessor 提交帧时不要无条件再次 clone；
5. 处理函数内部尽量复用临时缓冲区。

### 安全传递示例

```cpp
CameraFrame packet;
packet.image = frame;  // 引用计数共享已复制的独立图像
```

Qt 排队事件持有 `cv::Mat` 副本，但共享底层数据。

前提是之后没有线程原地修改该图像。

如果算法会原地修改，才在算法入口处 clone。

---

## 7. 第三阶段：北极星求解线程优化

## 7.1 方案 A：保留单线程

适合以下情况：

- 对准模式不要求两台相机同时完成；
- 希望限制 CPU 占用；
- 满画幅图像较大；
- 星图匹配耗时可接受。

需要修改 UI 状态，使其真实反映：

```text
相机1：正在求解
相机2：等待求解线程
```

而不是两台相机都显示“正在识别”。

### 增加全局队列状态

```cpp
int m_executingCameraIndex = -1;
```

当任务进入 worker 时：

```cpp
m_executingCameraIndex = cameraIndex;
```

另一台相机的状态显示为：

```text
已保留最新任务，等待相机1求解完成
```

## 7.2 方案 B：每台相机独立求解线程

适合以下情况：

- 要求双相机尽快完成识别；
- CPU 核心数量足够；
- 实测内存和 CPU 峰值可接受。

结构：

```cpp
QThread* m_workerThreads[2];
PolarisSolverWorker* m_workers[2];
```

```text
相机0满画幅 → SolverWorker0 → SolverThread0
相机1满画幅 → SolverWorker1 → SolverThread1
```

每台相机仍保留“最新任务覆盖”机制。

### 注意事项

OpenCV 可能内部使用线程。两个 solver 同时运行时，可能造成 CPU 过度订阅。

需要测试：

- 双线程总耗时；
- 单线程串行总耗时；
- CPU 峰值；
- 内存峰值；
- GUI 响应；
- 相机回调是否受影响。

如果 OpenCV 内部线程过多，可考虑：

```cpp
cv::setNumThreads(n);
```

但必须通过实测决定，不应盲目限制。

---

## 7.3 增加细粒度取消检查

建议为长循环统一提供：

```cpp
inline bool shouldCancel(
    const std::shared_ptr<std::atomic_bool>& cancelled)
{
    return cancelled &&
           cancelled->load(std::memory_order_relaxed);
}
```

每处理若干行或若干候选后检查：

```cpp
for (int y = 0; y < image.rows; ++y) {
    if ((y & 31) == 0 && shouldCancel(cancelled)) {
        return cancelledResult();
    }

    // 行处理
}
```

候选匹配循环：

```cpp
for (int i = 0; i < candidates.size(); ++i) {
    if ((i & 63) == 0 && shouldCancel(cancelled)) {
        return cancelledResult();
    }

    // 候选测试
}
```

不建议每个像素都读取 atomic，否则会增加额外开销。

---

## 7.4 关闭流程

当前：

```cpp
cancelCurrentSolveTasks();
m_workerThread->quit();
m_workerThread->wait();
```

建议增加：

1. 请求取消；
2. 停止接受新任务；
3. 清空 pending latest；
4. 等待线程；
5. 记录等待耗时；
6. debug 构建下对超时报警。

示例：

```cpp
PolarisSolverController::~PolarisSolverController()
{
    m_acceptTasks = false;
    ++m_generation;
    cancelCurrentSolveTasks();

    for (auto& task : m_pendingLatestTask) {
        task = PendingSolveTask();
    }

    if (!m_workerThread) {
        return;
    }

    m_workerThread->quit();

    if (!m_workerThread->wait(5000)) {
        qCritical()
            << "Polaris solver thread failed to stop within 5 seconds";

        m_workerThread->requestInterruption();

        // 正式版本中应继续安全等待，而不是直接销毁运行中的线程。
        m_workerThread->wait();
    }
}
```

不能使用 `terminate()` 作为正常关闭手段，因为它可能在线程持锁或执行 OpenCV 内部操作时强行终止。

---

## 8. 第四阶段：EAF 线程生命周期修复

## 8.1 检查 wait 返回值

建议：

```cpp
void EafFocuserManager::shutdown()
{
    if (!m_workerThread) {
        return;
    }

    if (m_worker &&
        m_workerThread->isRunning()) {
        QMetaObject::invokeMethod(
            m_worker,
            "doShutdown",
            Qt::BlockingQueuedConnection);
    }

    m_workerThread->quit();

    const bool stopped =
        m_workerThread->wait(3000);

    if (!stopped) {
        qCritical()
            << "EAF worker thread did not stop within 3000 ms";

        m_workerThread->requestInterruption();

        // 不得在此时删除 m_sdk。
        m_workerThread->wait();
    }

    m_workerThread = nullptr;
    m_worker = nullptr;
}
```

## 8.2 避免重复 shutdown

当前 `DIMM::~DIMM()` 会主动调用：

```cpp
m_focuserManager->shutdown();
```

而 `EafFocuserManager::~EafFocuserManager()` 又会调用一次。

`shutdown()` 应保持幂等：

```cpp
if (!m_workerThread) {
    return;
}
```

当前已有基本幂等判断，应继续保持。

## 8.3 移除冗余 BlockingQueuedConnection

`openDeviceForSlot()` 中先同步关闭旧设备：

```cpp
invokeMethod(
    m_worker,
    "doCloseDevice",
    Qt::BlockingQueuedConnection,
    ...);
```

随后 `doOpenDevice()` 内部又检查并关闭旧设备。

建议把“关闭旧设备并打开新设备”作为 worker 内的一个原子命令：

```cpp
void doReplaceDevice(
    TelescopeSlot slot,
    int enumerationIndex,
    int deviceId,
    QString serialHex);
```

GUI 只排队一个命令：

```cpp
QMetaObject::invokeMethod(
    m_worker,
    "doReplaceDevice",
    Qt::QueuedConnection,
    ...);
```

避免 GUI 被旧设备关闭操作阻塞。

---

## 9. 第五阶段：脉冲发生器异步化

## 9.1 推荐结构

```text
GUI
 ↓ queued command
PulseGeneratorWorker
 ↓
PulseGenerator QThread
 ↓
Win32 串口 ReadFile / WriteFile
```

类结构：

```cpp
class PulseGeneratorWorker : public QObject {
    Q_OBJECT

public slots:
    void applyConfig(PulseGeneratorConfig config);
    void start(PulseGeneratorConfig config);
    void stop();

signals:
    void commandFinished(QString command);
    void commandFailed(QString command, QString error);
    void runningChanged(bool running);
};
```

Manager 留在 GUI：

```cpp
class PulseGeneratorManager : public QObject {
    Q_OBJECT

private:
    QThread* m_workerThread = nullptr;
    PulseGeneratorWorker* m_worker = nullptr;
};
```

## 9.2 防止命令积压

脉冲板命令属于低频控制，不应简单无限排队。

可采用：

- 配置命令覆盖旧配置；
- stop 命令高优先级；
- 同一时刻只执行一个串口事务；
- 设备忙时 UI 禁止重复启动；
- 每个命令带 sequence ID；
- 过期结果不更新当前状态。

---

## 10. 第六阶段：相机控制线程

相机连接、枚举和参数设置目前大部分在 GUI 线程执行。

长期建议增加：

```text
CameraControlWorker
    ↓
独立 QThread
```

负责：

- 初始化 Galaxy SDK；
- 枚举设备；
- 打开和关闭设备；
- 配置曝光、增益和帧率；
- 设置触发模式；
- 切换全画幅和硬件 ROI；
- 启停采集。

### 需要注意

Galaxy SDK 的设备控制接口与回调接口是否允许跨线程使用，需要参考 SDK 文档和实际测试。

最安全的方式是：

- 所有设备控制 API 始终在同一个 CameraControlWorker 线程调用；
- 图像回调只负责复制数据和发出轻量通知；
- 回调中不调用控制接口；
- 关闭时先停止回调，再释放设备。

---

## 11. 第七阶段：文件写入线程

如果实测本地文件写入会影响 GUI，可增加 `ResultWriterWorker`。

### 数据流

```text
GUI / measurement result
    ↓
轻量 MeasurementRecord
    ↓
ResultWriterWorker
    ↓
批量写文件
```

### 队列策略

结果数据通常不能像图像帧一样随意丢弃，应采用有界批量队列：

- 正常情况下全部写入；
- 队列超过警戒线时发出告警；
- 文件异常时停止继续增长；
- 可设置最大缓存记录数；
- 关闭程序前执行阻塞 flush；
- 写入失败后把错误返回 GUI。

---

## 12. 推荐目标架构

## 12.1 保守方案

```text
Galaxy SDK callback threads
          ↓
CameraManager latest-frame slots
          ↓ frameReady
GUI thread
          ↓ bounded latest-frame submission
Single ImageProcessor thread
          ↓
centroid / pairing / atmosphere results
          ↓
GUI thread
```

同时：

```text
GUI → Single PolarisSolver thread
GUI → EAF worker thread
GUI → PulseGenerator worker thread
```

这个方案修改量相对较小，优先推荐。

## 12.2 高性能方案

```text
Galaxy callback camera0
    ↓
latest slot0
    ↓
CentroidWorker0
    ┐
    ├──→ PairingWorker → Atmosphere calculation
    ┘
CentroidWorker1
    ↑
latest slot1
    ↑
Galaxy callback camera1
```

另外：

```text
PolarisSolverWorker0
PolarisSolverWorker1
```

高性能方案能够真正并行处理两台相机，但会明显增加：

- 状态同步复杂度；
- 配对逻辑复杂度；
- CPU 使用；
- 内存带宽压力；
- 测试工作量。

在完成保守方案和性能测量前，不建议直接进入高性能方案。

---

## 13. 图像配对策略

双相机质心配对不能仅依赖任务处理顺序，因为两台相机的处理延迟可能不同。

建议继续使用：

- 相机帧 ID；
- 相机硬件时间戳；
- 初始 frame ID offset；
- 初始 timestamp offset。

### 建议配对规则

优先按硬件 frame ID：

```text
alignedFrameId0 = frameId0 + offset0
alignedFrameId1 = frameId1 + offset1
```

若帧 ID 不可靠，则使用硬件时间戳残差。

### 使用 latest-frame 背压后的影响

主动丢帧后，两台相机不一定每帧一一对应。

配对器应：

- 丢弃明显更旧的一侧；
- 统计 unpaired dropped；
- 记录配对时间戳残差；
- 不等待已经被覆盖的帧；
- 不因单侧丢帧导致队列无限增长。

---

## 14. 线程锁设计原则

## 14.1 不要用锁解决队列问题

锁只能保证：

- 数据不会同时被非法修改；
- 对象状态保持一致。

锁不能解决：

- 处理速度低于输入速度；
- 旧帧堆积；
- 延迟持续增长；
- 内存持续增长。

这些问题必须依靠：

- 有界队列；
- 最新帧覆盖；
- 背压；
- 丢弃策略；
- generation；
- cancellation。

## 14.2 临界区必须短

相机回调中的锁内操作应限制为：

```cpp
latestFramePacket = packet;
state update;
```

不应在锁内执行：

- OpenCV 处理；
- 文件写入；
- GUI 信号处理；
- Galaxy SDK 长耗时调用；
- 星图匹配。

## 14.3 固定锁顺序

如果未来出现同时需要多个锁的函数，应规定顺序：

```text
m_apiMutex
    ↓
camera.stateMutex
    ↓
camera.frameMutex
```

尽量避免同时持有多个锁。

当前多数代码只持有单个锁，这是正确方向。

---

## 15. 线程关闭顺序

推荐应用关闭顺序：

```text
1. GUI 停止接受新用户命令
2. 停止定时器
3. 停止相机采集
4. 注销相机回调
5. 等待 activeCallbacks 归零
6. 作废 ImageProcessor generation
7. 清空最新待处理帧
8. 请求北极星求解取消
9. 停止脉冲发生器输出
10. 停止 EAF 轮询和运动
11. flush 结果文件
12. quit 各工作线程
13. 检查 wait 返回值
14. 确认线程停止后释放 SDK 和 worker
15. 最后销毁 GUI
```

禁止：

- 在线程仍运行时删除其 worker；
- 在线程仍访问 SDK 时删除 SDK loader；
- 使用 `QThread::terminate()` 作为常规关闭方式；
- 在回调仍运行时释放相机 callback handler。

---

## 16. 性能监控指标

建议新增 `ThreadRuntimeMetrics`：

```cpp
struct ThreadRuntimeMetrics {
    quint64 cameraFramesReceived[2] = {0, 0};
    quint64 guiFramesConsumed[2] = {0, 0};
    quint64 processorFramesSubmitted[2] = {0, 0};
    quint64 processorFramesProcessed[2] = {0, 0};
    quint64 processorFramesReplaced[2] = {0, 0};

    double latestQueueDelayMs[2] = {0.0, 0.0};
    double averageQueueDelayMs[2] = {0.0, 0.0};
    double maxQueueDelayMs[2] = {0.0, 0.0};

    double latestProcessingMs[2] = {0.0, 0.0};
    double averageProcessingMs[2] = {0.0, 0.0};
    double maxProcessingMs[2] = {0.0, 0.0};

    double latestEndToEndMs[2] = {0.0, 0.0};
};
```

### 时间戳

`CameraFrame` 已有：

```cpp
qint64 receivedMs;
```

建议再增加高精度单调时钟时间：

```cpp
qint64 receivedSteadyNs;
```

不要只依赖 `QDateTime::currentMSecsSinceEpoch()` 测量短时延，因为系统时间可能被校准。

可以使用：

```cpp
QElapsedTimer
```

或者进程启动后的单调纳秒时间。

### 延迟定义

```text
queueDelay =
    processingStart - frameReceived

processingTime =
    processingEnd - processingStart

endToEnd =
    resultConsumedByGui - frameReceived
```

---

## 17. 验收标准

## 17.1 内存稳定性

连续运行 30 分钟：

- 内存不应线性增长；
- 停止采集后内存应回落或保持稳定；
- 满画幅定位阶段不应因 GUI 队列积压导致快速增长；
- 重复进入和退出对准模式不应持续泄漏。

## 17.2 处理延迟

在目标帧率下：

- 平均排队延迟保持稳定；
- 最大排队延迟不随运行时间持续增长；
- 主动丢帧发生时，应处理最新帧而不是旧帧；
- ROI 跟踪延迟应满足实际控制要求。

## 17.3 GUI 响应

以下操作期间 GUI 不应长时间无响应：

- 启动和停止采集；
- 脉冲板无应答；
- EAF 设备无响应；
- 双相机北极星求解；
- 文件写入；
- 相机连接失败。

## 17.4 关闭可靠性

重复执行 100 次：

```text
启动程序
连接相机
开始采集
停止采集
断开相机
关闭程序
```

应满足：

- 无崩溃；
- 无死锁；
- 无 `QThread: Destroyed while thread is still running`；
- 无 worker 访问已释放对象；
- 无相机 SDK 回调访问已释放 callback handler。

## 17.5 双相机同步

在主动丢帧模式下：

- 配对数量合理；
- dropped unpaired 统计正确；
- 不会无限等待另一台相机旧帧；
- 时间戳残差和同步抖动可观测；
- 配对错误不会污染大气参数计算。

---

## 18. 建议实施顺序

### P0：必须立即修改

- [x] 使用 `frameReady → onFrameReady` 作为相机主帧链路；
- [ ] 取消 `frameCaptured → onCapturedFramePacket` 的实时主连接；
- [x] ImageProcessor 增加每台相机最新帧覆盖和有界调度；
- [ ] 增加丢弃帧数和真实排队延迟统计；
- [x] EAF shutdown 检查 `wait()` 返回值；
- [x] 线程未退出前禁止释放 EAF SDK loader。

### P1：重要优化

- [x] 北极星检测长循环增加取消检查；
- [x] 明确 Polaris 单线程串行状态，或拆成双 worker；
- [x] 脉冲发生器串口移到独立线程；
- [ ] 停止采集时清空最新待处理帧；
- [ ] 采集状态切换时统一递增 generation；
- [x] 减少 ROI 和 worker 提交阶段的重复 clone。

### P2：进一步优化

- [ ] 相机枚举和设备控制移到 CameraControlWorker；
- [ ] 文件写入移到 ResultWriterWorker；
- [ ] 评估双相机独立质心线程；
- [ ] 将质心处理与双相机配对拆分；
- [ ] 增加线程运行状态调试页面；
- [ ] 增加自动压力测试。

---

## 19. 推荐首个提交范围

建议第一个线程优化提交只处理以下内容：

```text
1. CameraManager 的 frameReady 主链路接入；
2. 移除 DIMM 对 frameCaptured 的实时连接；
3. ImageProcessor latest-frame 背压；
4. 处理延迟、替换帧数和丢帧数统计；
5. 停止采集时清空 pending frame；
6. 保留现有单个 ImageProcessorWorker；
7. 不立即修改 Polaris 和 EAF 架构。
```

原因：

- 修改范围可控；
- 能直接解决最大风险；
- 不会同时引入过多线程；
- 容易通过模拟模式和真实相机测试；
- 可以先获得真实性能数据，再决定是否拆分双处理线程。

---

## 20. 预期效果

完成 P0 优化后，预期获得：

- 相机帧不会在 GUI 事件队列无限堆积；
- ImageProcessor 队列深度固定；
- 内存占用趋于稳定；
- 实际处理的是最新图像；
- ROI 跟踪响应更及时；
- 自动曝光依据更新图像；
- 停止采集响应更快；
- 程序退出等待时间下降；
- 长时间运行可靠性提高。

最终原则是：

> 实时测量系统不应追求处理每一张到达的图像，而应在计算能力有限时，优先保证处理最新、成对且时间有效的图像。
