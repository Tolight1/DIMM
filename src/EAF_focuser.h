#pragma once

// Minimal ZWO EAF SDK declarations used by this project.
// The runtime still loads EAF_focuser.dll dynamically; this header only lets
// builds succeed on machines without the SDK include directory installed.

#define EAF_ID_MAX 128

typedef struct _EAF_INFO {
    int ID;
    char Name[64];
    int MaxStep;
} EAF_INFO;

typedef enum _EAF_ERROR_CODE {
    EAF_SUCCESS = 0,
    EAF_ERROR_INVALID_INDEX,
    EAF_ERROR_INVALID_ID,
    EAF_ERROR_INVALID_VALUE,
    EAF_ERROR_REMOVED,
    EAF_ERROR_MOVING,
    EAF_ERROR_ERROR_STATE,
    EAF_ERROR_GENERAL_ERROR,
    EAF_ERROR_NOT_SUPPORTED,
    EAF_ERROR_CLOSED,
    EAF_ERROR_BATTER_INFO,
    EAF_ERROR_INVALID_LENGTH,
    EAF_ERROR_END = -1
} EAF_ERROR_CODE;

typedef struct _EAF_ID {
    unsigned char id[8];
} EAF_ID;

typedef EAF_ID EAF_SN;

typedef struct _EAF_TYPE {
    char type[16];
} EAF_TYPE;

typedef struct _EAF_ERROR_MSG {
    char motor_error_code[3];
    char battery_error_code[3];
} EAF_ERROR_MSG;

typedef struct _EAF_CONTROL_CAPS {
    int controlId;
    char name[64];
    int minValue;
    int maxValue;
    int defaultValue;
    bool isWritable;
} EAF_CONTROL_CAPS;
