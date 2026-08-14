# DIMM 构建速查

新 Codex 会话先读本文件。修改代码后，默认按本文构建，不要删除现有 `build`。

## 固定环境

项目目录：

```text
E:\Softwoare\visual studio\project\UI\UI_2
```

构建目录、生成器和平台：

```text
build
Visual Studio 18 2026
x64
```

固定工具：

```text
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

## 标准构建

在项目根目录执行唯一首选命令：

```powershell
cmd.exe /d /c call scripts\build_release_vs18.cmd
```

不要优先使用 VS Code/Codex 直接执行的 `ALL_BUILD -j 16`。本机 Codex 环境可能同时存在大小写不同的 `PATH`/`Path`，并且并行 MSVC 编译可能卡在 PDB 或文件跟踪阶段。`scripts\build_release_vs18.cmd` 已处理这些问题：

- 子进程先执行 `set "PATH="`；
- 加载 VS18 x64 编译环境；
- 使用固定的 CMake 4.x；
- 先执行 Release MOC/UIC；
- 串行构建 `DIMM`，跳过重复 `PreBuildEvent`；
- 关闭文件访问跟踪和增量链接；
- 自动部署 Qt、MSVC、Galaxy、OpenCV、EAF 运行库。

脚本位置：[scripts/build_release_vs18.cmd](scripts/build_release_vs18.cmd)

## 什么时候重新配置

只修改 `src`、`tests` 或文档：直接运行标准构建。

修改 `CMakeLists.txt`、新增源文件、修改生成器/平台，或 `build` 缓存损坏：先重新配置：

```powershell
$vsdev = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$line = 'set "PATH=" && call "' + $vsdev + '" -arch=x64 -host_arch=x64 && "' + $cmake + '" -S . -B build -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_VS_GLOBALS=TrackFileAccess=false -DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS'
cmd.exe /d /c $line
```

重新配置后再次运行标准构建。`CMAKE_VS_GLOBALS=TrackFileAccess=false` 用于避免 VS18 的文件跟踪卡住 CMake 的 `try_compile`；`CMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS` 确保该设置传递到 CMake 的探测子工程。不要在同一个 `build` 中混用 VS、MinGW 或其他生成器，也不要因为普通构建失败就删除 `build`。

## 构建成功检查

命令返回 `0` 后必须确认：

```powershell
Get-Item build\Release\DIMM.exe | Select-Object FullName,Length,LastWriteTime
Test-Path build\Release\Qt6Core.dll
Test-Path build\Release\GxIAPICPPEx.dll
Test-Path build\Release\opencv_world4120.dll
Test-Path build\Release\EAF_focuser.dll
```

`DIMM.exe` 的时间戳必须是本次构建后的时间。最终发布目录是：

```text
E:\Softwoare\visual studio\project\UI\UI_2\build\Release
```

复制整个 `Release` 目录，不要只复制 exe。

## 构建失败时

1. 报 `same key "PATH"`：不要改注册表或 Windows 环境变量，重新运行标准构建脚本。
2. 卡在 `Automatic MOC and UIC`：先结束本次 `cmake`/`MSBuild`/`cl` 进程，再重新运行脚本。
3. `MSBuild` 或 `cl` 长时间低 CPU：先确认没有正在运行的 `DIMM.exe`，结束残留构建进程，再重新运行脚本。
4. 命令返回 `0` 但 exe 时间未更新：不算成功，重新运行完整的标准构建，不要只执行 `/t:Link`。
5. 出现明确的 C++ 编译或链接错误：记录完整错误信息，再根据错误修改源码。

不要使用 `git reset --hard` 或 `git checkout --` 清理工作区，工作区可能包含用户或同事的历史改动。
