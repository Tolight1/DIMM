#include "CanvasWidgets.h"
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <algorithm>

// ============================================================
// FullFrameCanvas
// ============================================================

FullFrameCanvas::FullFrameCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void FullFrameCanvas::setImage(const cv::Mat& image)
{
    m_image = image;
    m_imageDirty = true; // 标记需要重新转换QImage
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

void FullFrameCanvas::clear()
{
    m_image = cv::Mat();
    m_rois.clear();
    m_currentRoiIndex = -1;
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

void FullFrameCanvas::resizeEvent(QResizeEvent*)
{
    if (parentWidget()) {
        resize(parentWidget()->size());
    }
}

void FullFrameCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // 背景
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (m_image.empty()) {
        painter.setPen(QColor(51, 51, 51));
        painter.setFont(QFont("Microsoft YaHei", 14));
        painter.drawText(rect(), Qt::AlignCenter, "无图像数据");
        return;
    }

    drawImage(painter);
    drawRoiOverlays(painter);
    drawScaleBar(painter);
    drawInfo(painter);
}

void FullFrameCanvas::drawImage(QPainter& painter)
{
    // 仅在图像变化时重新转换为QImage（缓存优化）
    if (m_imageDirty && !m_image.empty()) {
        if (m_image.channels() == 3) {
            cv::Mat rgb;
            cv::cvtColor(m_image, rgb, cv::COLOR_BGR2RGB);
            m_qimage = QImage(rgb.data, rgb.cols, rgb.rows,
                static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
        } else if (m_image.channels() == 1) {
            m_qimage = QImage(m_image.data, m_image.cols, m_image.rows,
                static_cast<int>(m_image.step), QImage::Format_Grayscale8).copy();
        }
        m_imageDirty = false;
    }

    if (m_qimage.isNull()) return;

    // 计算缩放和偏移（居中显示）
    double scaleX = static_cast<double>(width()) / m_qimage.width();
    double scaleY = static_cast<double>(height()) / m_qimage.height();
    m_scale = std::min(scaleX, scaleY);

    double offsetX = (width() - m_qimage.width() * m_scale) / 2.0;
    double offsetY = (height() - m_qimage.height() * m_scale) / 2.0;
    m_offset = QPointF(offsetX, offsetY);

    // 绘制图像
    QRectF targetRect(m_offset.x(), m_offset.y(),
        m_qimage.width() * m_scale, m_qimage.height() * m_scale);
    painter.drawImage(targetRect, m_qimage);
}

void FullFrameCanvas::drawRoiOverlays(QPainter& painter)
{
    for (int i = 0; i < m_rois.size(); ++i) {
        const auto& roi = m_rois[i];
        QPointF topLeft = imageToWidget(QPointF(roi.x, roi.y));
        QPointF bottomRight = imageToWidget(QPointF(roi.x + roi.w, roi.y + roi.h));
        QRectF roiRect(topLeft, bottomRight);

        bool isCurrent = (i == m_currentRoiIndex);

        // 半透明填充
        QColor fillColor = isCurrent ? QColor(255, 0, 0, 50) : QColor(79, 195, 247, 30);
        painter.fillRect(roiRect, fillColor);

        // 边框
        QPen pen = isCurrent ? QPen(QColor(255, 0, 0), 2) : QPen(QColor(79, 195, 247), 1);
        painter.setPen(pen);
        painter.drawRect(roiRect);

        // ROI编号标签
        painter.setPen(QColor(255, 255, 255));
        painter.setFont(QFont("Consolas", 8));
        QString label = QString("#%1").arg(i);
        painter.drawText(roiRect.topLeft() + QPointF(2, 12), label);
    }
}

void FullFrameCanvas::drawScaleBar(QPainter& painter)
{
    if (m_image.empty()) return;

    // 右下角比例尺
    static constexpr int kScaleBarPixels = 100;
    painter.setPen(QPen(QColor(200, 200, 200), 2));
    int x = width() - 120;
    int y = height() - 30;
    painter.drawLine(x, y, x + kScaleBarPixels, y);
    painter.drawLine(x, y - 5, x, y + 5);
    painter.drawLine(x + kScaleBarPixels, y - 5, x + kScaleBarPixels, y + 5);

    painter.setPen(QColor(200, 200, 200));
    painter.setFont(QFont("Consolas", 8));
    painter.drawText(x + 20, y - 8, "100px");
}

void FullFrameCanvas::drawInfo(QPainter& painter)
{
    if (m_image.empty()) return;

    // 左上角显示图像信息
    painter.setPen(QColor(150, 150, 150));
    painter.setFont(QFont("Consolas", 9));
    QString info = QString("%1×%2").arg(m_image.cols).arg(m_image.rows);
    painter.drawText(10, 20, info);
}

void FullFrameCanvas::wheelEvent(QWheelEvent* event)
{
    if (m_image.empty()) return;

    double factor = (event->angleDelta().y() > 0) ? 1.2 : 1.0 / 1.2;
    double newScale = m_scale * factor;

    // 限制缩放范围
    if (newScale < 0.05 || newScale > 20.0) return;

    // 以鼠标位置为中心缩放
    QPointF mousePos = event->position();
    QPointF imagePos = widgetToImage(mousePos);

    m_scale = newScale;
    m_offset = mousePos - imagePos * m_scale;

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
        QPointF delta = event->pos() - m_lastMousePos;
        m_offset += delta;
        m_lastMousePos = event->pos();
        update();
    }

    // 发送鼠标位置信号
    if (!m_image.empty()) {
        QPointF imgPos = widgetToImage(event->position());
        int ix = static_cast<int>(imgPos.x());
        int iy = static_cast<int>(imgPos.y());
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

// ============================================================
// RoiStarCanvas
// ============================================================

RoiStarCanvas::RoiStarCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void RoiStarCanvas::resizeEvent(QResizeEvent*)
{
    if (parentWidget()) {
        resize(parentWidget()->size());
    }
}

void RoiStarCanvas::setRoiImage(const cv::Mat& roiImage)
{
    m_roiImage = roiImage;
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
    m_hasCentroid = false;
    update();
}

void RoiStarCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 背景
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (m_roiImage.empty()) {
        painter.setPen(QColor(51, 51, 51));
        painter.setFont(QFont("Microsoft YaHei", 10));
        painter.drawText(rect(), Qt::AlignCenter, "无ROI数据");
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
    // 仅在图像变化时重新转换为QImage（缓存优化）
    if (m_imageDirty && !m_roiImage.empty()) {
        if (m_roiImage.channels() == 3) {
            cv::Mat rgb;
            cv::cvtColor(m_roiImage, rgb, cv::COLOR_BGR2RGB);
            m_qimage = QImage(rgb.data, rgb.cols, rgb.rows,
                static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
        } else if (m_roiImage.channels() == 1) {
            if (m_roiImage.type() == CV_64F) {
                cv::Mat display;
                double minVal, maxVal;
                cv::minMaxLoc(m_roiImage, &minVal, &maxVal);
                if (maxVal > minVal) {
                    m_roiImage.convertTo(display, CV_8U, 255.0 / (maxVal - minVal),
                        -255.0 * minVal / (maxVal - minVal));
                } else {
                    display = cv::Mat(m_roiImage.size(), CV_8U, cv::Scalar(128));
                }
                m_qimage = QImage(display.data, display.cols, display.rows,
                    static_cast<int>(display.step), QImage::Format_Grayscale8).copy();
            } else {
                m_qimage = QImage(m_roiImage.data, m_roiImage.cols, m_roiImage.rows,
                    static_cast<int>(m_roiImage.step), QImage::Format_Grayscale8).copy();
            }
        }
        m_imageDirty = false;
    }

    if (m_qimage.isNull()) return;

    // 居中缩放
    double scaleX = static_cast<double>(width()) / m_qimage.width();
    double scaleY = static_cast<double>(height()) / m_qimage.height();
    double scale = std::min(scaleX, scaleY);

    double offsetX = (width() - m_qimage.width() * scale) / 2.0;
    double offsetY = (height() - m_qimage.height() * scale) / 2.0;

    QRectF targetRect(offsetX, offsetY, m_qimage.width() * scale, m_qimage.height() * scale);
    painter.drawImage(targetRect, m_qimage);
}

void RoiStarCanvas::drawGrid(QPainter& painter)
{
    if (m_roiImage.empty()) return;

    double scaleX = static_cast<double>(width()) / m_roiImage.cols;
    double scaleY = static_cast<double>(height()) / m_roiImage.rows;
    double scale = std::min(scaleX, scaleY);

    double offsetX = (width() - m_roiImage.cols * scale) / 2.0;
    double offsetY = (height() - m_roiImage.rows * scale) / 2.0;

    painter.setPen(QPen(QColor(50, 50, 50), 0.5));

    // 垂直线
    for (int x = 0; x <= m_roiImage.cols; ++x) {
        double px = offsetX + x * scale;
        painter.drawLine(QPointF(px, offsetY), QPointF(px, offsetY + m_roiImage.rows * scale));
    }

    // 水平线
    for (int y = 0; y <= m_roiImage.rows; ++y) {
        double py = offsetY + y * scale;
        painter.drawLine(QPointF(offsetX, py), QPointF(offsetX + m_roiImage.cols * scale, py));
    }
}

void RoiStarCanvas::drawCentroid(QPainter& painter)
{
    if (m_roiImage.empty()) return;

    double scaleX = static_cast<double>(width()) / m_roiImage.cols;
    double scaleY = static_cast<double>(height()) / m_roiImage.rows;
    double scale = std::min(scaleX, scaleY);

    double offsetX = (width() - m_roiImage.cols * scale) / 2.0;
    double offsetY = (height() - m_roiImage.rows * scale) / 2.0;

    double cx = offsetX + m_centroidX * scale;
    double cy = offsetY + m_centroidY * scale;

    // 十字线
    painter.setPen(QPen(QColor(255, 0, 0), 1));
    double crossSize = 10;
    painter.drawLine(QPointF(cx - crossSize, cy), QPointF(cx + crossSize, cy));
    painter.drawLine(QPointF(cx, cy - crossSize), QPointF(cx, cy + crossSize));

    // 中心圆
    painter.setPen(QPen(QColor(255, 0, 0), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, cy), 5, 5);

    // 坐标值
    painter.setPen(QColor(0, 200, 255));
    painter.setFont(QFont("Consolas", 9));
    QString coordText = QString("(%1, %2)")
        .arg(m_centroidX, 0, 'f', 1)
        .arg(m_centroidY, 0, 'f', 1);
    painter.drawText(QPointF(cx + 12, cy - 5), coordText);
}

void RoiStarCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_roiImage.empty()) return;

    double scaleX = static_cast<double>(width()) / m_roiImage.cols;
    double scaleY = static_cast<double>(height()) / m_roiImage.rows;
    double scale = std::min(scaleX, scaleY);

    double offsetX = (width() - m_roiImage.cols * scale) / 2.0;
    double offsetY = (height() - m_roiImage.rows * scale) / 2.0;

    int px = static_cast<int>((event->pos().x() - offsetX) / scale);
    int py = static_cast<int>((event->pos().y() - offsetY) / scale);

    if (px >= 0 && px < m_roiImage.cols && py >= 0 && py < m_roiImage.rows) {
        double value = 0;
        if (m_roiImage.type() == CV_64F) {
            value = m_roiImage.at<double>(py, px);
        } else if (m_roiImage.type() == CV_8U) {
            value = m_roiImage.at<uchar>(py, px);
        }
        setToolTip(QString("(%1, %2) = %3").arg(px).arg(py).arg(value, 0, 'f', 1));
    }
}

// ============================================================
// ChartWidget
// ============================================================

ChartWidget::ChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 150);
}

void ChartWidget::addDataPoint(double r0, double seeing, double centroidX, double centroidY)
{
    m_r0Data.append(r0);
    m_seeingData.append(seeing);
    m_trajectoryData.append(QPointF(centroidX, centroidY));
    m_timeCounter++;

    // 限制数据量
    while (m_r0Data.size() > MAX_POINTS) m_r0Data.removeFirst();
    while (m_seeingData.size() > MAX_POINTS) m_seeingData.removeFirst();
    while (m_trajectoryData.size() > MAX_POINTS) m_trajectoryData.removeFirst();

    update();
}

void ChartWidget::clear()
{
    m_r0Data.clear();
    m_seeingData.clear();
    m_trajectoryData.clear();
    m_timeCounter = 0;
    update();
}

void ChartWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 背景
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (m_r0Data.isEmpty()) {
        painter.setPen(QColor(51, 51, 51));
        painter.setFont(QFont("Microsoft YaHei", 10));
        painter.drawText(rect(), Qt::AlignCenter, "无数据");
        return;
    }

    // 将区域分为3部分
    int w = width();
    int h = height();
    int chartW = w / 3 - 10;

    QRect r0Rect(5, 5, chartW, h - 10);
    QRect seeingRect(chartW + 15, 5, chartW, h - 10);
    QRect trajectoryRect(2 * chartW + 25, 5, w - 2 * chartW - 30, h - 10);

    drawR0Chart(painter, r0Rect);
    drawSeeingChart(painter, seeingRect);
    drawTrajectory(painter, trajectoryRect);
}

void ChartWidget::drawR0Chart(QPainter& painter, QRect rect)
{
    // 标题
    painter.setPen(QColor(79, 195, 247));
    painter.setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
    painter.drawText(rect.x() + 5, rect.y() + 15, "大气相干长度 r₀");

    QRect chartRect(rect.x() + 5, rect.y() + 25, rect.width() - 10, rect.height() - 35);

    // 背景
    painter.fillRect(chartRect, QColor(30, 30, 48));

    if (m_r0Data.isEmpty()) return;

    drawAxes(painter, chartRect, m_r0Data, 0, 20, "cm");

    // 绘制曲线
    if (m_r0Data.size() < 2) return;

    QPen curvePen(QColor(79, 195, 247), 1.5);
    painter.setPen(curvePen);

    double minY = 0, maxY = 20;
    double xStep = static_cast<double>(chartRect.width()) / (MAX_POINTS - 1);

    QPainterPath path;
    for (int i = 0; i < m_r0Data.size(); ++i) {
        double x = chartRect.x() + i * xStep;
        double y = chartRect.y() + chartRect.height() -
            (m_r0Data[i] - minY) / (maxY - minY) * chartRect.height();
        if (i == 0) path.moveTo(x, y);
        else path.lineTo(x, y);
    }
    painter.drawPath(path);
}

void ChartWidget::drawSeeingChart(QPainter& painter, QRect rect)
{
    painter.setPen(QColor(139, 195, 74));
    painter.setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
    painter.drawText(rect.x() + 5, rect.y() + 15, "视宁度");

    QRect chartRect(rect.x() + 5, rect.y() + 25, rect.width() - 10, rect.height() - 35);
    painter.fillRect(chartRect, QColor(30, 30, 48));

    if (m_seeingData.isEmpty()) return;

    drawAxes(painter, chartRect, m_seeingData, 0, 5, "角秒");

    if (m_seeingData.size() < 2) return;

    QPen curvePen(QColor(139, 195, 74), 1.5);
    painter.setPen(curvePen);

    double minY = 0, maxY = 5;
    double xStep = static_cast<double>(chartRect.width()) / (MAX_POINTS - 1);

    QPainterPath path;
    for (int i = 0; i < m_seeingData.size(); ++i) {
        double x = chartRect.x() + i * xStep;
        double y = chartRect.y() + chartRect.height() -
            (m_seeingData[i] - minY) / (maxY - minY) * chartRect.height();
        if (i == 0) path.moveTo(x, y);
        else path.lineTo(x, y);
    }
    painter.drawPath(path);
}

void ChartWidget::drawTrajectory(QPainter& painter, QRect rect)
{
    painter.setPen(QColor(255, 152, 0));
    painter.setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
    painter.drawText(rect.x() + 5, rect.y() + 15, "质心轨迹");

    QRect chartRect(rect.x() + 5, rect.y() + 25, rect.width() - 10, rect.height() - 35);
    painter.fillRect(chartRect, QColor(30, 30, 48));

    if (m_trajectoryData.isEmpty()) return;

    // 计算范围
    double minX = m_trajectoryData[0].x(), maxX = minX;
    double minY = m_trajectoryData[0].y(), maxY = minY;
    for (const auto& p : m_trajectoryData) {
        minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
    }

    double rangeX = maxX - minX;
    double rangeY = maxY - minY;
    if (rangeX < 1) rangeX = 1;
    if (rangeY < 1) rangeY = 1;

    // 边距
    minX -= rangeX * 0.1;
    maxX += rangeX * 0.1;
    minY -= rangeY * 0.1;
    maxY += rangeY * 0.1;

    // 绘制散点
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 152, 0));

    for (int i = 0; i < m_trajectoryData.size(); ++i) {
        double x = chartRect.x() +
            (m_trajectoryData[i].x() - minX) / (maxX - minX) * chartRect.width();
        double y = chartRect.y() + chartRect.height() -
            (m_trajectoryData[i].y() - minY) / (maxY - minY) * chartRect.height();

        // 渐变透明度（越新的点越不透明）
        int alpha = 50 + 205 * i / m_trajectoryData.size();
        painter.setBrush(QColor(255, 152, 0, alpha));
        painter.drawEllipse(QPointF(x, y), 2, 2);
    }
}

void ChartWidget::drawAxes(QPainter& painter, QRect rect, const QVector<double>& data,
                           double minY, double maxY, const QString& unit)
{
    // 坐标轴
    painter.setPen(QPen(QColor(80, 80, 80), 1));
    painter.drawLine(rect.bottomLeft(), rect.bottomRight());
    painter.drawLine(rect.bottomLeft(), rect.topLeft());

    // Y轴标签
    painter.setPen(QColor(120, 120, 120));
    painter.setFont(QFont("Consolas", 7));
    painter.drawText(rect.x() - 30, rect.y() + 5, QString("%1%2").arg(maxY, 0, 'f', 1).arg(unit));
    painter.drawText(rect.x() - 30, rect.y() + rect.height(),
        QString("%1%2").arg(minY, 0, 'f', 1).arg(unit));
}
