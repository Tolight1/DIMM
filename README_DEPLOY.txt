╔══════════════════════════════════════════════════════╗
║   C-DIMM 大气相干长度测量系统 v1.0  部署说明         ║
╚══════════════════════════════════════════════════════╝

一、打包（在开发电脑上）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  双击运行 deploy.bat，自动完成：
    1. CMake Release 构建
    2. windeployqt6 收集 Qt DLL
    3. 复制 OpenCV DLL
    4. 复制 Galaxy SDK DLL
    5. 打包到 DIMM_Release\ 文件夹

  输出文件夹 DIMM_Release\ 即为完整软件包，可直接拷贝。


二、目标电脑要求
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  必须已安装：
    ✓ 大恒 Galaxy SDK（含相机驱动）
    ✓ Windows 10/11 64位

  首次部署需要：
    ✓ 运行 VC_redist.x64.exe 安装 MSVC 运行时


三、在目标电脑上运行
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  1. 将整个 DIMM_Release 文件夹拷贝到目标电脑
  2. 双击运行 VC_redist.x64.exe（只需一次）
  3. 双击 DIMM.exe 启动程序

  注意：
  - 不需要安装 Qt 或 OpenCV
  - Galaxy SDK DLL 已包含在文件夹中，优先使用本地版本
  - 如果相机无法连接，检查 Galaxy SDK 驱动是否正确安装


四、文件说明
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  DIMM.exe            主程序
  Qt6Core.dll         Qt 核心库
  Qt6Gui.dll          Qt GUI 库
  Qt6Widgets.dll      Qt 控件库
  platforms/          Qt 平台插件（qwindows.dll）
  imageformats/       Qt 图片格式插件
  opencv_world4120.dll OpenCV 图像处理库
  GxIAPICPPEx.dll     大恒 Galaxy SDK（扩展）
  GxIAPICPP.dll       大恒 Galaxy SDK（C++接口）
  GxIAPI.dll          大恒 Galaxy SDK（核心API）
  DxImageProc.dll     大恒图像处理库
  VC_redist.x64.exe   MSVC 运行时安装包


五、故障排除
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  问题：启动报错"找不到 Qt6Core.dll"
  解决：确认 platforms\ 文件夹与 DIMM.exe 在同一目录

  问题：启动报错"找不到 GxIAPI.dll"
  解决：确认 Galaxy SDK 已正确安装，或 DLL 在同目录

  问题：相机列表为空
  解决：检查相机 USB 连接，确认 Galaxy SDK 驱动已加载

  问题：报错 "0xc000007b"
  解决：运行 VC_redist.x64.exe 安装 MSVC 运行时
