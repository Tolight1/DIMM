#pragma once

#include "ui_DIMM.h"
#include "ImageProcessor.h"  // for CentroidResult, RoiRect
#include <QMainWindow>
#include <QTimer>
#include <QDialog>
#include <QFile>
#include <QTextStream>
#include <functional>
#include <opencv2/opencv.hpp>

// 前向声明 - 项目组件
class CameraManager;
class FullFrameCanvas;
class RoiStarCanvas;
class ChartWidget;
class CommManager;

// 前向声明
class QSplitter;
class QTabWidget;
class QLineEdit;
class QRadioButton;
class QCheckBox;

// 设置对话框
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // 设置应用回调
    std::function<void(double exposure, double gain)> onApplyCamera;
    std::function<void(int kernelSize, double sigma, int method)> onApplyProcessing;
    std::function<void(double D, double f, double lambda, double pixelSize)> onApplyOptics;
    std::function<void(QString path, bool saveImages, int interval)> onApplyStorage;
    std::function<void(int mode)> onApplyTriggerMode;
    std::function<void(QString ip, quint16 port)> onApplyNetwork;

    // 读取当前值
    QLineEdit* exposureEdit = nullptr;
    QLineEdit* gainEdit = nullptr;
    QLineEdit* storagePathEdit = nullptr;
    QCheckBox* saveImagesCheck = nullptr;
    QLineEdit* saveIntervalEdit = nullptr;
    QRadioButton* triggerContinuous = nullptr;
    QRadioButton* triggerHardware = nullptr;
    // 图像处理参数
    QRadioButton* procGravity = nullptr;
    QRadioButton* procGaussian = nullptr;
    QLineEdit* procKernelSize = nullptr;
    QLineEdit* procSigma = nullptr;
    // 光学参数
    QLineEdit* opticsD = nullptr;
    QLineEdit* opticsF = nullptr;
    QLineEdit* detectorPixelSize = nullptr;
    QLineEdit* detectorWavelength = nullptr;
    // 网络参数
    QLineEdit* netIpEdit = nullptr;
    QLineEdit* netPortEdit = nullptr;
    QPushButton* netConnectBtn = nullptr;
    QLabel* netStatusLabel = nullptr;

private:
    void applySettings();
};

// 主窗口
class DIMM : public QMainWindow {
    Q_OBJECT

public:
    explicit DIMM(QWidget* parent = nullptr);
    ~DIMM();

private slots:
    // 采集控制
    void onStartCapture();
    void onStopCapture();

    // 页面切换
    void onShowMainPage();
    void onShowRoiPage();
    void onShowSettings();

    // 显示控制
    void onToggleRoiImages();
    void onToggleCharts();

    // 相机切换
    void onSwitchCamera1();
    void onSwitchCamera2();

    // 菜单操作
    void onSaveConfig();
    void onLoadConfig();
    void onExportData();
    void onExportReport();
    void onConnectAll();
    void onDisconnectAll();
    void onAbout();

    // 数据更新
    void onUpdateSimulation();

    // 相机帧处理
    void onFrameReady(int cameraIndex, cv::Mat frame);
    void onCameraConnected(int index, QString serial, QString model);
    void onCameraDisconnected(int index);
    void onCameraError(int index, int errorCode, QString message);

private:
    void setupConnections();
    void updateParams();

    Ui_DIMM* ui;
    QTimer* m_simulationTimer;   // 模拟数据定时器
    int m_frameCount;            // 帧计数
    bool m_isCapturing;          // 是否采集中
    int m_currentCamera;         // 当前相机(1/2)

    SettingsDialog* m_settingsDialog;  // 设置对话框

    // 状态栏标签（代码创建，不在.ui中）
    QLabel* m_lblStatusState;
    QLabel* m_lblStatusROI;
    QLabel* m_lblStatusFrames;

    // 项目组件
    CameraManager* m_cameraManager;
    ImageProcessor* m_imageProcessor;
    FullFrameCanvas* m_fullFrameCanvas;
    RoiStarCanvas* m_cam1RoiCanvas;
    RoiStarCanvas* m_cam2RoiCanvas;
    ChartWidget* m_r0Chart = nullptr;
    ChartWidget* m_seeingChart = nullptr;

    // 数据存储（异步写入）
    QString m_dataPath = "D:/C-DIMM/data";
    bool m_saveImages = false;
    int m_saveInterval = 1;
    QFile* m_resultFile = nullptr;
    QTextStream* m_resultStream = nullptr;
    QStringList m_pendingWrites; // 缓存待写入行
    QTimer* m_fileFlushTimer = nullptr; // 定时刷盘
    void initResultFile();
    void saveResultRow(int frame, int camIdx, double cx, double cy, double r0, double seeing);
    void flushPendingWrites();
    void updateCurrentRoi();

    // 质心数据缓存
    double m_centroidX[2] = {0.0, 0.0};
    double m_centroidY[2] = {0.0, 0.0};

    // 合并的1Hz定时器（相机信息 + ROI时段匹配）
    QTimer* m_1hzTimer = nullptr;
    void on1hzTick();
    void updateCameraInfo();
    void matchRoiTimeSlot();

    // 通信管理
    CommManager* m_commManager = nullptr;
    QTimer* m_reportTimer = nullptr;
    bool m_reporting = false;
    uint32_t m_startTimeMs = 0;
    void onCommCommand(uint8_t cmd);
    void reportMeasurement();
    void reportDeviceStatus();
};
