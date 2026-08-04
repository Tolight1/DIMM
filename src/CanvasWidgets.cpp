#include "CanvasWidgets.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
cv::Mat makeDisplayGray8(const cv::Mat& image)
{
    if (image.empty() || image.channels() != 1) {
        return cv::Mat();
    }
    if (image.type() == CV_8UC1) {
        return image;
    }

    cv::Mat display;
    constexpr double kMono12MaxValue = 4095.0;
    image.convertTo(display, CV_8U, 255.0 / kMono12MaxValue);
    return display;
}
}

FullFrameCanvas::FullFrameCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void FullFrameCanvas::setImage(const cv::Mat& image)
{
    m_image = image.empty() ? cv::Mat() : image;
    m_imageDirty = true;

    if (m_image.empty()) {
        m_qimage = QImage();
        m_scale = 1.0;
        m_offset = QPointF();
        m_hasViewTransform = false;
        m_followImageFit = true;
    }

    update();
}

void FullFrameCanvas::setRoiList(const QVector<RoiRect>& rois)
{
    m_rois = rois;
    update();
}

void FullFrameCanvas::setCurrentRoi(int index)
{
    m_currentRoiIndex = index;
    update();
}

void FullFrameCanvas::setAlignmentOverlay(const AlignmentOverlay& overlay)
{
    m_alignmentOverlay = overlay;
    update();
}

void FullFrameCanvas::clearAlignmentOverlay()
{
    m_alignmentOverlay = AlignmentOverlay();
    update();
}

void FullFrameCanvas::setStarCandidateOverlays(const QVector<StarCandidateOverlay>& candidates)
{
    m_starCandidateOverlays = candidates;
    update();
}

void FullFrameCanvas::clearStarCandidateOverlays()
{
    if (m_starCandidateOverlays.isEmpty()) {
        return;
    }
    m_starCandidateOverlays.clear();
    update();
}

void FullFrameCanvas::setCoarseDriftOverlay(const CoarseDriftOverlay& overlay)
{
    m_coarseDriftOverlay = overlay;
    update();
}

void FullFrameCanvas::clearCoarseDriftOverlay()
{
    m_coarseDriftOverlay = CoarseDriftOverlay();
    update();
}

void FullFrameCanvas::clear()
{
    m_image = cv::Mat();
    m_qimage = QImage();
    m_imageDirty = true;
    m_rois.clear();
    m_currentRoiIndex = -1;
    m_alignmentOverlay = AlignmentOverlay();
    m_starCandidateOverlays.clear();
    m_coarseDriftOverlay = CoarseDriftOverlay();
    m_scale = 1.0;
    m_offset = QPointF();
    m_hasViewTransform = false;
    m_followImageFit = true;
    update();
}

QPointF FullFrameCanvas::widgetToImage(QPointF widgetPos) const
{
    return (widgetPos - m_offset) / m_scale;
}

QPointF FullFrameCanvas::imageToWidget(QPointF imagePos) const
{
    return imagePos * m_scale + m_offset;
}

void FullFrameCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (m_image.empty()) {
        return;
    }

    if (m_followImageFit || !m_hasViewTransform) {
        fitImageToViewport();
    } else {
        clampOffset();
    }
}

void FullFrameCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (m_image.empty()) {
        painter.setPen(QColor(70, 70, 70));
        painter.setFont(QFont("Microsoft YaHei", 14));
        painter.drawText(rect(), Qt::AlignCenter, "No image");
        return;
    }

    drawImage(painter);
    drawAlignmentOverlay(painter);
    drawCoarseDriftOverlay(painter);
    drawRoiOverlays(painter);
    drawStarCandidateOverlays(painter);
    drawScaleBar(painter);
    drawInfo(painter);
}

void FullFrameCanvas::fitImageToViewport()
{
    if (m_qimage.isNull() || width() <= 0 || height() <= 0) {
        return;
    }

    const double scaleX = static_cast<double>(width()) / m_qimage.width();
    const double scaleY = static_cast<double>(height()) / m_qimage.height();
    m_scale = std::min(scaleX, scaleY);
    m_offset = QPointF((width() - m_qimage.width() * m_scale) / 2.0,
                       (height() - m_qimage.height() * m_scale) / 2.0);
    m_hasViewTransform = true;
}

void FullFrameCanvas::clampOffset()
{
    if (m_qimage.isNull() || width() <= 0 || height() <= 0) {
        return;
    }

    const double scaledWidth = m_qimage.width() * m_scale;
    const double scaledHeight = m_qimage.height() * m_scale;

    if (scaledWidth <= width()) {
        m_offset.setX((width() - scaledWidth) / 2.0);
    } else {
        const double minX = width() - scaledWidth;
        m_offset.setX(std::clamp(m_offset.x(), minX, 0.0));
    }

    if (scaledHeight <= height()) {
        m_offset.setY((height() - scaledHeight) / 2.0);
    } else {
        const double minY = height() - scaledHeight;
        m_offset.setY(std::clamp(m_offset.y(), minY, 0.0));
    }
}

void FullFrameCanvas::drawImage(QPainter& painter)
{
    if (m_imageDirty && !m_image.empty()) {
        if (m_image.channels() == 3) {
            cv::Mat rgb;
            cv::cvtColor(m_image, rgb, cv::COLOR_BGR2RGB);
            m_qimage = QImage(rgb.data, rgb.cols, rgb.rows,
                              static_cast<int>(rgb.step), QImage::Format_RGB888)
                           .copy();
        } else if (m_image.channels() == 1) {
            const cv::Mat display = makeDisplayGray8(m_image);
            m_qimage = QImage(display.data, display.cols, display.rows,
                              static_cast<int>(display.step), QImage::Format_Grayscale8)
                           .copy();
        } else {
            m_qimage = QImage();
        }
        m_imageDirty = false;
    }

    if (m_qimage.isNull()) {
        return;
    }

    if (!m_hasViewTransform || m_followImageFit) {
        fitImageToViewport();
    } else {
        clampOffset();
    }

    const QRectF targetRect(m_offset.x(), m_offset.y(),
                            m_qimage.width() * m_scale, m_qimage.height() * m_scale);
    painter.drawImage(targetRect, m_qimage);
}

void FullFrameCanvas::drawRoiOverlays(QPainter& painter)
{
    for (int i = 0; i < m_rois.size(); ++i) {
        const auto& roi = m_rois[i];
        const QPointF topLeft = imageToWidget(QPointF(roi.x, roi.y));
        const QPointF bottomRight = imageToWidget(QPointF(roi.x + roi.w, roi.y + roi.h));
        const QRectF roiRect(topLeft, bottomRight);
        const bool isCurrent = (i == m_currentRoiIndex);

        painter.fillRect(roiRect, isCurrent ? QColor(255, 82, 82, 56)
                                            : QColor(79, 195, 247, 32));
        painter.setPen(isCurrent ? QPen(QColor(255, 82, 82), 2)
                                 : QPen(QColor(79, 195, 247), 1));
        painter.drawRect(roiRect);

        painter.setPen(QColor(255, 255, 255));
        painter.setFont(QFont("Consolas", 8));
        painter.drawText(roiRect.topLeft() + QPointF(4, 14), QString("#%1").arg(i));
    }
}

void FullFrameCanvas::drawStarCandidateOverlays(QPainter& painter)
{
    if (m_starCandidateOverlays.isEmpty() || m_image.empty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(QFont("Consolas", 9, QFont::Bold));

    for (const StarCandidateOverlay& candidate : m_starCandidateOverlays) {
        const QPointF topLeft = imageToWidget(candidate.bbox.topLeft());
        const QPointF bottomRight = imageToWidget(candidate.bbox.bottomRight());
        const QRectF box(topLeft, bottomRight);
        const QPointF center = imageToWidget(candidate.center);

        const QColor color = candidate.selected ? QColor(255, 82, 82) : QColor(255, 210, 90);
        painter.setPen(QPen(color, candidate.selected ? 2.0 : 1.3));
        painter.drawRect(box);
        painter.drawLine(center + QPointF(-6.0, 0.0), center + QPointF(6.0, 0.0));
        painter.drawLine(center + QPointF(0.0, -6.0), center + QPointF(0.0, 6.0));

        painter.setPen(QColor(255, 255, 255));
        painter.drawText(box.topLeft() + QPointF(4.0, 14.0),
                         QStringLiteral("#%1").arg(candidate.index));
    }

    painter.restore();
}

void FullFrameCanvas::drawAlignmentOverlay(QPainter& painter)
{
    if (!m_alignmentOverlay.enabled || m_image.empty() || m_scale <= 0.0) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPointF center = imageToWidget(m_alignmentOverlay.orbitCenter);
    const double radius = std::max(0.0, m_alignmentOverlay.orbitRadiusPx * m_scale);
    const QRectF imageRect(m_offset.x(),
                           m_offset.y(),
                           m_qimage.width() * m_scale,
                           m_qimage.height() * m_scale);

    painter.setFont(QFont("Microsoft YaHei", 8));
    for (const CatalogMatchOverlay& match : m_alignmentOverlay.catalogMatches) {
        const QPointF detected = imageToWidget(match.detectedPosition);
        const QPointF predicted = imageToWidget(match.predictedPosition);
        const QColor matchColor = match.isPolaris ? QColor(255, 86, 86, 220)
                                                  : QColor(120, 235, 190, 185);

        painter.setPen(QPen(QColor(255, 255, 255, 80), 1.0, Qt::DashLine));
        painter.drawLine(detected, predicted);
        painter.setPen(QPen(matchColor, match.isPolaris ? 2.0 : 1.2));
        painter.drawEllipse(detected, match.isPolaris ? 5.0 : 3.5, match.isPolaris ? 5.0 : 3.5);
        painter.drawLine(predicted + QPointF(-4.0, 0.0), predicted + QPointF(4.0, 0.0));
        painter.drawLine(predicted + QPointF(0.0, -4.0), predicted + QPointF(0.0, 4.0));
        if (match.isPolaris || match.residualPx > 2.0) {
            const QString text = match.label.isEmpty()
                                     ? QStringLiteral("%1 px").arg(match.residualPx, 0, 'f', 1)
                                     : match.label;
            painter.drawText(detected + QPointF(6.0, -6.0), text);
        }
    }

    const QPen crossPen(QColor(34, 121, 255, 190), 1.2);
    painter.setPen(crossPen);
    painter.drawLine(QPointF(imageRect.left(), center.y()), QPointF(imageRect.right(), center.y()));
    painter.drawLine(QPointF(center.x(), imageRect.top()), QPointF(center.x(), imageRect.bottom()));

    if (radius > 1.0) {
        painter.setPen(QPen(QColor(40, 92, 255, 220), 2.0));
        painter.drawEllipse(center, radius, radius);
    }

    painter.setFont(QFont("Microsoft YaHei", 9));
    if (m_alignmentOverlay.hasPredictedPolaris || m_alignmentOverlay.hasDetectedPolaris) {
        const QPointF polarisForLine =
            imageToWidget(m_alignmentOverlay.hasDetectedPolaris
                              ? m_alignmentOverlay.detectedPolarisPosition
                              : m_alignmentOverlay.predictedPolarisPosition);
        painter.setPen(QPen(QColor(255, 235, 120, 180), 1.4, Qt::DashLine));
        painter.drawLine(center, polarisForLine);
    }

    if (m_alignmentOverlay.hasPredictedPolaris) {
        const QPointF predicted = imageToWidget(m_alignmentOverlay.predictedPolarisPosition);
        painter.setPen(QPen(QColor(255, 160, 70), 2.0, Qt::DashLine));
        painter.drawEllipse(predicted, 8.0, 8.0);
        painter.drawLine(predicted + QPointF(-11.0, 0.0), predicted + QPointF(11.0, 0.0));
        painter.drawLine(predicted + QPointF(0.0, -11.0), predicted + QPointF(0.0, 11.0));
    }

    if (m_alignmentOverlay.hasDetectedPolaris) {
        const QPointF detected = imageToWidget(m_alignmentOverlay.detectedPolarisPosition);
        painter.setPen(QPen(QColor(255, 86, 86), 2.4, Qt::SolidLine));
        painter.drawEllipse(detected, 7.0, 7.0);
        painter.drawLine(detected + QPointF(-10.0, 0.0), detected + QPointF(10.0, 0.0));
        painter.drawLine(detected + QPointF(0.0, -10.0), detected + QPointF(0.0, 10.0));
    }

    if (m_alignmentOverlay.hasPredictedPolaris && m_alignmentOverlay.hasDetectedPolaris) {
        const QPointF predicted = imageToWidget(m_alignmentOverlay.predictedPolarisPosition);
        const QPointF detected = imageToWidget(m_alignmentOverlay.detectedPolarisPosition);
        painter.setPen(QPen(QColor(255, 255, 255, 150), 1.2, Qt::DotLine));
        painter.drawLine(predicted, detected);
    }

    if (m_alignmentOverlay.hasStar) {
        const QPointF star = imageToWidget(m_alignmentOverlay.starPosition);
        if (!m_alignmentOverlay.hasPredictedPolaris && !m_alignmentOverlay.hasDetectedPolaris) {
            painter.setPen(QPen(QColor(255, 86, 86), 2.0));
            painter.drawEllipse(star, 7.0, 7.0);
            painter.drawLine(star + QPointF(-10.0, 0.0), star + QPointF(10.0, 0.0));
            painter.drawLine(star + QPointF(0.0, -10.0), star + QPointF(0.0, 10.0));
        }

        const QString label = m_alignmentOverlay.label.isEmpty()
                                  ? QStringLiteral("偏离轨道: %1 px")
                                        .arg(m_alignmentOverlay.deviationPx, 0, 'f', 1)
                                  : m_alignmentOverlay.label;
        painter.setPen(QColor(255, 210, 90));
        painter.drawText(star + QPointF(10.0, -10.0), label);
    } else {
        painter.setPen(QColor(255, 190, 70));
        painter.drawText(imageRect.adjusted(12.0, 28.0, -12.0, -12.0).topLeft(),
                         QStringLiteral("未检测到北极星"));
    }

    if (m_alignmentOverlay.matchedStarCount > 0) {
        painter.setPen(QColor(210, 235, 255));
        painter.drawText(imageRect.adjusted(12.0, 12.0, -12.0, -12.0).topLeft(),
                         QStringLiteral("匹配 %1 星 | RMS %2 px | %3\"/px")
                             .arg(m_alignmentOverlay.matchedStarCount)
                             .arg(m_alignmentOverlay.rmsPx, 0, 'f', 2)
                             .arg(m_alignmentOverlay.plateScaleArcsecPx, 0, 'f', 3));
    }

    QStringList detailLines;
    if (!m_alignmentOverlay.solveStateText.isEmpty()) {
        detailLines << m_alignmentOverlay.solveStateText;
    }
    if (m_alignmentOverlay.polarisNcpDistancePx > 0.0) {
        detailLines << QStringLiteral("NCP-Polaris %1 px / %2'")
                           .arg(m_alignmentOverlay.polarisNcpDistancePx, 0, 'f', 1)
                           .arg(m_alignmentOverlay.polarisNcpDistanceArcmin, 0, 'f', 2);
    }
    if (!m_alignmentOverlay.orbitSource.isEmpty()) {
        detailLines << QStringLiteral("轨道: %1").arg(m_alignmentOverlay.orbitSource);
    }
    if (m_alignmentOverlay.solveTotalMs > 0.0) {
        detailLines << QStringLiteral("耗时 %1 ms")
                           .arg(m_alignmentOverlay.solveTotalMs, 0, 'f', 1);
    }
    if (m_alignmentOverlay.mirroredKnown) {
        detailLines << QStringLiteral("镜像: %1")
                           .arg(m_alignmentOverlay.mirrored ? QStringLiteral("是")
                                                            : QStringLiteral("否"));
    }
    if (!m_alignmentOverlay.warningText.isEmpty()) {
        detailLines << m_alignmentOverlay.warningText;
    }
    if (!detailLines.isEmpty()) {
        painter.setFont(QFont("Microsoft YaHei", 8));
        painter.setPen(m_alignmentOverlay.warningText.isEmpty()
                           ? QColor(210, 235, 255)
                           : QColor(255, 190, 70));
        painter.drawText(imageRect.adjusted(12.0, 46.0, -12.0, -12.0).topLeft(),
                         detailLines.join(QLatin1String(" | ")));
    }

    painter.restore();
}

void FullFrameCanvas::drawCoarseDriftOverlay(QPainter& painter)
{
    if (!m_coarseDriftOverlay.enabled || m_image.empty() || m_scale <= 0.0) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF imageRect(m_offset.x(),
                           m_offset.y(),
                           m_qimage.width() * m_scale,
                           m_qimage.height() * m_scale);

    for (const CoarseDriftTrackOverlay& track : m_coarseDriftOverlay.tracks) {
        const QPointF start = imageToWidget(track.startPx);
        const QPointF end = imageToWidget(track.endPx);
        painter.setPen(track.usedForSolve
                           ? QPen(QColor(120, 235, 190, 210), 1.6)
                           : QPen(QColor(150, 150, 150, 120), 1.0));
        painter.drawLine(start, end);
        painter.drawEllipse(end, track.usedForSolve ? 3.5 : 2.0, track.usedForSolve ? 3.5 : 2.0);
    }

    if (m_coarseDriftOverlay.valid) {
        const QPointF center = imageToWidget(m_coarseDriftOverlay.northCelestialPolePx);
        const QPointF frameCenter = imageToWidget(m_coarseDriftOverlay.frameCenterPx);
        painter.setPen(QPen(QColor(120, 255, 160), 2.0));
        painter.drawEllipse(center, 8.0, 8.0);
        painter.drawLine(center + QPointF(-14.0, 0.0), center + QPointF(14.0, 0.0));
        painter.drawLine(center + QPointF(0.0, -14.0), center + QPointF(0.0, 14.0));

        painter.setPen(QPen(QColor(255, 220, 90), 2.0, Qt::DashLine));
        QPointF arrowEnd = center;
        if (!imageRect.contains(center)) {
            // adjustmentVectorPx points from the frame center to the estimated NCP
            // in image pixels; clamp the arrow endpoint to the image rect edge when
            // the estimated NCP lies offscreen.
            const double length =
                std::sqrt(m_coarseDriftOverlay.adjustmentVectorPx.x() *
                              m_coarseDriftOverlay.adjustmentVectorPx.x() +
                          m_coarseDriftOverlay.adjustmentVectorPx.y() *
                              m_coarseDriftOverlay.adjustmentVectorPx.y());
            if (length > 1e-6) {
                const QPointF unit(m_coarseDriftOverlay.adjustmentVectorPx.x() / length,
                                   m_coarseDriftOverlay.adjustmentVectorPx.y() / length);
                double t = std::numeric_limits<double>::max();
                if (std::abs(unit.x()) > 1e-9) {
                    t = std::min(t, (unit.x() > 0.0 ? imageRect.right() - frameCenter.x()
                                                    : imageRect.left() - frameCenter.x()) /
                                        unit.x());
                }
                if (std::abs(unit.y()) > 1e-9) {
                    t = std::min(t, (unit.y() > 0.0 ? imageRect.bottom() - frameCenter.y()
                                                    : imageRect.top() - frameCenter.y()) /
                                        unit.y());
                }
                arrowEnd = frameCenter + unit * std::max(0.0, t);
            }
        }
        painter.drawLine(frameCenter, arrowEnd);
    }

    QStringList lines;
    if (!m_coarseDriftOverlay.statusText.isEmpty()) {
        lines << m_coarseDriftOverlay.statusText;
    }
    lines << QStringLiteral("候选 %1 | 轨迹 %2 | 速度 %3 px/s | RMS %4 px")
                 .arg(m_coarseDriftOverlay.detectedCandidateCount)
                 .arg(m_coarseDriftOverlay.usableTrackCount)
                 .arg(m_coarseDriftOverlay.medianSpeedPxSec, 0, 'f', 3)
                 .arg(m_coarseDriftOverlay.centerResidualRmsPx, 0, 'f', 1);

    painter.setFont(QFont("Microsoft YaHei", 9));
    painter.setPen(m_coarseDriftOverlay.valid ? QColor(170, 255, 190) : QColor(255, 210, 90));
    painter.drawText(imageRect.adjusted(12.0, 70.0, -12.0, -12.0).topLeft(),
                     lines.join(QLatin1String(" | ")));

    painter.restore();
}

void FullFrameCanvas::drawScaleBar(QPainter& painter)
{
    if (m_image.empty()) {
        return;
    }

    static constexpr int kScaleBarPixels = 100;
    const int x = width() - 120;
    const int y = height() - 30;

    painter.setPen(QPen(QColor(200, 200, 200), 2));
    painter.drawLine(x, y, x + kScaleBarPixels, y);
    painter.drawLine(x, y - 5, x, y + 5);
    painter.drawLine(x + kScaleBarPixels, y - 5, x + kScaleBarPixels, y + 5);

    painter.setPen(QColor(200, 200, 200));
    painter.setFont(QFont("Consolas", 8));
    painter.drawText(x + 20, y - 8, "100 px");
}

void FullFrameCanvas::drawInfo(QPainter& painter)
{
    if (m_image.empty()) {
        return;
    }

    painter.setPen(QColor(150, 150, 150));
    painter.setFont(QFont("Consolas", 9));
    const QString info = QString("%1x%2  zoom %3%")
                             .arg(m_image.cols)
                             .arg(m_image.rows)
                             .arg(static_cast<int>(m_scale * 100.0));
    painter.drawText(10, 20, info);
}

void FullFrameCanvas::wheelEvent(QWheelEvent* event)
{
    if (m_image.empty() || m_qimage.isNull()) {
        return;
    }

    const double factor = (event->angleDelta().y() > 0) ? 1.2 : (1.0 / 1.2);
    const double newScale = m_scale * factor;
    if (newScale < 0.05 || newScale > 20.0) {
        return;
    }

    const QPointF mousePos = event->position();
    const QPointF imagePos = widgetToImage(mousePos);

    m_scale = newScale;
    m_offset = mousePos - imagePos * m_scale;
    m_followImageFit = false;
    m_hasViewTransform = true;
    clampOffset();
    update();
}

void FullFrameCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        m_isDragging = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void FullFrameCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging) {
        const QPointF delta = event->pos() - m_lastMousePos;
        m_offset += delta;
        m_lastMousePos = event->pos();
        m_followImageFit = false;
        m_hasViewTransform = true;
        clampOffset();
        update();
    }

    if (!m_image.empty()) {
        const QPointF imgPos = widgetToImage(event->position());
        const int ix = static_cast<int>(imgPos.x());
        const int iy = static_cast<int>(imgPos.y());
        if (ix >= 0 && ix < m_image.cols && iy >= 0 && iy < m_image.rows) {
            emit mousePositionChanged(ix, iy);
        }
    }
}

void FullFrameCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    m_isDragging = false;
    setCursor(Qt::ArrowCursor);
}

RoiStarCanvas::RoiStarCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void RoiStarCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

void RoiStarCanvas::setRoiImage(const cv::Mat& roiImage)
{
    m_roiImage = roiImage.empty() ? cv::Mat() : roiImage.clone();
    m_imageDirty = true;
    update();
}

void RoiStarCanvas::setCentroid(double x, double y)
{
    m_centroidX = x;
    m_centroidY = y;
    m_hasCentroid = true;
    update();
}

void RoiStarCanvas::clear()
{
    m_roiImage = cv::Mat();
    m_qimage = QImage();
    m_imageDirty = true;
    m_hasCentroid = false;
    update();
}

void RoiStarCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (m_roiImage.empty()) {
        painter.setPen(QColor(70, 70, 70));
        painter.setFont(QFont("Microsoft YaHei", 10));
        painter.drawText(rect(), Qt::AlignCenter, "No ROI data");
        return;
    }

    drawImage(painter);
    drawGrid(painter);
    if (m_hasCentroid) {
        drawCentroid(painter);
    }
}

void RoiStarCanvas::drawImage(QPainter& painter)
{
    if (m_imageDirty && !m_roiImage.empty()) {
        if (m_roiImage.channels() == 3) {
            cv::Mat rgb;
            cv::cvtColor(m_roiImage, rgb, cv::COLOR_BGR2RGB);
            m_qimage = QImage(rgb.data, rgb.cols, rgb.rows,
                              static_cast<int>(rgb.step), QImage::Format_RGB888)
                           .copy();
        } else if (m_roiImage.channels() == 1) {
            const cv::Mat display = makeDisplayGray8(m_roiImage);
            m_qimage = QImage(display.data, display.cols, display.rows,
                              static_cast<int>(display.step), QImage::Format_Grayscale8)
                           .copy();
        } else {
            m_qimage = QImage();
        }
        m_imageDirty = false;
    }

    if (m_qimage.isNull()) {
        return;
    }

    const double scaleX = static_cast<double>(width()) / m_qimage.width();
    const double scaleY = static_cast<double>(height()) / m_qimage.height();
    const double scale = std::min(scaleX, scaleY);
    const double offsetX = (width() - m_qimage.width() * scale) / 2.0;
    const double offsetY = (height() - m_qimage.height() * scale) / 2.0;

    painter.drawImage(QRectF(offsetX, offsetY, m_qimage.width() * scale, m_qimage.height() * scale),
                      m_qimage);
}

void RoiStarCanvas::drawGrid(QPainter& painter)
{
    if (m_roiImage.empty()) {
        return;
    }

    const double scaleX = static_cast<double>(width()) / m_roiImage.cols;
    const double scaleY = static_cast<double>(height()) / m_roiImage.rows;
    const double scale = std::min(scaleX, scaleY);
    const double offsetX = (width() - m_roiImage.cols * scale) / 2.0;
    const double offsetY = (height() - m_roiImage.rows * scale) / 2.0;

    painter.setPen(QPen(QColor(50, 50, 50), 0.5));

    for (int x = 0; x <= m_roiImage.cols; ++x) {
        const double px = offsetX + x * scale;
        painter.drawLine(QPointF(px, offsetY), QPointF(px, offsetY + m_roiImage.rows * scale));
    }

    for (int y = 0; y <= m_roiImage.rows; ++y) {
        const double py = offsetY + y * scale;
        painter.drawLine(QPointF(offsetX, py), QPointF(offsetX + m_roiImage.cols * scale, py));
    }
}

void RoiStarCanvas::drawCentroid(QPainter& painter)
{
    if (m_roiImage.empty()) {
        return;
    }

    const double scaleX = static_cast<double>(width()) / m_roiImage.cols;
    const double scaleY = static_cast<double>(height()) / m_roiImage.rows;
    const double scale = std::min(scaleX, scaleY);
    const double offsetX = (width() - m_roiImage.cols * scale) / 2.0;
    const double offsetY = (height() - m_roiImage.rows * scale) / 2.0;
    const double cx = offsetX + m_centroidX * scale;
    const double cy = offsetY + m_centroidY * scale;

    painter.setPen(QPen(QColor(255, 0, 0), 1));
    const double crossSize = 10.0;
    painter.drawLine(QPointF(cx - crossSize, cy), QPointF(cx + crossSize, cy));
    painter.drawLine(QPointF(cx, cy - crossSize), QPointF(cx, cy + crossSize));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, cy), 5, 5);

    painter.setPen(QColor(0, 200, 255));
    painter.setFont(QFont("Consolas", 9));
    painter.drawText(QPointF(cx + 12, cy - 5),
                     QString("(%1, %2)").arg(m_centroidX, 0, 'f', 1).arg(m_centroidY, 0, 'f', 1));
}

void RoiStarCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_roiImage.empty()) {
        return;
    }

    const double scaleX = static_cast<double>(width()) / m_roiImage.cols;
    const double scaleY = static_cast<double>(height()) / m_roiImage.rows;
    const double scale = std::min(scaleX, scaleY);
    const double offsetX = (width() - m_roiImage.cols * scale) / 2.0;
    const double offsetY = (height() - m_roiImage.rows * scale) / 2.0;

    const int px = static_cast<int>((event->pos().x() - offsetX) / scale);
    const int py = static_cast<int>((event->pos().y() - offsetY) / scale);

    if (px >= 0 && px < m_roiImage.cols && py >= 0 && py < m_roiImage.rows) {
        double value = 0.0;
        if (m_roiImage.type() == CV_64F) {
            value = m_roiImage.at<double>(py, px);
        } else if (m_roiImage.type() == CV_8U) {
            value = m_roiImage.at<uchar>(py, px);
        } else if (m_roiImage.type() == CV_16U) {
            value = m_roiImage.at<quint16>(py, px);
        }
        setToolTip(QString("(%1, %2) = %3").arg(px).arg(py).arg(value, 0, 'f', 1));
    }
}

ChartWidget::ChartWidget(QWidget* parent)
    : ChartWidget(SeriesKind::R0, parent)
{
}

ChartWidget::ChartWidget(SeriesKind kind, QWidget* parent)
    : QWidget(parent)
    , m_kind(kind)
    , m_data(WINDOW_SECONDS, std::numeric_limits<double>::quiet_NaN())
{
    setMinimumSize(260, 220);
}

void ChartWidget::setSecondValue(int second, double value)
{
    if (second < 0 || second >= WINDOW_SECONDS) {
        return;
    }
    m_data[second] = value;
    update();
}

void ChartWidget::clear()
{
    std::fill(m_data.begin(), m_data.end(), std::numeric_limits<double>::quiet_NaN());
    m_displayMinY = 0.0;
    m_displayMaxY = std::numeric_limits<double>::quiet_NaN();
    update();
}

void ChartWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(17, 17, 17));

    const bool hasAnyValue =
        std::any_of(m_data.cbegin(), m_data.cend(), [](double value) { return !std::isnan(value); });

    if (!hasAnyValue) {
        painter.setPen(QColor(70, 70, 70));
        painter.setFont(QFont("Microsoft YaHei", 10));
        painter.drawText(rect(), Qt::AlignCenter, "No data");
        return;
    }

    drawSeriesChart(painter, rect().adjusted(0, 0, -1, -1));
}

void ChartWidget::drawSeriesChart(QPainter& painter, QRect rect)
{
    const bool isR0 = m_kind == SeriesKind::R0;
    const QColor lineColor = isR0 ? QColor(79, 195, 247) : QColor(139, 195, 74);
    const QString title = isR0 ? QStringLiteral("r0") : QStringLiteral("Seeing");
    const QString unit = isR0 ? QStringLiteral("cm") : QStringLiteral("arcsec");
    const double minY = m_displayMinY;
    const double maxY = smoothDisplayMaxY(computeTargetMaxY());

    painter.setPen(QPen(QColor(52, 52, 82), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect.adjusted(1, 1, -2, -2), 8, 8);

    painter.setPen(lineColor);
    painter.setFont(QFont("Microsoft YaHei", 11, QFont::Bold));
    painter.drawText(rect.x() + 16, rect.y() + 26, title);

    const QRect chartRect(rect.x() + 46, rect.y() + 42, rect.width() - 64, rect.height() - 70);
    painter.fillRect(chartRect, QColor(30, 30, 48));
    drawAxes(painter, chartRect, m_data, minY, maxY, unit);

    painter.setPen(QPen(lineColor, 2.0));
    const double xStep = static_cast<double>(chartRect.width()) / (WINDOW_SECONDS - 1);

    QPainterPath path;
    bool hasStarted = false;
    for (int i = 0; i < m_data.size(); ++i) {
        if (std::isnan(m_data[i])) {
            continue;
        }
        const double x = chartRect.x() + i * xStep;
        const double normalized = std::clamp((m_data[i] - minY) / (maxY - minY), 0.0, 1.0);
        const double y = chartRect.y() + chartRect.height() - normalized * chartRect.height();
        if (!hasStarted) {
            path.moveTo(x, y);
            hasStarted = true;
        } else {
            path.lineTo(x, y);
        }
    }
    if (hasStarted) {
        painter.drawPath(path);
    }
}

void ChartWidget::drawAxes(QPainter& painter, QRect rect, const QVector<double>&,
                           double minY, double maxY, const QString& unit)
{
    painter.setPen(QPen(QColor(56, 56, 78), 1));
    for (int i = 1; i <= 3; ++i) {
        const int y = rect.top() + (rect.height() * i) / 4;
        painter.drawLine(rect.left(), y, rect.right(), y);
    }
    for (int i = 1; i <= 4; ++i) {
        const int x = rect.left() + (rect.width() * i) / 4;
        painter.drawLine(x, rect.top(), x, rect.bottom());
    }

    painter.setPen(QPen(QColor(112, 112, 140), 1.2));
    painter.drawLine(rect.bottomLeft(), rect.bottomRight());
    painter.drawLine(rect.bottomLeft(), rect.topLeft());

    painter.setPen(QColor(170, 170, 190));
    painter.setFont(QFont("Consolas", 8));
    painter.drawText(QRect(rect.left() - 42, rect.top() - 6, 40, 16),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(maxY, 'f', 1));
    painter.drawText(QRect(rect.left() - 42, rect.center().y() - 8, 40, 16),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number((minY + maxY) / 2.0, 'f', 1));
    painter.drawText(QRect(rect.left() - 42, rect.bottom() - 10, 40, 16),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(minY, 'f', 1));

    painter.setPen(QColor(130, 130, 156));
    painter.drawText(QRect(rect.left() - 4, rect.top() - 18, 64, 14),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     unit);

    painter.setPen(QColor(140, 140, 166));
    painter.drawText(QRect(rect.left() - 10, rect.bottom() + 8, 24, 14),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("0s"));
    painter.drawText(QRect(rect.left() + rect.width() / 4 - 18, rect.bottom() + 8, 36, 14),
                     Qt::AlignCenter,
                     QStringLiteral("15s"));
    painter.drawText(QRect(rect.left() + rect.width() / 2 - 18, rect.bottom() + 8, 36, 14),
                     Qt::AlignCenter,
                     QStringLiteral("30s"));
    painter.drawText(QRect(rect.left() + (rect.width() * 3) / 4 - 18, rect.bottom() + 8, 36, 14),
                     Qt::AlignCenter,
                     QStringLiteral("45s"));
    painter.drawText(QRect(rect.right() - 28, rect.bottom() + 8, 36, 14),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("60s"));
}

double ChartWidget::baselineMaxY() const
{
    return m_kind == SeriesKind::R0 ? 20.0 : 1.0;
}

double ChartWidget::computeTargetMaxY() const
{
    double maxValue = 0.0;
    bool hasValue = false;
    for (double value : m_data) {
        if (std::isnan(value) || !std::isfinite(value)) {
            continue;
        }
        maxValue = std::max(maxValue, value);
        hasValue = true;
    }

    if (!hasValue) {
        return baselineMaxY();
    }

    const double paddedMax = std::max(maxValue * 1.15, baselineMaxY());
    return niceCeil(paddedMax);
}

double ChartWidget::smoothDisplayMaxY(double targetMaxY)
{
    const double safeTarget = std::max(targetMaxY, baselineMaxY());
    if (!std::isfinite(m_displayMaxY)) {
        m_displayMaxY = safeTarget;
        return m_displayMaxY;
    }

    const double delta = safeTarget - m_displayMaxY;
    if (std::abs(delta) < 1e-6) {
        return m_displayMaxY;
    }

    const double factor = delta > 0.0 ? 0.35 : 0.12;
    m_displayMaxY += delta * factor;

    if (std::abs(safeTarget - m_displayMaxY) < safeTarget * 0.02) {
        m_displayMaxY = safeTarget;
    }

    m_displayMaxY = std::max(m_displayMaxY, baselineMaxY());
    return m_displayMaxY;
}

double ChartWidget::niceCeil(double value)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return 1.0;
    }

    const double exponent = std::floor(std::log10(value));
    const double scale = std::pow(10.0, exponent);
    const double normalized = value / scale;

    double niceNormalized = 1.0;
    if (normalized <= 1.0) {
        niceNormalized = 1.0;
    } else if (normalized <= 2.0) {
        niceNormalized = 2.0;
    } else if (normalized <= 5.0) {
        niceNormalized = 5.0;
    } else {
        niceNormalized = 10.0;
    }

    return niceNormalized * scale;
}
