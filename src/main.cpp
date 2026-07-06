#include "DIMM.h"

#include <QApplication>
#include <QMetaType>
#include <opencv2/opencv.hpp>
#pragma comment(lib, "user32.lib")

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 注册元类型，使跨线程信号传递正常工作
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<RoiRect>("RoiRect");
    qRegisterMetaType<CentroidResult>("CentroidResult");

    // 全局暗色主题样式表
    QString globalStyle = R"(
        * { font-family: "Microsoft YaHei", "Segoe UI", Arial; }
        QWidget { background-color: #1a1a2e; color: #e0e0e0; }
        QMainWindow { background-color: #1a1a2e; }
        QLabel { color: #e0e0e0; }
        QFrame { color: #e0e0e0; }

        /* 菜单栏 */
        QMenuBar { background-color: #2d2d44; border-bottom: 1px solid #3a3a5a; color: #ccc; }
        QMenuBar::item { padding: 4px 12px; color: #ccc; }
        QMenuBar::item:selected { background-color: #3a3a5a; color: #fff; }
        QMenu { background-color: #2d2d44; border: 1px solid #3a3a5a; }
        QMenu::item { padding: 6px 16px; color: #ccc; }
        QMenu::item:selected { background-color: #4a4a6a; color: #fff; }
        QMenu::separator { height: 1px; background: #3a3a5a; }

        /* 工具栏 */
        QToolBar { background-color: #252540; border-bottom: 1px solid #3a3a5a; }
        QToolButton { padding: 5px 14px; color: #ccc; background: #3a3a5a; border: 1px solid #4a4a6a; border-radius: 4px; }
        QToolButton:hover { background: #4a4a6a; color: #fff; }

        /* 按钮 */
        QPushButton { padding: 5px 14px; color: #ccc; background: #3a3a5a; border: 1px solid #4a4a6a; border-radius: 4px; }
        QPushButton:hover { background: #4a4a6a; color: #fff; }

        /* 状态栏 */
        QStatusBar { background-color: #252540; border-top: 1px solid #3a3a5a; color: #888; }
        QStatusBar QLabel { color: #888; }

        /* 分割器 */
        QSplitter::handle { background-color: #3a3a5a; height: 3px; }
        QSplitter::handle:hover { background-color: #4fc3f7; }

        /* 表格 */
        QTableWidget { background-color: #111; border: 1px solid #3a3a5a; color: #ccc; gridline-color: #2a2a4a; selection-background-color: rgba(79, 195, 247, 0.3); }
        QTableWidget::item { color: #ccc; padding: 4px 8px; }
        QTableWidget::item:selected { background-color: rgba(79, 195, 247, 0.3); }
        QHeaderView::section { background-color: #2a2a4a; color: #4fc3f7; border: 1px solid #3a3a5a; padding: 6px; }

        /* 滚动条 */
        QScrollBar:vertical { background: #1a1a2e; width: 10px; }
        QScrollBar::handle:vertical { background: #3a3a5a; border-radius: 5px; min-height: 30px; }
        QScrollBar::handle:vertical:hover { background: #4fc3f7; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
        QScrollBar:horizontal { background: #1a1a2e; height: 10px; }
        QScrollBar::handle:horizontal { background: #3a3a5a; border-radius: 5px; min-width: 30px; }
        QScrollBar::handle:horizontal:hover { background: #4fc3f7; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }

        /* 对话框 */
        QDialog { background-color: #1a1a2e; color: #e0e0e0; }
        QGroupBox { border: 1px solid #3a3a5a; border-radius: 6px; margin-top: 10px; padding-top: 15px; color: #4fc3f7; font-weight: bold; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; }
        QLineEdit { background: #111; border: 1px solid #3a3a5a; padding: 6px; color: #fff; border-radius: 4px; }
        QDoubleSpinBox { background: #111; border: 1px solid #3a3a5a; padding: 6px; color: #fff; border-radius: 4px; }
        QSpinBox { background: #111; border: 1px solid #3a3a5a; padding: 6px; color: #fff; border-radius: 4px; }

        /* 单选/复选 */
        QRadioButton { color: #ccc; spacing: 6px; }
        QRadioButton::indicator { width: 16px; height: 16px; border-radius: 8px; border: 2px solid #555; }
        QRadioButton::indicator:unchecked { background: transparent; }
        QRadioButton::indicator:checked { border-color: #4fc3f7; background: #4fc3f7; }
        QCheckBox { color: #ccc; spacing: 6px; }
        QCheckBox::indicator { width: 16px; height: 16px; border-radius: 3px; border: 2px solid #555; }
        QCheckBox::indicator:unchecked { background: transparent; }
        QCheckBox::indicator:checked { border-color: #4fc3f7; background: #4fc3f7; }

        /* Tab */
        QTabBar::tab { padding: 10px 20px; color: #888; background: #1e1e30; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { color: #4fc3f7; border-bottom-color: #4fc3f7; background: #252540; font-weight: bold; }
        QTabWidget::pane { border: none; background: #1a1a2e; }

        /* 左侧面板 */
        #leftPanel { background-color: #1e1e30; border-right: 1px solid #3a3a5a; }
        #cameraSection, #roiSection, #paramsSection, #statsSection {
            background-color: #1a1a2e; border-bottom: 1px solid #2a2a4a; padding: 6px;
        }
        #cam1Card, #cam2Card {
            background-color: #252540; border-radius: 6px; padding: 8px;
            border: 1px solid #3a3a5a;
        }
        #roiInfoCard, #statsCard {
            background-color: #252540; border-radius: 6px; padding: 8px;
            border: 1px solid #3a3a5a;
        }
        #r0Card, #seeingCard, #thetaCard, #tauCard {
            background-color: #252540; border-radius: 6px; padding: 4px;
            border: 1px solid #3a3a5a;
        }

        /* 左侧面板标题 */
        #lblCameraTitle, #lblROITitle, #lblParamsTitle, #lblStatsTitle {
            color: #4fc3f7; font-size: 11px; font-weight: bold;
        }

        /* 相机卡片 */
        #lblCam1Name, #lblCam2Name { color: #fff; font-size: 12px; font-weight: bold; }
        #lblCam1Status, #lblCam2Status { color: #8bc34a; font-size: 10px; }
        #lblCam1Info, #lblCam2Info { color: #aaa; font-size: 11px; }

        /* ROI信息 */
        #lblROIX, #lblROIY, #lblROIW, #lblROIH { color: #888; }
        #lblROIXValue, #lblROIYValue, #lblROIWValue, #lblROIHValue { color: #fff; }
        #lblROITimeLabel { color: #888; }
        #lblROITimeCurrent { color: #ff9800; font-size: 13px; font-weight: bold; }
        #lblROITimeNext { color: #888; font-size: 10px; }

        /* 大气参数卡片 */
        #lblR0Label, #lblSeeingLabel, #lblThetaLabel, #lblTauLabel { color: #888; font-size: 10px; }
        #lblR0Unit, #lblSeeingUnit, #lblThetaUnit, #lblTauUnit { color: #888; font-size: 10px; }
        #lblR0Value { color: #4fc3f7; font-size: 22px; font-weight: bold; }
        #lblSeeingValue { color: #8bc34a; font-size: 22px; font-weight: bold; }
        #lblThetaValue { color: #ff9800; font-size: 22px; font-weight: bold; }
        #lblTauValue { color: #e91e63; font-size: 22px; font-weight: bold; }

        /* 统计信息 */
        #lblStatFrames, #lblStatValid, #lblStatLatency, #lblStatWindow { color: #aaa; font-size: 11px; }

        /* Canvas区域 */
        #previewCanvas, #r0ChartCanvas, #seeingChartCanvas,
        #cam1ROICanvas, #cam2ROICanvas, #roiMapCanvas { background-color: #111; border-radius: 4px; }

        /* 底部工具栏 */
        #bottomToolbar { background-color: #1e1e30; border-top: 1px solid #2a2a4a; }

        /* 标题 */
        #lblFullframeTitle { color: #4fc3f7; font-size: 12px; font-weight: bold; }
        #lblR0ChartTitle { color: #4fc3f7; }
        #lblSeeingChartTitle { color: #8bc34a; }
        #lblCam1ROITitle { color: #4fc3f7; font-weight: bold; }
        #lblCam2ROITitle { color: #8bc34a; font-weight: bold; }

        /* 全画幅标签 */
        #lblFullframeLabel { color: #333; font-size: 18px; }
        #lblCam1ROICoord { color: #4fc3f7; }
        #lblCam2ROICoord { color: #8bc34a; }

        /* 图表区域 */
        #r0ChartCanvas, #seeingChartCanvas {
            background-color: #111; border-radius: 4px; min-height: 80px;
        }
        #r0ChartPanel, #seeingChartPanel {
            background-color: #1a1a2e;
        }
        #chartsArea { background-color: #1a1a2e; border-top: 1px solid #3a3a5a; }

        /* 全画幅区域 */
        #fullframeArea { background-color: #1a1a2e; border: none; margin: 0; padding: 0; }
        #previewCanvas { background-color: #111; border-radius: 4px; }

        /* ROI星图区域 */
        #roiImagesArea { background-color: #1a1a2e; border-top: 1px solid #3a3a5a; }
        #cam1ROIPanel, #cam2ROIPanel { background-color: #1a1a2e; }
        #cam1ROICanvas, #cam2ROICanvas {
            background-color: #111; border-radius: 4px; min-width: 120px; min-height: 120px;
        }
    )";
    app.setStyleSheet(globalStyle);

    DIMM window;
    window.show();

    return app.exec();
}
