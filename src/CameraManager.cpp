#include "CameraManager.h"

#include <QDebug>
#include <QDateTime>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {
constexpr quint64 kFrameIdWrapModulo = 65536;
constexpr quint64 kFrameIdWrapBackstepThreshold = kFrameIdWrapModulo / 2;

qint64 safeIncrement(qint64 increment)
{
    return increment > 0 ? increment : 1;
}

qint64 alignToRange(qint64 value, const RoiAxisRange& range)
{
    const qint64 increment = safeIncrement(range.increment);
    const qint64 clamped = std::clamp(value, range.minValue, range.maxValue);
    const qint64 steps = (clamped - range.minValue) / increment;
    const qint64 aligned = range.minValue + steps * increment;
    return std::clamp(aligned, range.minValue, range.maxValue);
}

bool isFeatureReadable(CGXFeatureControlPointer& fc, const char* nodeName)
{
    return !fc.IsNull() && fc->IsImplemented(nodeName) && fc->IsReadable(nodeName);
}

bool isFeatureWritable(CGXFeatureControlPointer& fc, const char* nodeName)
{
    return !fc.IsNull() && fc->IsImplemented(nodeName) && fc->IsWritable(nodeName);
}

bool setEnumIfWritable(CGXFeatureControlPointer& fc, const char* nodeName, const char* value)
{
    if (!isFeatureWritable(fc, nodeName)) {
        return false;
    }
    CEnumFeaturePointer feature = fc->GetEnumFeature(nodeName);
    feature->SetValue(value);
    return true;
}

QString readEnumIfReadable(CGXFeatureControlPointer& fc, const char* nodeName)
{
    if (!isFeatureReadable(fc, nodeName)) {
        return QString();
    }
    CEnumFeaturePointer feature = fc->GetEnumFeature(nodeName);
    const GxIAPICPP::gxstring value = feature->GetValue();
    return QString::fromLatin1(value.c_str());
}

bool is16BitMonoPixelFormat(GX_PIXEL_FORMAT_ENTRY pixelFormat)
{
    return pixelFormat == GX_PIXEL_FORMAT_MONO10 ||
           pixelFormat == GX_PIXEL_FORMAT_MONO12 ||
           pixelFormat == GX_PIXEL_FORMAT_MONO16;
}

int cameraFrameBitDepth(GX_PIXEL_FORMAT_ENTRY pixelFormat)
{
    switch (pixelFormat) {
    case GX_PIXEL_FORMAT_MONO8:
    case GX_PIXEL_FORMAT_BAYER_RG8:
    case GX_PIXEL_FORMAT_BAYER_GB8:
    case GX_PIXEL_FORMAT_BAYER_GR8:
    case GX_PIXEL_FORMAT_BAYER_BG8:
        return 8;
    case GX_PIXEL_FORMAT_MONO10:
        return 10;
    case GX_PIXEL_FORMAT_MONO12:
        return 12;
    case GX_PIXEL_FORMAT_MONO16:
        return 16;
    default:
        return 8;
    }
}

double cameraFrameMaxPixelValue(GX_PIXEL_FORMAT_ENTRY pixelFormat)
{
    switch (cameraFrameBitDepth(pixelFormat)) {
    case 10:
        return 1023.0;
    case 12:
        return 4095.0;
    case 16:
        return 65535.0;
    case 8:
    default:
        return 255.0;
    }
}

RoiAxisRange readIntRangeChecked(CGXFeatureControlPointer& fc, const char* nodeName)
{
    CIntFeaturePointer feature = fc->GetIntFeature(nodeName);
    RoiAxisRange range;
    range.minValue = feature->GetMin();
    range.maxValue = feature->GetMax();
    range.increment = safeIncrement(feature->GetInc());
    return range;
}
}

void CaptureCallbackHandler::DoOnImageCaptured(CImageDataPointer& objImageDataPointer, void* pUserParam)
{
    Q_UNUSED(pUserParam);
    auto* mgr = static_cast<CameraManager*>(m_manager);
    if (mgr) {
        mgr->onFrameCaptured(m_cameraIndex, objImageDataPointer);
    }
}

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

void CameraManager::resetFrameIdTracking(CameraData& camera)
{
    camera.hasFrameIdTracking = false;
    camera.lastRawFrameId = 0;
    camera.frameIdWrapOffset = 0;
}

quint64 CameraManager::extendWrappingFrameId(CameraData& camera, quint64 rawFrameId)
{
    if (camera.hasFrameIdTracking &&
        rawFrameId < camera.lastRawFrameId &&
        camera.lastRawFrameId - rawFrameId > kFrameIdWrapBackstepThreshold) {
        camera.frameIdWrapOffset += kFrameIdWrapModulo;
    }

    camera.hasFrameIdTracking = true;
    camera.lastRawFrameId = rawFrameId;
    return camera.frameIdWrapOffset + rawFrameId + 1;
}

bool CameraManager::init()
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (m_initialized) {
        return true;
    }

    try {
        IGXFactory::GetInstance().Init();
        m_initialized = true;
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(-1, e.GetErrorCode(), QStringLiteral("Galaxy SDK 初始化失败"));
        return false;
    }
}

void CameraManager::uninit()
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (!m_initialized) {
        return;
    }

    for (int i = 0; i < 2; ++i) {
        auto& camera = m_cameras[i];
        {
            QMutexLocker stateLocker(&camera.stateMutex);
            camera.isClosing = true;
        }

        try {
            if (!camera.remoteFeatureControl.IsNull()) {
                try {
                    camera.remoteFeatureControl->GetCommandFeature("AcquisitionStop")->Execute();
                } catch (...) {
                }
            }
            if (!camera.stream.IsNull()) {
                try {
                    camera.stream->StopGrab();
                } catch (...) {
                }
                try {
                    camera.stream->UnregisterCaptureCallback();
                } catch (...) {
                }
            }
            {
                QMutexLocker stateLocker(&camera.stateMutex);
                camera.isStreaming = false;
                while (camera.activeCallbacks > 0) {
                    camera.callbackDrained.wait(&camera.stateMutex, 300);
                }
            }
            camera.callbackHandler.reset();
            if (!camera.device.IsNull()) {
                try {
                    camera.device->Close();
                } catch (...) {
                }
            }
        } catch (...) {
        }

        camera.remoteFeatureControl = CGXFeatureControlPointer();
        camera.stream = CGXStreamPointer();
        camera.device = CGXDevicePointer();
        {
            QMutexLocker frameLocker(&camera.frameMutex);
            camera.latestFrame.release();
            camera.latestFramePacket = CameraFrame();
        }
        {
            QMutexLocker stateLocker(&camera.stateMutex);
            camera.isOpen = false;
            camera.isStreaming = false;
            camera.isClosing = false;
            camera.info.isOnline = false;
            camera.frameNotificationPending.store(false, std::memory_order_release);
        }
    }

    try {
        IGXFactory::GetInstance().Uninit();
    } catch (...) {
    }
    m_initialized = false;
    m_deviceCount = 0;
}

QVector<CameraInfo> CameraManager::enumerateDevices()
{
    QMutexLocker apiLocker(&m_apiMutex);
    QVector<CameraInfo> result;
    if (!m_initialized) {
        return result;
    }

    try {
        GxIAPICPP::gxdeviceinfo_vector deviceInfoVector;
        IGXFactory::GetInstance().UpdateDeviceList(1000, deviceInfoVector);
        m_deviceCount = static_cast<int>(deviceInfoVector.size());

        for (int i = 0; i < m_deviceCount && i < 2; ++i) {
            CameraInfo info;
            info.serialNumber = QString::fromStdString(std::string(deviceInfoVector[i].GetSN()));
            info.modelName = QString::fromStdString(std::string(deviceInfoVector[i].GetModelName()));
            info.ipAddress = QString::fromStdString(std::string(deviceInfoVector[i].GetIP()));
            info.isOnline = true;
            m_cameras[i].info = info;
            result.append(info);
        }
    } catch (CGalaxyException& e) {
        emit cameraError(-1, e.GetErrorCode(), QStringLiteral("相机枚举失败"));
    }

    return result;
}

bool CameraManager::openDevice(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    {
        QMutexLocker stateLocker(&m_cameras[index].stateMutex);
        if (m_cameras[index].isOpen) {
            return true;
        }
    }

    try {
        GxIAPICPP::gxdeviceinfo_vector deviceInfoVector;
        IGXFactory::GetInstance().UpdateDeviceList(1000, deviceInfoVector);
        if (index >= static_cast<int>(deviceInfoVector.size())) {
            emit cameraError(index, -1, QStringLiteral("相机索引超出范围"));
            return false;
        }

        auto& camera = m_cameras[index];
        const GxIAPICPP::gxstring sn = deviceInfoVector[index].GetSN();
        camera.device = IGXFactory::GetInstance().OpenDeviceBySN(sn, GX_ACCESS_EXCLUSIVE);
        camera.remoteFeatureControl = camera.device->GetRemoteFeatureControl();
        camera.stream = camera.device->OpenStream(0);

        bool mono12Set = false;
        try {
            mono12Set = setEnumIfWritable(camera.remoteFeatureControl, "PixelFormat", "Mono12");
        } catch (...) {
            mono12Set = false;
        }
        QString pixelFormatValue;
        try {
            pixelFormatValue = readEnumIfReadable(camera.remoteFeatureControl, "PixelFormat");
        } catch (...) {
            pixelFormatValue.clear();
        }
        if (!mono12Set || (!pixelFormatValue.isEmpty() && pixelFormatValue != QStringLiteral("Mono12"))) {
            emit cameraError(index,
                             -1,
                             QStringLiteral("相机%1 PixelFormat 未确认到 Mono12，当前值: %2")
                                 .arg(index + 1)
                                 .arg(pixelFormatValue.isEmpty()
                                          ? QStringLiteral("unknown")
                                          : pixelFormatValue));
        }

        // For GigE cameras, negotiate a stable packet size before streaming.
        // Without this, dual-camera acquisition can degrade into one camera
        // receiving frames while the other silently starves.
        if (!camera.stream.IsNull() && !camera.remoteFeatureControl.IsNull() &&
            camera.remoteFeatureControl->IsImplemented("GevSCPSPacketSize") &&
            camera.remoteFeatureControl->IsWritable("GevSCPSPacketSize")) {
            try {
                const uint32_t packetSize = camera.stream->GetOptimalPacketSize();
                if (packetSize > 0U) {
                    camera.remoteFeatureControl->GetIntFeature("GevSCPSPacketSize")->SetValue(packetSize);
                }
            } catch (...) {
            }
        }

        // Stagger GigE transmission timing between the two cameras so a shared
        // trigger does not make both devices burst a full-frame image onto the
        // same NIC at the exact same instant.
        if (!camera.remoteFeatureControl.IsNull()) {
            try {
                if (camera.remoteFeatureControl->IsImplemented("GevSCFTD") &&
                    camera.remoteFeatureControl->IsWritable("GevSCFTD")) {
                    const int64_t frameTxDelay = (index == 0) ? 0 : 20000;
                    camera.remoteFeatureControl->GetIntFeature("GevSCFTD")->SetValue(frameTxDelay);
                }
            } catch (...) {
            }
            try {
                if (camera.remoteFeatureControl->IsImplemented("GevSCPD") &&
                    camera.remoteFeatureControl->IsWritable("GevSCPD")) {
                    const int64_t packetDelay = (index == 0) ? 1000 : 3000;
                    camera.remoteFeatureControl->GetIntFeature("GevSCPD")->SetValue(packetDelay);
                }
            } catch (...) {
            }
        }

        camera.callbackHandler = std::make_unique<CaptureCallbackHandler>(index, this);
        camera.stream->RegisterCaptureCallback(camera.callbackHandler.get(), nullptr);

        camera.info.serialNumber = QString::fromStdString(std::string(deviceInfoVector[index].GetSN()));
        camera.info.modelName = QString::fromStdString(std::string(deviceInfoVector[index].GetModelName()));
        camera.info.ipAddress = QString::fromStdString(std::string(deviceInfoVector[index].GetIP()));
        camera.info.isOnline = true;

        {
            QMutexLocker frameLocker(&camera.frameMutex);
            camera.latestFrame.release();
            camera.latestFramePacket = CameraFrame();
        }
        {
            QMutexLocker stateLocker(&camera.stateMutex);
            camera.isOpen = true;
            camera.isStreaming = false;
            camera.isClosing = false;
            camera.activeCallbacks = 0;
            camera.frameNotificationPending.store(false, std::memory_order_release);
        }

        emit cameraConnected(index, camera.info.serialNumber, camera.info.modelName);
        return true;
    } catch (CGalaxyException& e) {
        auto& camera = m_cameras[index];
        camera.callbackHandler.reset();
        camera.remoteFeatureControl = CGXFeatureControlPointer();
        camera.stream = CGXStreamPointer();
        camera.device = CGXDevicePointer();
        {
            QMutexLocker frameLocker(&camera.frameMutex);
            camera.latestFrame.release();
            camera.latestFramePacket = CameraFrame();
        }
        {
            QMutexLocker stateLocker(&camera.stateMutex);
            camera.isOpen = false;
            camera.isStreaming = false;
            camera.isClosing = false;
            camera.info.isOnline = false;
            camera.frameNotificationPending.store(false, std::memory_order_release);
        }
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("打开相机失败"));
        return false;
    }
}

bool CameraManager::closeDevice(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2) {
        return false;
    }

    auto& camera = m_cameras[index];
    {
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen) {
            return true;
        }
        camera.isClosing = true;
    }

    try {
        if (!camera.remoteFeatureControl.IsNull()) {
            try {
                camera.remoteFeatureControl->GetCommandFeature("AcquisitionStop")->Execute();
            } catch (...) {
            }
        }

        if (!camera.stream.IsNull()) {
            try {
                camera.stream->StopGrab();
            } catch (...) {
            }

            if (camera.callbackHandler) {
                try {
                    camera.stream->UnregisterCaptureCallback();
                } catch (...) {
                }
            }
        }

        {
            QMutexLocker stateLocker(&camera.stateMutex);
            camera.isStreaming = false;
            while (camera.activeCallbacks > 0) {
                camera.callbackDrained.wait(&camera.stateMutex, 300);
            }
        }

        camera.callbackHandler.reset();
        if (!camera.device.IsNull()) {
            camera.device->Close();
        }

        camera.remoteFeatureControl = CGXFeatureControlPointer();
        camera.stream = CGXStreamPointer();
        camera.device = CGXDevicePointer();

        {
            QMutexLocker stateLocker(&camera.stateMutex);
            camera.isOpen = false;
            camera.isStreaming = false;
            camera.isClosing = false;
            camera.info.isOnline = false;
            camera.frameNotificationPending.store(false, std::memory_order_release);
        }

        emit cameraDisconnected(index);
        return true;
    } catch (CGalaxyException& e) {
        camera.callbackHandler.reset();
        camera.remoteFeatureControl = CGXFeatureControlPointer();
        camera.stream = CGXStreamPointer();
        camera.device = CGXDevicePointer();
        {
            QMutexLocker stateLocker(&camera.stateMutex);
            camera.isOpen = false;
            camera.isStreaming = false;
            camera.isClosing = false;
            camera.info.isOnline = false;
        }
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("关闭相机失败"));
        return false;
    }
}

bool CameraManager::openAll()
{
    const auto devices = enumerateDevices();
    if (devices.isEmpty()) {
        return false;
    }

    bool success = true;
    for (int i = 0; i < devices.size() && i < 2; ++i) {
        if (!openDevice(i)) {
            success = false;
        }
    }
    return success;
}

bool CameraManager::closeAll()
{
    bool success = true;
    for (int i = 0; i < 2; ++i) {
        if (!closeDevice(i)) {
            success = false;
        }
    }
    return success;
}

bool CameraManager::startAcquisition(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2) {
        return false;
    }

    auto& camera = m_cameras[index];
    {
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing) {
            return false;
        }
        if (camera.isStreaming) {
            return true;
        }
    }

    try {
        if (camera.stream.IsNull() || camera.remoteFeatureControl.IsNull()) {
            emit cameraError(index, -1, QStringLiteral("相机未完成初始化"));
            return false;
        }

        {
            QMutexLocker stateLocker(&camera.stateMutex);
            resetFrameIdTracking(camera);
            camera.isStreaming = true;
        }

        camera.stream->StartGrab();
        camera.remoteFeatureControl->GetCommandFeature("AcquisitionStart")->Execute();
        return true;
    } catch (CGalaxyException& e) {
        {
            QMutexLocker stateLocker(&camera.stateMutex);
            camera.isStreaming = false;
        }
        if (!camera.stream.IsNull()) {
            try {
                camera.stream->StopGrab();
            } catch (...) {
            }
        }
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("开始采集失败"));
        return false;
    }
}

bool CameraManager::stopAcquisition(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2) {
        return false;
    }

    auto& camera = m_cameras[index];
    {
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isStreaming) {
            return true;
        }
    }

    try {
        if (!camera.remoteFeatureControl.IsNull()) {
            camera.remoteFeatureControl->GetCommandFeature("AcquisitionStop")->Execute();
        }
        if (!camera.stream.IsNull()) {
            camera.stream->StopGrab();
        }

        QMutexLocker stateLocker(&camera.stateMutex);
        camera.isStreaming = false;
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("停止采集失败"));
        return false;
    }
}

bool CameraManager::startAll()
{
    bool success = true;
    for (int i = 0; i < 2; ++i) {
        if (isOpen(i) && !startAcquisition(i)) {
            success = false;
        }
    }

    if (!success) {
        stopAll();
    }
    return success;
}

bool CameraManager::stopAll()
{
    bool success = true;
    for (int i = 0; i < 2; ++i) {
        if (!stopAcquisition(i)) {
            success = false;
        }
    }
    return success;
}

bool CameraManager::setExposure(int index, double us)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }
        camera.remoteFeatureControl->GetFloatFeature("ExposureTime")->SetValue(us);
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("设置曝光失败"));
        return false;
    }
}

bool CameraManager::setGain(int index, double dB)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }
        camera.remoteFeatureControl->GetFloatFeature("Gain")->SetValue(dB);
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("设置增益失败"));
        return false;
    }
}

bool CameraManager::setFrameRate(int index, double fps)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }
        auto& fc = camera.remoteFeatureControl;
        if (fc->IsImplemented("AcquisitionFrameRateEnable")) {
            fc->GetBoolFeature("AcquisitionFrameRateEnable")->SetValue(true);
        }
        fc->GetFloatFeature("AcquisitionFrameRate")->SetValue(fps);
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("设置帧率失败"));
        return false;
    }
}

bool CameraManager::setTriggerMode(int index, TriggerMode mode)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }
        auto& fc = camera.remoteFeatureControl;
        switch (mode) {
        case TriggerMode::Continuous:
            if (fc->IsImplemented("AcquisitionFrameRateEnable") && fc->IsWritable("AcquisitionFrameRateEnable")) {
                fc->GetBoolFeature("AcquisitionFrameRateEnable")->SetValue(true);
            }
            fc->GetEnumFeature("TriggerMode")->SetValue("Off");
            break;
        case TriggerMode::Software:
            setEnumIfWritable(fc, "AcquisitionMode", "Continuous");
            fc->GetEnumFeature("TriggerMode")->SetValue("On");
            if (fc->IsImplemented("TriggerSelector")) {
                fc->GetEnumFeature("TriggerSelector")->SetValue("FrameStart");
            }
            fc->GetEnumFeature("TriggerSource")->SetValue("Software");
            break;
        case TriggerMode::Hardware:
            setEnumIfWritable(fc, "AcquisitionMode", "Continuous");
            if (fc->IsImplemented("AcquisitionFrameRateEnable") && fc->IsWritable("AcquisitionFrameRateEnable")) {
                fc->GetBoolFeature("AcquisitionFrameRateEnable")->SetValue(false);
            }
            fc->GetEnumFeature("TriggerMode")->SetValue("On");
            if (fc->IsImplemented("TriggerSelector")) {
                fc->GetEnumFeature("TriggerSelector")->SetValue("FrameStart");
            }
            fc->GetEnumFeature("TriggerSource")->SetValue("Line0");
            break;
        }
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("设置触发模式失败"));
        return false;
    }
}

bool CameraManager::configureExternalTrigger(int index, const QString& inputLine, const QString& triggerActivation)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }

        auto& fc = camera.remoteFeatureControl;
        const std::string inputLineStd = inputLine.toStdString();
        const std::string triggerActivationStd = triggerActivation.toStdString();
        setEnumIfWritable(fc, "AcquisitionMode", "Continuous");
        if (fc->IsImplemented("AcquisitionFrameRateEnable") && fc->IsWritable("AcquisitionFrameRateEnable")) {
            fc->GetBoolFeature("AcquisitionFrameRateEnable")->SetValue(false);
        }
        setEnumIfWritable(fc, "TriggerMode", "Off");
        if (fc->IsImplemented("TriggerSelector")) {
            fc->GetEnumFeature("TriggerSelector")->SetValue("FrameStart");
        }
        fc->GetEnumFeature("LineSelector")->SetValue(inputLineStd.c_str());
        if (fc->IsImplemented("LineMode")) {
            fc->GetEnumFeature("LineMode")->SetValue("Input");
        }
        if (isFeatureWritable(fc, "LineInverter")) {
            fc->GetBoolFeature("LineInverter")->SetValue(false);
        }
        fc->GetEnumFeature("TriggerSource")->SetValue(inputLineStd.c_str());
        if (fc->IsImplemented("TriggerActivation")) {
            fc->GetEnumFeature("TriggerActivation")->SetValue(triggerActivationStd.c_str());
        }
        fc->GetEnumFeature("TriggerMode")->SetValue("On");
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("配置外部触发失败"));
        return false;
    }
}

bool CameraManager::configureSoftwareTrigger(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }

        auto& fc = camera.remoteFeatureControl;
        setEnumIfWritable(fc, "AcquisitionMode", "Continuous");
        setEnumIfWritable(fc, "TriggerMode", "Off");
        if (fc->IsImplemented("TriggerSelector")) {
            fc->GetEnumFeature("TriggerSelector")->SetValue("FrameStart");
        }
        fc->GetEnumFeature("TriggerSource")->SetValue("Software");
        fc->GetEnumFeature("TriggerMode")->SetValue("On");
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("配置软件触发失败"));
        return false;
    }
}

bool CameraManager::prepareTriggerInputLine(int index, const QString& inputLine)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }

        auto& fc = camera.remoteFeatureControl;
        const std::string inputLineStd = inputLine.toStdString();
        fc->GetEnumFeature("LineSelector")->SetValue(inputLineStd.c_str());
        if (fc->IsImplemented("LineMode")) {
            fc->GetEnumFeature("LineMode")->SetValue("Input");
        }
        if (isFeatureWritable(fc, "LineInverter")) {
            fc->GetBoolFeature("LineInverter")->SetValue(false);
        }
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("配置触发输入线失败"));
        return false;
    }
}

bool CameraManager::setTriggerSource(int index, const QString& inputLine)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }

        auto& fc = camera.remoteFeatureControl;
        if (fc->IsImplemented("TriggerSelector")) {
            fc->GetEnumFeature("TriggerSelector")->SetValue("FrameStart");
        }
        const std::string inputLineStd = inputLine.toStdString();
        fc->GetEnumFeature("TriggerSource")->SetValue(inputLineStd.c_str());
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("切换触发源失败"));
        return false;
    }
}

bool CameraManager::setPairTriggerSource(const QString& inputLine)
{
    bool success = true;
    if (!setTriggerSource(0, inputLine)) {
        success = false;
    }
    if (!setTriggerSource(1, inputLine)) {
        success = false;
    }
    return success;
}

bool CameraManager::flushStreamQueue(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        {
            QMutexLocker frameLocker(&camera.frameMutex);
            camera.latestFrame.release();
            camera.latestFramePacket = CameraFrame();
            camera.frameNotificationPending.store(false, std::memory_order_release);
        }
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.stream.IsNull()) {
            return false;
        }
        camera.stream->FlushQueue();
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("清空图像队列失败"));
        return false;
    }
}

bool CameraManager::flushPairQueues()
{
    bool success = true;
    if (!flushStreamQueue(0)) {
        success = false;
    }
    if (!flushStreamQueue(1)) {
        success = false;
    }
    return success;
}

bool CameraManager::prepareFullFrame(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }

        auto& fc = camera.remoteFeatureControl;
        fc->GetIntFeature("OffsetX")->SetValue(0);
        fc->GetIntFeature("OffsetY")->SetValue(0);
        const RoiAxisRange widthRange = readIntRangeChecked(fc, "Width");
        const RoiAxisRange heightRange = readIntRangeChecked(fc, "Height");
        fc->GetIntFeature("Width")->SetValue(widthRange.maxValue);
        fc->GetIntFeature("Height")->SetValue(heightRange.maxValue);
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("切换全画幅失败"));
        return false;
    }
}

bool CameraManager::prepareFixedRoi(int index, qint64 requestedWidth, qint64 requestedHeight, RoiCapability* capability)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }

        auto& fc = camera.remoteFeatureControl;
        fc->GetIntFeature("OffsetX")->SetValue(0);
        fc->GetIntFeature("OffsetY")->SetValue(0);

        const RoiAxisRange widthRange = readIntRangeChecked(fc, "Width");
        const RoiAxisRange heightRange = readIntRangeChecked(fc, "Height");
        const qint64 width = alignToRange(requestedWidth, widthRange);
        const qint64 height = alignToRange(requestedHeight, heightRange);
        fc->GetIntFeature("Width")->SetValue(width);
        fc->GetIntFeature("Height")->SetValue(height);

        if (capability) {
            capability->width = fc->GetIntFeature("Width")->GetValue();
            capability->height = fc->GetIntFeature("Height")->GetValue();
            capability->offsetX = readIntRangeChecked(fc, "OffsetX");
            capability->offsetY = readIntRangeChecked(fc, "OffsetY");
        }
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("配置固定ROI失败"));
        return false;
    }
}

bool CameraManager::readRoiPosition(int index, RoiPosition* position)
{
    if (!position) {
        return false;
    }

    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }
        position->x = camera.remoteFeatureControl->GetIntFeature("OffsetX")->GetValue();
        position->y = camera.remoteFeatureControl->GetIntFeature("OffsetY")->GetValue();
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("读取ROI位置失败"));
        return false;
    }
}

bool CameraManager::moveRoi(int index, const RoiPosition& position)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return false;
        }
        auto& fc = camera.remoteFeatureControl;
        const RoiAxisRange offsetXRange = readIntRangeChecked(fc, "OffsetX");
        const RoiAxisRange offsetYRange = readIntRangeChecked(fc, "OffsetY");
        fc->GetIntFeature("OffsetX")->SetValue(alignToRange(position.x, offsetXRange));
        fc->GetIntFeature("OffsetY")->SetValue(alignToRange(position.y, offsetYRange));
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("移动ROI失败"));
        return false;
    }
}

bool CameraManager::pauseForRoiUpdate(int index, RoiUpdatePauseState* pauseState)
{
    if (pauseState) {
        *pauseState = RoiUpdatePauseState();
    }

    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull() || camera.stream.IsNull()) {
            return false;
        }

        if (camera.isStreaming) {
            camera.remoteFeatureControl->GetCommandFeature("AcquisitionStop")->Execute();
            camera.isStreaming = false;
            if (pauseState) {
                pauseState->acquisitionStopped = true;
            }
            camera.stream->StopGrab();
            if (pauseState) {
                pauseState->streamStopped = true;
            }
        }

        if (!isFeatureWritable(camera.remoteFeatureControl, "OffsetX") ||
            !isFeatureWritable(camera.remoteFeatureControl, "OffsetY") ||
            !isFeatureWritable(camera.remoteFeatureControl, "Width") ||
            !isFeatureWritable(camera.remoteFeatureControl, "Height")) {
            return false;
        }
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("暂停采集以更新ROI失败"));
        return false;
    }
}

bool CameraManager::resumeAfterRoiUpdate(int index, const RoiUpdatePauseState& pauseState)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return false;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull() || camera.stream.IsNull()) {
            return false;
        }

        if (pauseState.streamStopped) {
            camera.stream->StartGrab();
        }
        if (pauseState.acquisitionStopped) {
            resetFrameIdTracking(camera);
            camera.remoteFeatureControl->GetCommandFeature("AcquisitionStart")->Execute();
            camera.isStreaming = true;
        }
        return true;
    } catch (CGalaxyException& e) {
        emit cameraError(index, e.GetErrorCode(), QStringLiteral("恢复采集失败"));
        return false;
    }
}

bool CameraManager::pausePairForRoiUpdate(RoiUpdatePauseState pauseState[2])
{
    if (!pauseState) {
        return false;
    }
    pauseState[0] = RoiUpdatePauseState();
    pauseState[1] = RoiUpdatePauseState();
    if (!pauseForRoiUpdate(0, &pauseState[0])) {
        return false;
    }
    if (!pauseForRoiUpdate(1, &pauseState[1])) {
        resumeAfterRoiUpdate(0, pauseState[0]);
        pauseState[0] = RoiUpdatePauseState();
        return false;
    }
    return true;
}

bool CameraManager::resumePairAfterRoiUpdate(const RoiUpdatePauseState pauseState[2])
{
    if (!pauseState) {
        return false;
    }

    bool success = true;
    if (!resumeAfterRoiUpdate(0, pauseState[0])) {
        success = false;
    }
    if (!resumeAfterRoiUpdate(1, pauseState[1])) {
        success = false;
    }
    return success;
}

double CameraManager::getExposure(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return 0.0;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return 0.0;
        }
        return camera.remoteFeatureControl->GetFloatFeature("ExposureTime")->GetValue();
    } catch (...) {
        return 0.0;
    }
}

double CameraManager::getGain(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return 0.0;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return 0.0;
        }
        return camera.remoteFeatureControl->GetFloatFeature("Gain")->GetValue();
    } catch (...) {
        return 0.0;
    }
}

double CameraManager::getFrameRate(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return 0.0;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return 0.0;
        }
        return camera.remoteFeatureControl->GetFloatFeature("AcquisitionFrameRate")->GetValue();
    } catch (...) {
        return 0.0;
    }
}

double CameraManager::getTemperature(int index)
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2 || !m_initialized) {
        return 0.0;
    }

    try {
        auto& camera = m_cameras[index];
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || camera.isClosing || camera.remoteFeatureControl.IsNull()) {
            return 0.0;
        }
        return camera.remoteFeatureControl->GetFloatFeature("DeviceTemperature")->GetValue();
    } catch (...) {
        return 0.0;
    }
}

QString CameraManager::getSerialNumber(int index) const
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2) {
        return QString();
    }
    return m_cameras[index].info.serialNumber;
}

QString CameraManager::getModelName(int index) const
{
    QMutexLocker apiLocker(&m_apiMutex);
    if (index < 0 || index >= 2) {
        return QString();
    }
    return m_cameras[index].info.modelName;
}

cv::Mat CameraManager::getLatestFrame(int index)
{
    if (index < 0 || index >= 2) {
        return cv::Mat();
    }

    QMutexLocker locker(&m_cameras[index].frameMutex);
    return m_cameras[index].latestFrame.clone();
}

cv::Mat CameraManager::takeLatestFrame(int index)
{
    if (index < 0 || index >= 2) {
        return cv::Mat();
    }

    auto& camera = m_cameras[index];
    QMutexLocker locker(&camera.frameMutex);
    camera.frameNotificationPending.store(false, std::memory_order_release);
    return camera.latestFrame.clone();
}

CameraFrame CameraManager::takeLatestFramePacket(int index)
{
    if (index < 0 || index >= 2) {
        return CameraFrame();
    }

    auto& camera = m_cameras[index];
    QMutexLocker locker(&camera.frameMutex);
    camera.frameNotificationPending.store(false, std::memory_order_release);
    return camera.latestFramePacket;
}

bool CameraManager::isOpen(int index) const
{
    if (index < 0 || index >= 2) {
        return false;
    }

    QMutexLocker locker(&m_cameras[index].stateMutex);
    return m_cameras[index].isOpen;
}

bool CameraManager::isStreaming(int index) const
{
    if (index < 0 || index >= 2) {
        return false;
    }

    QMutexLocker locker(&m_cameras[index].stateMutex);
    return m_cameras[index].isStreaming;
}

int CameraManager::deviceCount() const
{
    QMutexLocker apiLocker(&m_apiMutex);
    return m_deviceCount;
}

void CameraManager::onFrameCaptured(int cameraIndex, CImageDataPointer& imageData)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }

    auto& camera = m_cameras[cameraIndex];
    {
        QMutexLocker stateLocker(&camera.stateMutex);
        if (!camera.isOpen || !camera.isStreaming || camera.isClosing) {
            return;
        }
        ++camera.activeCallbacks;
    }

    const auto finishCallback = [&camera]() {
        QMutexLocker stateLocker(&camera.stateMutex);
        if (camera.activeCallbacks > 0) {
            --camera.activeCallbacks;
        }
        if (camera.activeCallbacks == 0) {
            camera.callbackDrained.wakeAll();
        }
    };

    try {
        const GX_FRAME_STATUS status = imageData->GetStatus();
        if (status != GX_FRAME_STATUS_SUCCESS) {
            finishCallback();
            return;
        }

        const uint64_t width = imageData->GetWidth();
        const uint64_t height = imageData->GetHeight();
        const GX_PIXEL_FORMAT_ENTRY pixelFormat = imageData->GetPixelFormat();
        const quint64 rawFrameId = static_cast<quint64>(imageData->GetFrameID());
        const quint64 cameraTimestamp = static_cast<quint64>(imageData->GetTimeStamp());
        const qint64 receivedMs = QDateTime::currentMSecsSinceEpoch();

        cv::Mat frame;
        if (pixelFormat == GX_PIXEL_FORMAT_MONO8) {
            const size_t bufSize = static_cast<size_t>(width * height);
            frame = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC1);
            std::memcpy(frame.data, imageData->GetBuffer(), bufSize);
        } else if (is16BitMonoPixelFormat(pixelFormat)) {
            const size_t bufSize = static_cast<size_t>(width * height * sizeof(uint16_t));
            frame = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_16UC1);
            std::memcpy(frame.data, imageData->GetBuffer(), bufSize);
        } else if (pixelFormat == GX_PIXEL_FORMAT_BAYER_RG8 ||
                   pixelFormat == GX_PIXEL_FORMAT_BAYER_GB8 ||
                   pixelFormat == GX_PIXEL_FORMAT_BAYER_GR8 ||
                   pixelFormat == GX_PIXEL_FORMAT_BAYER_BG8) {
            void* rgbBuffer = imageData->ConvertToRGB24(GX_BIT_0_7, GX_RAW2RGB_NEIGHBOUR, false);
            const size_t bufSize = static_cast<size_t>(width * height * 3);
            frame = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC3);
            std::memcpy(frame.data, rgbBuffer, bufSize);
        } else {
            void* raw8Buffer = imageData->ConvertToRaw8(GX_BIT_0_7);
            const size_t bufSize = static_cast<size_t>(width * height);
            frame = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC1);
            std::memcpy(frame.data, raw8Buffer, bufSize);
        }

        if (frame.empty()) {
            finishCallback();
            return;
        }

        bool abortCallback = false;
        {
            QMutexLocker stateLocker(&camera.stateMutex);
            if (!camera.isOpen || camera.isClosing) {
                abortCallback = true;
            }
        }
        if (abortCallback) {
            finishCallback();
            return;
        }

        quint64 frameId = 0;
        {
            QMutexLocker stateLocker(&camera.stateMutex);
            frameId = extendWrappingFrameId(camera, rawFrameId);
        }

        CameraFrame packet;
        packet.image = frame;
        packet.pixelFormat = pixelFormat;
        packet.bitDepth = cameraFrameBitDepth(pixelFormat);
        packet.maxPixelValue = cameraFrameMaxPixelValue(pixelFormat);
        packet.frameId = frameId;
        packet.cameraTimestamp = cameraTimestamp;
        packet.receivedMs = receivedMs;

        {
            QMutexLocker locker(&camera.frameMutex);
            camera.latestFrame = frame;
            camera.latestFramePacket = packet;
        }

        emit frameCaptured(cameraIndex, packet);

        bool expected = false;
        if (camera.frameNotificationPending.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            emit frameReady(cameraIndex);
        }
        finishCallback();
    } catch (CGalaxyException& e) {
        finishCallback();
        emit cameraError(cameraIndex, e.GetErrorCode(), QStringLiteral("图像回调处理异常"));
    } catch (...) {
        finishCallback();
        emit cameraError(cameraIndex, -1, QStringLiteral("图像回调未知异常"));
    }
}
