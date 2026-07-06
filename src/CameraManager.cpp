#include "CameraManager.h"
#include <QDebug>

// ============================================================
// CaptureCallbackHandler
// ============================================================

void CaptureCallbackHandler::DoOnImageCaptured(CImageDataPointer& objImageDataPointer, void* pUserParam)
{
    Q_UNUSED(pUserParam);
    auto* mgr = static_cast<CameraManager*>(m_manager);
    mgr->onFrameCaptured(m_cameraIndex, objImageDataPointer);
}

// ============================================================
// CameraManager 单例
// ============================================================

CameraManager& CameraManager::instance()
{
    static CameraManager mgr;
    return mgr;
}

CameraManager::CameraManager(QObject* parent)
    : QObject(parent)
{
}

CameraManager::~CameraManager()
{
    uninit();
}

// ============================================================
// 初始化/反初始化
// ============================================================

bool CameraManager::init()
{
    if (m_initialized) return true;
    try {
        qDebug() << "[CameraManager] 正在初始化Galaxy SDK...";
        IGXFactory::GetInstance().Init();
        m_initialized = true;
        qDebug() << "[CameraManager] Galaxy SDK初始化成功";
        return true;
    } catch (CGalaxyException& e) {
        qDebug() << "[CameraManager] SDK初始化失败, 错误码:" << e.GetErrorCode();
        emit cameraError(-1, e.GetErrorCode(), "SDK初始化失败");
        return false;
    }
}

void CameraManager::uninit()
{
    if (!m_initialized) return;
    try {
        closeAll();
        IGXFactory::GetInstance().Uninit();
        m_initialized = false;
    } catch (...) {
        // 忽略反初始化异常
    }
}

// ============================================================
// 设备枚举
// ============================================================

QVector<CameraInfo> CameraManager::enumerateDevices()
{
    QVector<CameraInfo> result;
    if (!m_initialized) {
        qDebug() << "[CameraManager] enumerateDevices: SDK未初始化!";
        return result;
    }

    try {
        qDebug() << "[CameraManager] 正在枚举设备(超时1秒)...";
        GxIAPICPP::gxdeviceinfo_vector deviceInfoVector;
        IGXFactory::GetInstance().UpdateDeviceList(1000, deviceInfoVector);
        m_deviceCount = static_cast<int>(deviceInfoVector.size());
        qDebug() << "[CameraManager] 发现设备数量:" << m_deviceCount;

        for (int i = 0; i < m_deviceCount && i < 2; ++i) {
            CameraInfo info;
            info.serialNumber = QString::fromStdString(
                std::string(deviceInfoVector[i].GetSN()));
            info.modelName = QString::fromStdString(
                std::string(deviceInfoVector[i].GetModelName()));
            info.ipAddress = QString::fromStdString(
                std::string(deviceInfoVector[i].GetIP()));
            info.isOnline = true;
            m_cameras[i].info = info;
            result.append(info);
            qDebug() << "[CameraManager] 设备" << i << ": SN=" << info.serialNumber
                     << "型号=" << info.modelName << "IP=" << info.ipAddress;
        }
    } catch (CGalaxyException& e) {
        qDebug() << "[CameraManager] 设备枚举失败, 错误码:" << e.GetErrorCode();
        emit cameraError(-1, e.GetErrorCode(), "设备枚举失败");
    }
    return result;
}

// ============================================================
// 设备打开/关闭
// ============================================================

bool CameraManager::openDevice(int index)
{
    if (index < 0 || index >= 2) return false;
    if (!m_initialized) {
        qDebug() << "[CameraManager] openDevice(" << index << "): SDK未初始化!";
        return false;
    }
    if (m_cameras[index].isOpen) return true;

    try {
        // 枚举设备
        qDebug() << "[CameraManager] openDevice(" << index << "): 重新枚举设备...";
        GxIAPICPP::gxdeviceinfo_vector deviceInfoVector;
        IGXFactory::GetInstance().UpdateDeviceList(1000, deviceInfoVector);
        qDebug() << "[CameraManager] openDevice: 枚举到" << deviceInfoVector.size() << "个设备";
        if (index >= static_cast<int>(deviceInfoVector.size())) {
            qDebug() << "[CameraManager] openDevice: 索引" << index << "超出范围";
            emit cameraError(index, -1, "设备索引超出范围");
            return false;
        }

        // 打开设备
        GxIAPICPP::gxstring sn = deviceInfoVector[index].GetSN();
        qDebug() << "[CameraManager] openDevice: 尝试打开设备 SN=" << QString::fromStdString(std::string(sn));
        m_cameras[index].device = IGXFactory::GetInstance().OpenDeviceBySN(
            sn, GX_ACCESS_EXCLUSIVE);
        qDebug() << "[CameraManager] openDevice: 设备打开成功";

        // 获取远程设备控制
        m_cameras[index].remoteFeatureControl =
            m_cameras[index].device->GetRemoteFeatureControl();

        // 打开流通道
        m_cameras[index].stream = m_cameras[index].device->OpenStream(0);

        // 注册回调
        m_cameras[index].callbackHandler = std::make_unique<CaptureCallbackHandler>(index, this);
        m_cameras[index].stream->RegisterCaptureCallback(
            m_cameras[index].callbackHandler.get(), nullptr);

        m_cameras[index].isOpen = true;

        // 更新信息
        m_cameras[index].info.serialNumber = QString::fromStdString(
            std::string(deviceInfoVector[index].GetSN()));
        m_cameras[index].info.modelName = QString::fromStdString(
            std::string(deviceInfoVector[index].GetModelName()));
        m_cameras[index].info.ipAddress = QString::fromStdString(
            std::string(deviceInfoVector[index].GetIP()));
        m_cameras[index].info.isOnline = true;

        emit cameraConnected(index,
            m_cameras[index].info.serialNumber,
            m_cameras[index].info.modelName);
        return true;

    } catch (CGalaxyException& e) {
        qDebug() << "[CameraManager] openDevice失败, 错误码:" << e.GetErrorCode();
        emit cameraError(index, e.GetErrorCode(), "打开设备失败");
        return false;
    }
}

bool CameraManager::closeDevice(int index)
{
    if (index < 0 || index >= 2) return false;
    if (!m_cameras[index].isOpen) return true;

    try {
        // 停止采集
        if (m_cameras[index].isStreaming) {
            stopAcquisition(index);
        }

        // 注销回调
        if (m_cameras[index].callbackHandler) {
            m_cameras[index].stream->UnregisterCaptureCallback();
            // 延迟释放handler，避免SDK内部仍持有指针
            m_cameras[index].callbackHandler.reset();
        }

        // 关闭设备（先关设备，再释放流）
        m_cameras[index].device->Close();
        m_cameras[index].remoteFeatureControl = CGXFeatureControlPointer();
        m_cameras[index].stream = CGXStreamPointer();
        m_cameras[index].device = CGXDevicePointer();

        m_cameras[index].isOpen = false;
        m_cameras[index].info.isOnline = false;

        emit cameraDisconnected(index);
        return true;

    } catch (CGalaxyException& e) {
        // 异常路径：强制重置所有指针，确保资源释放
        qDebug() << "[CameraManager] closeDevice异常, 相机" << index << "错误码:" << e.GetErrorCode();
        m_cameras[index].callbackHandler.reset();
        m_cameras[index].stream = CGXStreamPointer();
        m_cameras[index].device = CGXDevicePointer();
        m_cameras[index].remoteFeatureControl = CGXFeatureControlPointer();
        m_cameras[index].isOpen = false;
        m_cameras[index].info.isOnline = false;
        emit cameraError(index, e.GetErrorCode(), "关闭设备失败");
        return false;
    }
}

bool CameraManager::openAll()
{
    bool success = true;
    enumerateDevices();
    for (int i = 0; i < m_deviceCount && i < 2; ++i) {
        if (!openDevice(i)) success = false;
    }
    return success;
}

bool CameraManager::closeAll()
{
    bool success = true;
    for (int i = 0; i < 2; ++i) {
        if (m_cameras[i].isOpen) {
            if (!closeDevice(i)) success = false;
        }
    }
    return success;
}

// ============================================================
// 采集控制
// ============================================================

bool CameraManager::startAcquisition(int index)
{
    if (index < 0 || index >= 2) return false;
    if (!m_cameras[index].isOpen) return false;
    if (m_cameras[index].isStreaming) return true;

    try {
        // GenICam标准流程：先 StartGrab（告诉驱动层开始拉流），再 AcquisitionStart（让相机开始曝光）
        m_cameras[index].stream->StartGrab();
        qDebug() << "[CameraManager] StartGrab 成功, 相机" << index;

        auto& fc = m_cameras[index].remoteFeatureControl;
        fc->GetCommandFeature("AcquisitionStart")->Execute();
        qDebug() << "[CameraManager] AcquisitionStart 已发送, 相机" << index;

        m_cameras[index].isStreaming = true;
        return true;
    } catch (CGalaxyException& e) {
        qDebug() << "[CameraManager] startAcquisition失败, 相机" << index << "错误码:" << e.GetErrorCode();
        emit cameraError(index, e.GetErrorCode(), "开始采集失败");
        return false;
    }
}

bool CameraManager::stopAcquisition(int index)
{
    if (index < 0 || index >= 2) return false;
    if (!m_cameras[index].isStreaming) return true;

    try {
        // GenICam标准流程：先 AcquisitionStop（让相机停止曝光），再 StopGrab（停止驱动层拉流）
        auto& fc = m_cameras[index].remoteFeatureControl;
        fc->GetCommandFeature("AcquisitionStop")->Execute();

        m_cameras[index].stream->StopGrab();
        m_cameras[index].isStreaming = false;
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), "停止采集失败");
        return false;
    }
}

bool CameraManager::startAll()
{
    bool success = true;
    for (int i = 0; i < 2; ++i) {
        if (m_cameras[i].isOpen && !startAcquisition(i)) success = false;
    }
    return success;
}

bool CameraManager::stopAll()
{
    bool success = true;
    for (int i = 0; i < 2; ++i) {
        if (m_cameras[i].isStreaming && !stopAcquisition(i)) success = false;
    }
    return success;
}

// ============================================================
// 参数控制
// ============================================================

bool CameraManager::setExposure(int index, double us)
{
    if (index < 0 || index >= 2) return false;
    if (!m_cameras[index].isOpen) return false;
    try {
        auto& fc = m_cameras[index].remoteFeatureControl;
        fc->GetFloatFeature("ExposureTime")->SetValue(us);
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), "设置曝光失败");
        return false;
    }
}

bool CameraManager::setGain(int index, double dB)
{
    if (index < 0 || index >= 2) return false;
    if (!m_cameras[index].isOpen) return false;
    try {
        auto& fc = m_cameras[index].remoteFeatureControl;
        fc->GetFloatFeature("Gain")->SetValue(dB);
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), "设置增益失败");
        return false;
    }
}

bool CameraManager::setFrameRate(int index, double fps)
{
    if (index < 0 || index >= 2) return false;
    if (!m_cameras[index].isOpen) return false;
    try {
        auto& fc = m_cameras[index].remoteFeatureControl;
        // 先使能帧率控制
        if (fc->IsImplemented("AcquisitionFrameRateEnable")) {
            fc->GetBoolFeature("AcquisitionFrameRateEnable")->SetValue(true);
        }
        fc->GetFloatFeature("AcquisitionFrameRate")->SetValue(fps);
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), "设置帧率失败");
        return false;
    }
}

bool CameraManager::setTriggerMode(int index, TriggerMode mode)
{
    if (index < 0 || index >= 2) return false;
    if (!m_cameras[index].isOpen) return false;
    try {
        auto& fc = m_cameras[index].remoteFeatureControl;
        switch (mode) {
        case TriggerMode::Continuous:
            fc->GetEnumFeature("TriggerMode")->SetValue("Off");
            break;
        case TriggerMode::Software:
            fc->GetEnumFeature("TriggerMode")->SetValue("On");
            fc->GetEnumFeature("TriggerSource")->SetValue("Software");
            break;
        case TriggerMode::Hardware:
            fc->GetEnumFeature("TriggerMode")->SetValue("On");
            fc->GetEnumFeature("TriggerSource")->SetValue("Line0");
            break;
        }
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), "设置触发模式失败");
        return false;
    }
}

// ============================================================
// 参数读取
// ============================================================

double CameraManager::getExposure(int index)
{
    if (index < 0 || index >= 2 || !m_cameras[index].isOpen) return 0.0;
    try {
        return m_cameras[index].remoteFeatureControl
            ->GetFloatFeature("ExposureTime")->GetValue();
    } catch (...) { return 0.0; }
}

double CameraManager::getGain(int index)
{
    if (index < 0 || index >= 2 || !m_cameras[index].isOpen) return 0.0;
    try {
        return m_cameras[index].remoteFeatureControl
            ->GetFloatFeature("Gain")->GetValue();
    } catch (...) { return 0.0; }
}

double CameraManager::getFrameRate(int index)
{
    if (index < 0 || index >= 2 || !m_cameras[index].isOpen) return 0.0;
    try {
        return m_cameras[index].remoteFeatureControl
            ->GetFloatFeature("AcquisitionFrameRate")->GetValue();
    } catch (...) { return 0.0; }
}

double CameraManager::getTemperature(int index)
{
    if (index < 0 || index >= 2 || !m_cameras[index].isOpen) return 0.0;
    try {
        return m_cameras[index].remoteFeatureControl
            ->GetFloatFeature("DeviceTemperature")->GetValue();
    } catch (...) { return 0.0; }
}

QString CameraManager::getSerialNumber(int index) const
{
    if (index < 0 || index >= 2) return "";
    return m_cameras[index].info.serialNumber;
}

QString CameraManager::getModelName(int index) const
{
    if (index < 0 || index >= 2) return "";
    return m_cameras[index].info.modelName;
}

// ============================================================
// 图像获取
// ============================================================

cv::Mat CameraManager::getLatestFrame(int index)
{
    if (index < 0 || index >= 2) return cv::Mat();
    QMutexLocker locker(&m_cameras[index].frameMutex);
    return m_cameras[index].latestFrame.clone();
}

// ============================================================
// 状态查询
// ============================================================

bool CameraManager::isOpen(int index) const
{
    if (index < 0 || index >= 2) return false;
    return m_cameras[index].isOpen;
}

bool CameraManager::isStreaming(int index) const
{
    if (index < 0 || index >= 2) return false;
    return m_cameras[index].isStreaming;
}

int CameraManager::deviceCount() const
{
    return m_deviceCount;
}

// ============================================================
// 回调处理
// ============================================================

void CameraManager::onFrameCaptured(int cameraIndex, CImageDataPointer& imageData)
{
    if (cameraIndex < 0 || cameraIndex >= 2) return;

    try {
        // 检查帧状态
        GX_FRAME_STATUS status = imageData->GetStatus();
        if (status != GX_FRAME_STATUS_SUCCESS) {
            qDebug() << "[CameraManager] 帧状态异常, 相机" << cameraIndex << "状态:" << status;
            return;
        }

        uint64_t width = imageData->GetWidth();
        uint64_t height = imageData->GetHeight();
        GX_PIXEL_FORMAT_ENTRY pixelFormat = imageData->GetPixelFormat();

        // 先深拷贝原始缓冲区，避免SDK复用缓冲区导致数据竞争
        cv::Mat frame;
        if (pixelFormat == GX_PIXEL_FORMAT_MONO8) {
            // 直接拷贝原始数据
            size_t bufSize = width * height;
            frame = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC1);
            memcpy(frame.data, imageData->GetBuffer(), bufSize);
        } else if (pixelFormat == GX_PIXEL_FORMAT_BAYER_RG8 ||
                   pixelFormat == GX_PIXEL_FORMAT_BAYER_GB8 ||
                   pixelFormat == GX_PIXEL_FORMAT_BAYER_GR8 ||
                   pixelFormat == GX_PIXEL_FORMAT_BAYER_BG8) {
            // Bayer转RGB（ConvertToRGB24内部使用SDK缓冲区，需立即拷贝）
            void* rgbBuffer = imageData->ConvertToRGB24(
                GX_BIT_0_7, GX_RAW2RGB_NEIGHBOUR, false);
            size_t bufSize = width * height * 3;
            frame = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC3);
            memcpy(frame.data, rgbBuffer, bufSize);
        } else {
            void* raw8Buffer = imageData->ConvertToRaw8(GX_BIT_0_7);
            size_t bufSize = width * height;
            frame = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC1);
            memcpy(frame.data, raw8Buffer, bufSize);
        }

        if (frame.empty()) {
            qDebug() << "[CameraManager] 帧转换后为空, 相机" << cameraIndex;
            return;
        }

        static int s_frameCount[2] = {0, 0};
        s_frameCount[cameraIndex]++;
        if (s_frameCount[cameraIndex] % 100 == 1) {
            qDebug() << "[CameraManager] 收到帧 #" << s_frameCount[cameraIndex]
                     << "相机" << cameraIndex
                     << "尺寸:" << frame.cols << "x" << frame.rows
                     << "通道:" << frame.channels();
        }

        // 更新最新帧
        {
            QMutexLocker locker(&m_cameras[cameraIndex].frameMutex);
            m_cameras[cameraIndex].latestFrame = frame;
        }

        // 注意：帧率和温度不在回调中查询（线程安全），
        // 改在主线程的onFrameReady中定时更新

        // 发送信号
        emit frameReady(cameraIndex, frame);

    } catch (CGalaxyException& e) {
        emit cameraError(cameraIndex, e.GetErrorCode(), "图像回调处理异常");
    } catch (...) {
        emit cameraError(cameraIndex, -1, "图像回调未知异常");
    }
}
