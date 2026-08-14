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
