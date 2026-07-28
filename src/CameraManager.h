#pragma once

#include <QObject>
#include <QList>
#include <QMetaType>
#include <QMutex>
#include <QWaitCondition>
#include <QVector>
#include <atomic>
#include <QString>
#include <memory>
#include <opencv2/opencv.hpp>

// Galaxy SDK头文件
#include "GalaxyIncludes.h"

// 触发模式枚举
enum class TriggerMode {
    Continuous = 0,   // 连续采集
    Software = 1,     // 软件触发
    Hardware = 2      // 硬件触发
};

// 相机信息结构
struct CameraInfo {
    QString serialNumber;
    QString modelName;
    QString ipAddress;
    bool isOnline = false;
    double frameRate = 0.0;
    double temperature = 0.0;
};

struct RoiAxisRange {
    qint64 minValue = 0;
    qint64 maxValue = 0;
    qint64 increment = 1;
};

struct RoiCapability {
    qint64 width = 0;
    qint64 height = 0;
    RoiAxisRange offsetX;
    RoiAxisRange offsetY;
};

struct RoiPosition {
    qint64 x = 0;
    qint64 y = 0;
};

struct RoiUpdatePauseState {
    bool acquisitionStopped = false;
    bool streamStopped = false;
};

struct CameraFrame {
    cv::Mat image;
    GX_PIXEL_FORMAT_ENTRY pixelFormat = GX_PIXEL_FORMAT_UNDEFINED;
    int bitDepth = 0;
    double maxPixelValue = 0.0;
    quint64 frameId = 0;
    quint64 cameraTimestamp = 0;
    qint64 receivedMs = 0;

    bool isValid() const { return !image.empty(); }
};
Q_DECLARE_METATYPE(CameraFrame)

// 图像回调处理类
class CaptureCallbackHandler : public ICaptureEventHandler {
public:
    CaptureCallbackHandler(int cameraIndex, void* manager)
        : m_cameraIndex(cameraIndex), m_manager(manager) {}

    void DoOnImageCaptured(CImageDataPointer& objImageDataPointer, void* pUserParam) override;

private:
    int m_cameraIndex;
    void* m_manager;
};

// 相机管理器（单例）
class CameraManager : public QObject {
    Q_OBJECT

public:
    static CameraManager& instance();

    // 初始化/反初始化
    bool init();
    void uninit();

    // 设备枚举
    QVector<CameraInfo> enumerateDevices();

    // 设备打开/关闭
    bool openDevice(int index);
    bool closeDevice(int index);
    bool openAll();
    bool closeAll();

    // 采集控制
    bool startAcquisition(int index);
    bool stopAcquisition(int index);
    bool startAll();
    bool stopAll();

    // 参数控制
    bool setExposure(int index, double us);
    bool setGain(int index, double dB);
    bool setFrameRate(int index, double fps);
    bool setTriggerMode(int index, TriggerMode mode);
    bool configureExternalTrigger(int index,
                                  const QString& inputLine = QStringLiteral("Line0"),
                                  const QString& triggerActivation = QStringLiteral("RisingEdge"));
    bool configureSoftwareTrigger(int index);
    bool prepareTriggerInputLine(int index, const QString& inputLine);
    bool setTriggerSource(int index, const QString& inputLine);
    bool setPairTriggerSource(const QString& inputLine);
    bool flushStreamQueue(int index);
    bool flushPairQueues();
    bool prepareFullFrame(int index);
    bool prepareFixedRoi(int index, qint64 requestedWidth, qint64 requestedHeight, RoiCapability* capability = nullptr);
    bool readRoiPosition(int index, RoiPosition* position);
    bool moveRoi(int index, const RoiPosition& position);
    bool pauseForRoiUpdate(int index, RoiUpdatePauseState* pauseState = nullptr);
    bool resumeAfterRoiUpdate(int index, const RoiUpdatePauseState& pauseState);
    bool pausePairForRoiUpdate(RoiUpdatePauseState pauseState[2]);
    bool resumePairAfterRoiUpdate(const RoiUpdatePauseState pauseState[2]);

    double getExposure(int index);
    double getGain(int index);
    double getFrameRate(int index);
    double getTemperature(int index);
    QString getSerialNumber(int index) const;
    QString getModelName(int index) const;

    // 图像获取
    cv::Mat getLatestFrame(int index);
    cv::Mat takeLatestFrame(int index);
    CameraFrame takeLatestFramePacket(int index);

    // 回调处理（由CaptureCallbackHandler调用）
    void onFrameCaptured(int cameraIndex, CImageDataPointer& imageData);

    // 状态查询
    bool isOpen(int index) const;
    bool isStreaming(int index) const;
    int deviceCount() const;

signals:
    void frameCaptured(int cameraIndex, CameraFrame frame);
    void frameReady(int cameraIndex);
    void cameraConnected(int index, QString serial, QString model);
    void cameraDisconnected(int index);
    void cameraError(int index, int errorCode, QString message);

private:
    CameraManager(QObject* parent = nullptr);
    ~CameraManager();
    Q_DISABLE_COPY_MOVE(CameraManager)

    // 相机实例数据
    struct CameraData {
        CGXDevicePointer device;
        CGXStreamPointer stream;
        CGXFeatureControlPointer remoteFeatureControl;
        std::unique_ptr<CaptureCallbackHandler> callbackHandler;
        bool isOpen = false;
        bool isStreaming = false;
        bool isClosing = false;
        int activeCallbacks = 0;
        cv::Mat latestFrame;
        CameraFrame latestFramePacket;
        mutable QMutex stateMutex;
        QMutex frameMutex;
        std::atomic_bool frameNotificationPending = false;
        QWaitCondition callbackDrained;
        CameraInfo info;
    };

    CameraData m_cameras[2];
    mutable QMutex m_apiMutex;
    bool m_initialized = false;
    int m_deviceCount = 0;
};
