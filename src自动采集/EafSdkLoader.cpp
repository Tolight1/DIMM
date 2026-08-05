#include "EafSdkLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

bool EafSdkLoader::load()
{
    if (m_library.isLoaded()) {
        return true;
    }

    m_attemptedPaths.clear();
    m_loadError.clear();

    const QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/EAF_focuser.dll"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/eaf/EAF_focuser.dll"),
        QStringLiteral("E:/env/EAF_SDK_V1.8.1/EAF_Windows_SDK_V1.8.1/lib/Windows/x64/Release/EAF_focuser.dll"),
        QStringLiteral("EAF_focuser.dll"),
    };

    for (const QString& path : searchPaths) {
        m_attemptedPaths.append(path);
        if (!QFileInfo::exists(path)) {
            continue;
        }
        m_library.setFileName(path);
        if (m_library.load()) {
            break;
        }
    }

    if (!m_library.isLoaded()) {
        m_loadError = QStringLiteral("Failed to load EAF_focuser.dll. Attempted paths:\n");
        for (const QString& p : m_attemptedPaths) {
            m_loadError += QStringLiteral("  - ") + p + QStringLiteral("\n");
        }
        m_loadError += m_library.errorString();
        return false;
    }

    bool allRequired = true;
    allRequired &= resolveRequired("EAFGetNum", &EAFGetNum);
    allRequired &= resolveRequired("EAFGetID", &EAFGetID);
    allRequired &= resolveRequired("EAFOpen", &EAFOpen);
    allRequired &= resolveRequired("EAFGetProperty", &EAFGetProperty);
    allRequired &= resolveRequired("EAFMove", &EAFMove);
    allRequired &= resolveRequired("EAFStop", &EAFStop);
    allRequired &= resolveRequired("EAFIsMoving", &EAFIsMoving);
    allRequired &= resolveRequired("EAFGetPosition", &EAFGetPosition);
    allRequired &= resolveRequired("EAFResetPostion", &EAFResetPostion);
    allRequired &= resolveRequired("EAFGetTemp", &EAFGetTemp);
    allRequired &= resolveRequired("EAFSetMaxStep", &EAFSetMaxStep);
    allRequired &= resolveRequired("EAFGetMaxStep", &EAFGetMaxStep);
    allRequired &= resolveRequired("EAFStepRange", &EAFStepRange);
    allRequired &= resolveRequired("EAFSetReverse", &EAFSetReverse);
    allRequired &= resolveRequired("EAFGetReverse", &EAFGetReverse);
    allRequired &= resolveRequired("EAFSetBacklash", &EAFSetBacklash);
    allRequired &= resolveRequired("EAFGetBacklash", &EAFGetBacklash);
    allRequired &= resolveRequired("EAFClose", &EAFClose);
    allRequired &= resolveRequired("EAFGetSDKVersion", &EAFGetSDKVersion);

    if (!allRequired) {
        m_loadError = QStringLiteral("Missing required EAF SDK function pointers.");
        m_library.unload();
        return false;
    }

    resolveOptional("EAFStopAndWait", &EAFStopAndWait);
    resolveOptional("EAFGetFirmwareVersion", &EAFGetFirmwareVersion);
    resolveOptional("EAFGetSerialNumber", &EAFGetSerialNumber);
    resolveOptional("EAFGetType", &EAFGetType);
    resolveOptional("EAFSetBeep", &EAFSetBeep);
    resolveOptional("EAFGetBeep", &EAFGetBeep);
    resolveOptional("EAFGetLedState", &EAFGetLedState);
    resolveOptional("EAFSetLedState", &EAFSetLedState);
    resolveOptional("EAFGetErrorCode", &EAFGetErrorCode);
    resolveOptional("EAFGetNumOfControls", &EAFGetNumOfControls);
    resolveOptional("EAFGetControlCaps", &EAFGetControlCaps);

    return true;
}

bool EafSdkLoader::isLoaded() const
{
    return m_library.isLoaded();
}

QString EafSdkLoader::loadError() const
{
    return m_loadError;
}

QString EafSdkLoader::sdkVersion() const
{
    if (!EAFGetSDKVersion) {
        return QStringLiteral("unknown");
    }
    return QString::fromUtf8(EAFGetSDKVersion());
}

QStringList EafSdkLoader::attemptedPaths() const
{
    return m_attemptedPaths;
}

template <typename Fn>
bool EafSdkLoader::resolveRequired(const char* name, Fn* out)
{
    *out = reinterpret_cast<Fn>(m_library.resolve(name));
    return *out != nullptr;
}

template <typename Fn>
void EafSdkLoader::resolveOptional(const char* name, Fn* out)
{
    *out = reinterpret_cast<Fn>(m_library.resolve(name));
}

QString EafSdkLoader::errorToString(EAF_ERROR_CODE code)
{
    switch (code) {
    case EAF_SUCCESS:             return QStringLiteral("Success");
    case EAF_ERROR_INVALID_INDEX:  return QStringLiteral("Invalid device index");
    case EAF_ERROR_INVALID_ID:     return QStringLiteral("Invalid device ID");
    case EAF_ERROR_INVALID_VALUE:  return QStringLiteral("Invalid value");
    case EAF_ERROR_REMOVED:        return QStringLiteral("Device removed");
    case EAF_ERROR_MOVING:         return QStringLiteral("Device is moving");
    case EAF_ERROR_ERROR_STATE:    return QStringLiteral("Device is in error state");
    case EAF_ERROR_GENERAL_ERROR:  return QStringLiteral("General error");
    case EAF_ERROR_NOT_SUPPORTED:  return QStringLiteral("Not supported");
    case EAF_ERROR_CLOSED:         return QStringLiteral("Device is closed");
    case EAF_ERROR_BATTER_INFO:    return QStringLiteral("Battery information error");
    case EAF_ERROR_INVALID_LENGTH: return QStringLiteral("Invalid length");
    default:                       return QStringLiteral("Unknown error code ") + QString::number(static_cast<int>(code));
    }
}

QString EafSdkLoader::serialToHex(const EAF_SN& sn)
{
    const auto* data = reinterpret_cast<const unsigned char*>(&sn);
    QString hex;
    for (int i = 0; i < static_cast<int>(sizeof(EAF_SN)); ++i) {
        hex += QString::asprintf("%02X", data[i]);
    }
    return hex;
}
