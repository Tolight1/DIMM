@echo off
setlocal

rem Codex may expose both PATH and Path. Build from a clean cmd environment.
set "PATH="
set "PROJECT_ROOT=%~dp0.."
set "VSDEV_CMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
set "CMAKE_EXE=E:\Softwoare\cmake18\bin\cmake.exe"

if not exist "%VSDEV_CMD%" (
    echo Visual Studio 18 developer environment not found:
    echo %VSDEV_CMD%
    exit /b 1
)
if not exist "%CMAKE_EXE%" (
    echo CMake 4.x not found:
    echo %CMAKE_EXE%
    exit /b 1
)

call "%VSDEV_CMD%" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

where cl.exe
"%CMAKE_EXE%" -E cmake_autogen "%PROJECT_ROOT%/build/CMakeFiles/DIMM_autogen.dir/AutogenInfo.json" Release
if errorlevel 1 exit /b %errorlevel%

rem CMake's generated VS18 pre-build event can hang in the inherited Codex environment.
rem MOC/UIC has already completed above, so skip the duplicate MSBuild event.
rem Use a serial MSBuild queue and disable file tracking/incremental linking to avoid
rem VS18 waiting on the shared compiler database during this full rebuild.
"%CMAKE_EXE%" --build "%PROJECT_ROOT%\build" --config Release --target DIMM --parallel 1 -- -p:PreBuildEventUseInBuild=false -p:TrackFileAccess=false -p:LinkIncremental=false
exit /b %errorlevel%
