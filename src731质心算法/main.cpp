#include "DIMM.h"

#include <QApplication>
#include <QMetaType>

#include <opencv2/opencv.hpp>

#pragma comment(lib, "user32.lib")

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<CameraFrame>("CameraFrame");
    qRegisterMetaType<RoiRect>("RoiRect");
    qRegisterMetaType<CentroidResult>("CentroidResult");

    QString globalStyle = R"(
        * {
            font-family: "Microsoft YaHei", "Segoe UI", Arial;
            color: #e6eef8;
            outline: none;
        }

        QWidget {
            background-color: #08111f;
            color: #e6eef8;
            selection-background-color: rgba(86, 212, 255, 0.24);
            selection-color: #f7fbff;
        }

        QMainWindow, QDialog {
            background-color: #08111f;
        }

        QLabel {
            background: transparent;
            color: #d8e4f2;
        }

        QFrame {
            color: #d8e4f2;
        }

        QToolTip {
            background-color: #122239;
            color: #eef7ff;
            border: 1px solid #2f5674;
            border-radius: 6px;
            padding: 6px 8px;
        }

        QMenuBar {
            background-color: #101c30;
            border-bottom: 1px solid #20344d;
            color: #d5e3f3;
            padding: 2px 6px;
        }

        QMenuBar::item {
            padding: 6px 12px;
            border-radius: 6px;
            background: transparent;
        }

        QMenuBar::item:selected {
            background-color: rgba(86, 212, 255, 0.14);
            color: #ffffff;
        }

        QMenuBar::item:pressed {
            background-color: rgba(86, 212, 255, 0.22);
        }

        QMenu {
            background-color: #101d31;
            border: 1px solid #223954;
            border-radius: 10px;
            padding: 8px;
        }

        QMenu::item {
            padding: 7px 16px;
            border-radius: 6px;
            color: #d6e5f4;
        }

        QMenu::item:selected {
            background-color: rgba(86, 212, 255, 0.16);
            color: #ffffff;
        }

        QMenu::separator {
            height: 1px;
            margin: 6px 8px;
            background: #22354c;
        }

        QToolBar {
            background-color: #0d1829;
            border-bottom: 1px solid #1f324a;
            spacing: 8px;
            padding: 6px 8px;
        }

        QPushButton,
        QToolButton,
        QComboBox,
        QLineEdit,
        QSpinBox,
        QDoubleSpinBox {
            min-height: 18px;
            border-radius: 9px;
            padding: 7px 12px;
            border: 1px solid #29425f;
        }

        QPushButton,
        QToolButton {
            background-color: #16253b;
            color: #dfeaf7;
        }

        QPushButton:hover,
        QToolButton:hover {
            background-color: #1b2f49;
            border-color: #3d6788;
            color: #ffffff;
        }

        QPushButton:pressed,
        QToolButton:pressed,
        QPushButton:checked,
        QToolButton:checked {
            background-color: #20496b;
            border-color: #58d3ff;
            color: #f9fdff;
        }

        QPushButton:disabled,
        QToolButton:disabled,
        QLineEdit:disabled,
        QSpinBox:disabled,
        QDoubleSpinBox:disabled,
        QComboBox:disabled,
        QRadioButton:disabled,
        QCheckBox:disabled {
            background-color: #0d1727;
            color: #66788d;
            border-color: #1b2a3d;
        }

        QDialogButtonBox QPushButton {
            min-width: 92px;
            padding: 8px 16px;
        }

        QPushButton[role="primary"] {
            background-color: #1e4c72;
            border: 1px solid #56d4ff;
            color: #f5fbff;
        }

        QPushButton[role="primary"]:hover {
            background-color: #255a86;
            border-color: #7de1ff;
        }

        QPushButton[role="primary"]:pressed {
            background-color: #173d5b;
            border-color: #4fcfff;
        }

        QPushButton[role="secondary"] {
            background-color: #111d2f;
            border: 1px solid #2a415c;
            color: #b8c8d8;
        }

        QPushButton[role="secondary"]:hover {
            background-color: #16253b;
            border-color: #3b5e80;
            color: #e9f2fb;
        }

        QLineEdit,
        QSpinBox,
        QDoubleSpinBox,
        QComboBox {
            background-color: #09111d;
            color: #f3f8ff;
            selection-background-color: rgba(86, 212, 255, 0.28);
        }

        QLineEdit:hover,
        QSpinBox:hover,
        QDoubleSpinBox:hover,
        QComboBox:hover {
            border-color: #3a607e;
            background-color: #0c1625;
        }

        QLineEdit:focus,
        QSpinBox:focus,
        QDoubleSpinBox:focus,
        QComboBox:focus {
            border: 1px solid #56d4ff;
            background-color: #0d1b2d;
            color: #ffffff;
        }

        QComboBox {
            padding-right: 34px;
        }

        QComboBox::drop-down {
            width: 28px;
            border: none;
            background: transparent;
        }

        QComboBox::down-arrow {
            width: 0px;
            height: 0px;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 7px solid #9bc7dd;
            margin-right: 8px;
        }

        QComboBox QAbstractItemView {
            background-color: #0f1d31;
            color: #e7f1fb;
            border: 1px solid #28435f;
            border-radius: 8px;
            padding: 4px;
            selection-background-color: rgba(86, 212, 255, 0.22);
            selection-color: #ffffff;
            outline: none;
        }

        QAbstractItemView::item {
            min-height: 24px;
            padding: 4px 10px;
            border-radius: 6px;
        }

        QAbstractItemView::item:hover {
            background-color: rgba(86, 212, 255, 0.10);
        }

        QRadioButton,
        QCheckBox {
            spacing: 8px;
            color: #d3e0ef;
            background: transparent;
        }

        QRadioButton::indicator,
        QCheckBox::indicator {
            background-color: #0b1423;
            border: 2px solid #466179;
        }

        QRadioButton::indicator {
            width: 18px;
            height: 18px;
            border-radius: 9px;
        }

        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 5px;
        }

        QRadioButton::indicator:hover,
        QCheckBox::indicator:hover {
            border-color: #62d6ff;
        }

        QRadioButton::indicator:checked,
        QCheckBox::indicator:checked {
            background-color: #56d4ff;
            border-color: #56d4ff;
        }

        QRadioButton::indicator:checked {
            border: 5px solid #56d4ff;
            background-color: #08111f;
        }

        QCheckBox::indicator:checked {
            image: none;
        }

        QGroupBox {
            margin-top: 18px;
            padding: 18px 16px 14px 16px;
            border: 1px solid #22384f;
            border-radius: 14px;
            background-color: #101b2d;
            color: #56d4ff;
            font-weight: 600;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            left: 14px;
            padding: 0 8px;
            background-color: #101b2d;
        }

        QTabWidget::pane {
            border: none;
            background: #09111f;
        }

        QTabBar::tab {
            background-color: #101c2f;
            color: #8ea5bb;
            padding: 11px 20px;
            margin-right: 6px;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
            border: 1px solid transparent;
            border-bottom: 2px solid transparent;
        }

        QTabBar::tab:hover {
            color: #dff3ff;
            background-color: #132339;
        }

        QTabBar::tab:selected {
            color: #59d5ff;
            background-color: #152740;
            border-color: #21374d;
            border-bottom: 2px solid #56d4ff;
            font-weight: 700;
        }

        QStatusBar {
            background-color: #0e1a2b;
            border-top: 1px solid #1f3249;
        }

        QStatusBar QLabel {
            color: #88a0b6;
            padding: 0 6px;
            background: transparent;
        }

        QSplitter::handle {
            background-color: #16253a;
        }

        QSplitter::handle:hover {
            background-color: #244462;
        }

        QTableWidget {
            background-color: #0b1422;
            alternate-background-color: #0e1727;
            border: 1px solid #23384f;
            border-radius: 10px;
            color: #dce7f3;
            gridline-color: #1d3045;
            outline: none;
        }

        QTableWidget::item {
            padding: 6px 8px;
            border: none;
        }

        QTableWidget::item:selected {
            background-color: rgba(86, 212, 255, 0.20);
            color: #ffffff;
        }

        QHeaderView::section {
            background-color: #132238;
            color: #62d9ff;
            padding: 8px 10px;
            border: none;
            border-right: 1px solid #1f344c;
            border-bottom: 1px solid #1f344c;
            font-weight: 600;
        }

        QScrollArea {
            border: none;
            background: transparent;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 12px;
            margin: 6px 2px 6px 2px;
        }

        QScrollBar:horizontal {
            background: transparent;
            height: 12px;
            margin: 2px 6px 2px 6px;
        }

        QScrollBar::handle:vertical,
        QScrollBar::handle:horizontal {
            background: #22384e;
            border-radius: 5px;
            min-height: 28px;
            min-width: 28px;
        }

        QScrollBar::handle:vertical:hover,
        QScrollBar::handle:horizontal:hover {
            background: #376b90;
        }

        QScrollBar::handle:vertical:pressed,
        QScrollBar::handle:horizontal:pressed {
            background: #56d4ff;
        }

        QScrollBar::add-line,
        QScrollBar::sub-line,
        QScrollBar::add-page,
        QScrollBar::sub-page {
            background: transparent;
            border: none;
            width: 0px;
            height: 0px;
        }

        #leftPanel {
            background-color: #0d182a;
            border-right: 1px solid #1e3349;
        }

        #cameraSection,
        #roiSection,
        #paramsSection,
        #statsSection {
            background-color: #0d182a;
            border-bottom: 1px solid #1a2b3f;
            padding: 8px 6px;
        }

        #cam1Card,
        #cam2Card,
        #environmentStrip,
        #roiInfoCard,
        #statsCard,
        #r0Card,
        #seeingCard,
        #thetaCard,
        #tauCard {
            background-color: #152338;
            border-radius: 12px;
            border: 1px solid #243b55;
        }

        #lblCameraTitle,
        #lblROITitle,
        #lblParamsTitle,
        #lblStatsTitle,
        #lblFullframeTitle,
        #lblCam1ROITitle,
        #lblCam2ROITitle {
            color: #5cd7ff;
            font-weight: 700;
        }

        #lblCam1Name,
        #lblCam2Name,
        #lblEnvironmentName {
            color: #f4f8fd;
            font-size: 12px;
            font-weight: 700;
        }

        #lblCam1Info,
        #lblCam2Info,
        #lblEnvironmentInfo,
        #lblStatFrames,
        #lblStatValid,
        #lblStatLatency,
        #lblStatWindow,
        #lblROIX,
        #lblROIY,
        #lblROIW,
        #lblROIH,
        #lblROITimeLabel,
        #lblROITimeNext,
        #lblR0Label,
        #lblSeeingLabel,
        #lblThetaLabel,
        #lblTauLabel,
        #lblR0Unit,
        #lblSeeingUnit,
        #lblThetaUnit,
        #lblTauUnit {
            color: #8ea5bb;
        }

        #lblROIXValue,
        #lblROIYValue,
        #lblROIWValue,
        #lblROIHValue,
        #lblCam1ROICoord,
        #lblCam2ROICoord {
            color: #eff7ff;
            font-weight: 600;
        }

        #lblROITimeCurrent {
            color: #ffbe55;
            font-size: 13px;
            font-weight: 700;
        }

        #lblR0Value {
            color: #56d4ff;
            font-size: 22px;
            font-weight: 700;
        }

        #lblSeeingValue {
            color: #95dd6b;
            font-size: 22px;
            font-weight: 700;
        }

        #lblThetaValue {
            color: #ffb347;
            font-size: 22px;
            font-weight: 700;
        }

        #lblTauValue {
            color: #ff6aa7;
            font-size: 22px;
            font-weight: 700;
        }

        #previewCanvas,
        #r0ChartCanvas,
        #seeingChartCanvas,
        #cam1ROICanvas,
        #cam2ROICanvas,
        #roiMapCanvas {
            background-color: #050b14;
            border: 1px solid #1c2f44;
            border-radius: 12px;
        }

        #r0ChartPanel,
        #seeingChartPanel {
            background-color: #111e31;
            border: 1px solid #22384f;
            border-radius: 16px;
        }

        #fullframeArea,
        #roiImagesArea,
        #chartsArea,
        #bottomToolbar,
        #cam1ROIPanel,
        #cam2ROIPanel {
            background-color: #09111f;
        }

        #chartsArea,
        #roiImagesArea,
        #bottomToolbar {
            border-top: 1px solid #1c2f45;
        }

        #lblFullframeLabel {
            color: #475b6f;
            font-size: 18px;
        }
    )";
    app.setStyleSheet(globalStyle);

    DIMM window;
    window.show();

    return app.exec();
}
