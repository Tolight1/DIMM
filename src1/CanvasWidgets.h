#pragma once

#include <QMouseEvent>
#include <QPainter>
#include <QString>
#include <QVector>
#include <QWheelEvent>
#include <QWidget>
#include <limits>
#include <opencv2/opencv.hpp>

#include "ImageProcessor.h"

class FullFrameCanvas : public QWidget {
    Q_OBJECT

public:
    struct CatalogMatchOverlay {
        QPointF detectedPosition;
        QPointF predictedPosition;
        QString label;
        double residualPx = 0.0;
        bool isPolaris = false;
    };

    struct AlignmentOverlay {
        bool enabled = false;
        QPointF orbitCenter;
        double orbitRadiusPx = 0.0;
        bool hasStar = false;
        QPointF starPosition;
        bool hasPredictedPolaris = false;
        QPointF predictedPolarisPosition;
        bool hasDetectedPolaris = false;
        QPointF detectedPolarisPosition;
        double deviationPx = 0.0;
        double polarisNcpDistancePx = 0.0;
        double polarisNcpDistanceArcmin = 0.0;
        int matchedStarCount = 0;
        double rmsPx = 0.0;
        double plateScaleArcsecPx = 0.0;
        double solveTotalMs = 0.0;
        QString orbitSource;
        QString solveStateText;
        QString warningText;
        bool mirroredKnown = false;
        bool mirrored = false;
        QString label;
        QVector<CatalogMatchOverlay> catalogMatches;
    };

    struct StarCandidateOverlay {
        int index = 0;
        QRectF bbox;
        QPointF center;
        bool selected = false;
    };

    struct CoarseDriftTrackOverlay {
        int pointCount = 0;
        QPointF startPx;
        QPointF endPx;
        QPointF velocityPxSec;
        double speedPxSec = 0.0;
        double durationSec = 0.0;
        double displacementPx = 0.0;
        double fitRmsPx = 0.0;
        bool velocityFitValid = false;
        bool usedForSolve = false;
        QString rejectionReason;
    };

    struct CoarseDriftOverlay {
        bool enabled = false;
        bool valid = false;
        QPointF northCelestialPolePx;
        QPointF frameCenterPx;
        QPointF adjustmentVectorPx;
        double offsetPx = 0.0;
        double offsetDeg = 0.0;
        double medianSpeedPxSec = 0.0;
        double medianFittedSpeedPxSec = 0.0;
        double centerResidualRmsPx = 0.0;
        int detectedCandidateCount = 0;
        int activeTrackCount = 0;
        int fittedTrackCount = 0;
        int usableTrackCount = 0;
        int requiredTrackCount = 0;
        QString statusText;
        QString diagnosticText;
        QVector<CoarseDriftTrackOverlay> tracks;
    };

    explicit FullFrameCanvas(QWidget* parent = nullptr);

    void setImage(const cv::Mat& image);
    void setRoiList(const QVector<RoiRect>& rois);
    void setCurrentRoi(int index);
    void setAlignmentOverlay(const AlignmentOverlay& overlay);
    void clearAlignmentOverlay();
    void setStarCandidateOverlays(const QVector<StarCandidateOverlay>& candidates);
    void clearStarCandidateOverlays();
    void setCoarseDriftOverlay(const CoarseDriftOverlay& overlay);
    void clearCoarseDriftOverlay();
    void clear();

signals:
    void mousePositionChanged(int x, int y);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    cv::Mat m_image;
    QImage m_qimage;
    bool m_imageDirty = true;
    QVector<RoiRect> m_rois;
    int m_currentRoiIndex = -1;
    AlignmentOverlay m_alignmentOverlay;
    QVector<StarCandidateOverlay> m_starCandidateOverlays;
    CoarseDriftOverlay m_coarseDriftOverlay;

    double m_scale = 1.0;
    QPointF m_offset;
    QPoint m_lastMousePos;
    bool m_isDragging = false;
    bool m_hasViewTransform = false;
    bool m_followImageFit = true;

    QPointF widgetToImage(QPointF widgetPos) const;
    QPointF imageToWidget(QPointF imagePos) const;
    void fitImageToViewport();
    void clampOffset();
    void drawImage(QPainter& painter);
    void drawRoiOverlays(QPainter& painter);
    void drawStarCandidateOverlays(QPainter& painter);
    void drawAlignmentOverlay(QPainter& painter);
    void drawCoarseDriftOverlay(QPainter& painter);
    void drawScaleBar(QPainter& painter);
    void drawInfo(QPainter& painter);
};

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
    QImage m_qimage;
    bool m_imageDirty = true;
    double m_centroidX = 0.0;
    double m_centroidY = 0.0;
    bool m_hasCentroid = false;

    void drawImage(QPainter& painter);
    void drawCentroid(QPainter& painter);
    void drawGrid(QPainter& painter);
    void drawPixelInfo(QPainter& painter, QPoint pos);
};

class ChartWidget : public QWidget {
    Q_OBJECT

public:
    enum class SeriesKind {
        R0,
        Seeing
    };

    explicit ChartWidget(QWidget* parent = nullptr);
    explicit ChartWidget(SeriesKind kind, QWidget* parent = nullptr);

    void setSecondValue(int second, double value);
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    SeriesKind m_kind = SeriesKind::R0;
    QVector<double> m_data;
    double m_displayMinY = 0.0;
    double m_displayMaxY = std::numeric_limits<double>::quiet_NaN();

    static constexpr int WINDOW_SECONDS = 60;

    void drawSeriesChart(QPainter& painter, QRect rect);
    void drawAxes(QPainter& painter, QRect rect, const QVector<double>& data,
                  double minY, double maxY, const QString& unit);
    double baselineMaxY() const;
    double computeTargetMaxY() const;
    double smoothDisplayMaxY(double targetMaxY);
    static double niceCeil(double value);
};
