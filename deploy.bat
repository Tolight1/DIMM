@echo off
setlocal enabledelayedexpansion

echo ============================================
echo   DIMM v1.0 - Build and Package Script
echo ============================================
echo.

REM === Configuration ===
set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"
set "DEPLOY_DIR=%PROJECT_DIR%DIMM_Release"
set "QT_DIR=E:\Softwoare\Qtool\qt\6.11.0\msvc2022_64"
set "OPENCV_DIR=E:\Softwoare\OpenCV\opencv4120"
set "GALAXY_DLL_DIR=E:\Softwoare\Galaxy\GalaxySDK\APIDll\Win64"
set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

REM === Step 1: CMake Configure (Release) ===
echo [1/5] Configuring CMake (Release)...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%PROJECT_DIR%"
"%CMAKE_EXE%" -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo ERROR: CMake configure failed!
    pause
    exit /b 1
)
echo      OK
echo.

REM === Step 2: Build Release ===
echo [2/5] Building Release...
"%CMAKE_EXE%" --build build --config Release --parallel
if errorlevel 1 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)
echo      OK
echo.

REM === Step 3: Prepare deploy folder ===
echo [3/5] Preparing deploy folder...
if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%"

copy "%BUILD_DIR%\Release\DIMM.exe" "%DEPLOY_DIR%\" >nul
if errorlevel 1 (
    echo ERROR: DIMM.exe not found!
    pause
    exit /b 1
)
echo      EXE copied
echo.

REM === Step 4: Deploy Qt DLLs (windeployqt6) ===
echo [4/5] Running windeployqt6...
"%QT_DIR%\bin\windeployqt6.exe" --no-translations --no-opengl-sw --no-system-d3d-compiler "%DEPLOY_DIR%\DIMM.exe"
if errorlevel 1 (
    echo WARNING: windeployqt6 had issues, Qt DLLs may be incomplete.
)
echo      Qt DLLs deployed
echo.

REM === Step 5: Copy third-party DLLs ===
echo [5/5] Copying third-party DLLs...

copy "%OPENCV_DIR%\bin\opencv_world4120.dll" "%DEPLOY_DIR%\" >nul
echo      OpenCV DLL copied

for %%d in (GxIAPICPPEx.dll GxIAPICPP.dll GxIAPI.dll DxImageProc.dll) do (
    copy "%GALAXY_DLL_DIR%\%%d" "%DEPLOY_DIR%\" >nul
    echo      Galaxy: %%d
)

copy "%PROJECT_DIR%VC_redist.x64.exe" "%DEPLOY_DIR%\" >nul
echo      VC_redist copied
echo.

REM === Done ===
echo ============================================
echo   Package complete!
echo   Output: %DEPLOY_DIR%
echo ============================================
echo.
echo On target PC:
echo   1. Install VC_redist.x64.exe if not already installed
echo   2. Galaxy SDK should already be installed (drivers)
echo   3. Run DIMM.exe
echo.
pause
