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

    // Core function pointers (required)
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
    EAFSetMaxStepFn EAFSetMaxStep = nullptr;
    EAFGetMaxStepFn EAFGetMaxStep = nullptr;
    EAFStepRangeFn EAFStepRange = nullptr;
    EAFSetReverseFn EAFSetReverse = nullptr;
    EAFGetReverseFn EAFGetReverse = nullptr;
    EAFSetBacklashFn EAFSetBacklash = nullptr;
    EAFGetBacklashFn EAFGetBacklash = nullptr;
    EAFCloseFn EAFClose = nullptr;
    EAFGetSDKVersionFn EAFGetSDKVersion = nullptr;

    // Optional function pointers
    EAFGetFirmwareVersionFn EAFGetFirmwareVersion = nullptr;
    EAFGetSerialNumberFn EAFGetSerialNumber = nullptr;
    EAFGetTypeFn EAFGetType = nullptr;
    EAFSetBeepFn EAFSetBeep = nullptr;
    EAFGetBeepFn EAFGetBeep = nullptr;
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
