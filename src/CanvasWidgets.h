#pragma once

#include <QWidget>
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <opencv2/opencv.hpp>
#include <QVector>
#include "ImageProcessor.h" // for RoiRect, CentroidResult

// ============================================================
// 全画幅预览Canvas
// ============================================================

class FullFrameCanvas : public QWidget {
    Q_OBJECT

public:
    explicit FullFrameCanvas(QWidget* parent = nullptr);

    void setImage(const cv::Mat& image);
    void setRoiList(const QVector<RoiRect>& rois);
    void setCurrentRoi(int index);
    void clear();

signals:
    void mousePositionChanged(int x, int y); // 鼠标在图像上的像素坐标

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    cv::Mat m_image;
    QImage m_qimage; // 缓存转换后的QImage，避免每帧重复cvtColor
    bool m_imageDirty = true; // 标记图像是否需要重新转换
    QVector<RoiRect> m_rois;
    int m_currentRoiIndex = -1;

    double m_scale = 1.0;
    QPointF m_offset;
    QPoint m_lastMousePos;
    bool m_isDragging = false;

    QPointF widgetToImage(QPointF widgetPos) const;
    QPointF imageToWidget(QPointF imagePos) const;
    void drawImage(QPainter& painter);
    void drawRoiOverlays(QPainter& painter);
    void drawScaleBar(QPainter& painter);
    void drawInfo(QPainter& painter);
};

// ============================================================
// ROI星图Canvas
// ============================================================

class RoiStarCanvas : public QWidget {
    Q_OBJECT

public:
    explicit RoiStarCanvas(QWidget* parent = nullptr);

    void setRoiImage(const cv::Mat& roiImage);
    void setCentroid(double x, double y);
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

private:
    cv::Mat m_roiImage;
    QImage m_qimage; // 缓存转换后的QImage
    bool m_imageDirty = true;
    double m_centroidX = 0.0;
    double m_centroidY = 0.0;
    bool m_hasCentroid = false;

    void drawImage(QPainter& painter);
    void drawCentroid(QPainter& painter);
    void drawGrid(QPainter& painter);
    void drawPixelInfo(QPainter& painter, QPoint pos);
};

// ============================================================
// 参数曲线Widget（使用QtCharts）
// ============================================================

// 注意：需要在CMakeLists.txt中添加 Qt6::Charts
// 如果没有QtCharts，可以用简单的QPainter绘制替代

class ChartWidget : public QWidget {
    Q_OBJECT

public:
    explicit ChartWidget(QWidget* parent = nullptr);

    void addDataPoint(double r0, double seeing, double centroidX, double centroidY);
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    // 数据存储
    QVector<double> m_r0Data;
    QVector<double> m_seeingData;
    QVector<QPointF> m_trajectoryData;

    static constexpr int MAX_POINTS = 300;
    int m_timeCounter = 0;

    void drawR0Chart(QPainter& painter, QRect rect);
    void drawSeeingChart(QPainter& painter, QRect rect);
    void drawTrajectory(QPainter& painter, QRect rect);

    void drawAxes(QPainter& painter, QRect rect, const QVector<double>& data,
                  double minY, double maxY, const QString& unit);
};
