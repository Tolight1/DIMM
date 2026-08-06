cmake_minimum_required(VERSION 3.5) # CMake install : https://cmake.org/download/
project(DIMM LANGUAGES CXX)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_PREFIX_PATH "E:/Softwoare/Qtool/qt/6.11.0/msvc2022_64") # Qt Kit Dir
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 COMPONENTS Widgets Network REQUIRED) # Qt COMPONENTS
aux_source_directory(./src srcs)
list(FILTER srcs EXCLUDE REGEX "opencv_test\\.cpp$") # exclude test file from main target
list(APPEND srcs
    src/AppConfig.h
    src/AppConfigPersistence.h
    src/AppConfigPersistence.cpp
    src/ConfigApplicationController.h
    src/ConfigApplicationController.cpp
    src/ConfigTextUtils.h
    src/ConfigTextUtils.cpp
    src/DimmRuntimeHelpers.h
    src/DimmRuntimeHelpers.cpp
    src/DIMM.Config.cpp
    src/DIMM.Ui.cpp
    src/DIMM.LiveRoi.cpp
    src/DIMM.Alignment.cpp
    src/DIMM.Simulation.cpp
    src/DIMM.Results.cpp
    src/DIMM.CommCamera.cpp
    src/DIMM.AutoExposure.cpp
    src/FullFrameStarDetector.h
    src/FullFrameStarDetector.cpp
    src/ImageUtils.h
    src/ImageUtils.cpp
    src/HotPixelTemplateSettings.h
    src/HotPixelTemplateSettings.cpp
    src/SettingsDialog.h
    src/SettingsDialog.cpp
    src/EnvironmentSensorManager.h
    src/EnvironmentSensorManager.cpp
    src/EafSdkLoader.h
    src/EafSdkLoader.cpp
    src/EafFocuserManager.h
    src/EafFocuserManager.cpp
    src/FocuserControlWidget.h
    src/FocuserControlWidget.cpp
    src/AlignmentController.h
    src/AlignmentController.cpp
    src/AlignmentTaskManager.h
    src/AlignmentTaskManager.cpp
    src/AlignmentSession.h
    src/AlignmentSession.cpp
    src/AutoAcquisitionScheduler.h
    src/AutoAcquisitionScheduler.cpp
    src/AlignmentCoarseEstimator.h
    src/AlignmentCoarseEstimator.cpp
    src/AlignmentCoarseController.h
    src/AlignmentCoarseController.cpp
)
list(REMOVE_DUPLICATES srcs)

# Specify MSVC UTF-8 encoding
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# Suppress C4828 warnings from Galaxy SDK headers (invalid UTF-8 characters)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/wd4828>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/wd4828>")

# Galaxy SDK Configuration
set(GALAXY_SDK_DIR "E:/Softwoare/Galaxy/GalaxySDK/Development")
include_directories("${GALAXY_SDK_DIR}/C++ SDK/inc")
link_directories("${GALAXY_SDK_DIR}/C++ SDK/lib/x64")

# OpenCV Configuration (same style as Galaxy SDK)
set(OPENCV_DIR "E:/Softwoare/OpenCV/opencv4120")
include_directories("${OPENCV_DIR}/include")
link_directories("${OPENCV_DIR}/lib")

# ZWO EAF SDK Configuration
set(EAF_SDK_DIR
    "E:/env/EAF_SDK_V1.8.1/EAF_Windows_SDK_V1.8.1"
    CACHE PATH "ZWO EAF SDK directory")
include_directories("${EAF_SDK_DIR}/include")

add_executable(${PROJECT_NAME}
    WIN32 # If you need a terminal for debug, please comment this statement
    ${srcs}
)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Network GxIAPICPPEx.lib
    $<IF:$<CONFIG:Debug>,opencv_world4120d.lib,opencv_world4120.lib>
) # Qt6 + Galaxy SDK + OpenCV

# Auto-copy Galaxy DLLs to output directory after build
set(GALAXY_DLL_DIR "E:/Softwoare/Galaxy/GalaxySDK/APIDll/Win64")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GALAXY_DLL_DIR}/GxIAPICPPEx.dll"
        "${GALAXY_DLL_DIR}/GxIAPICPP.dll"
        "${GALAXY_DLL_DIR}/GxIAPI.dll"
        "${GALAXY_DLL_DIR}/DxImageProc.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying Galaxy SDK DLLs to output directory..."
)

# Auto-copy OpenCV DLLs to output directory after build
set(OPENCV_DLL_DIR "E:/Softwoare/OpenCV/opencv4120/bin")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENCV_DLL_DIR}/$<IF:$<CONFIG:Debug>,opencv_world4120d.dll,opencv_world4120.dll>"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying OpenCV DLLs to output directory..."
)

# Auto-copy ZWO EAF DLL to output directory when available
set(EAF_DLL "${EAF_SDK_DIR}/lib/Windows/x64/Release/EAF_focuser.dll")
if(EXISTS "${EAF_DLL}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${EAF_DLL}"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying ZWO EAF SDK DLL..."
    )
else()
    message(WARNING "EAF_focuser.dll not found; focuser support will be unavailable at runtime")
endif()
