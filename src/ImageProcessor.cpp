#include "ImageProcessor.h"
#include <cmath>

// ============================================================
// ImageProcessorWorker（工作线程）
// ============================================================

ImageProcessorWorker::ImageProcessorWorker(QObject* parent)
    : QObject(parent)
{
}

void ImageProcessorWorker::setCentroidMethod(int method)
{
    QMutexLocker locker(&m_mutex);
    m_method = method;
}

void ImageProcessorWorker::setGaussianKernelSize(int size)
{
    QMutexLocker locker(&m_mutex);
    m_kernelSize = (size % 2 == 0) ? size + 1 : size;
}

void ImageProcessorWorker::setGaussianSigma(double sigma)
{
    QMutexLocker locker(&m_mutex);
    m_sigma = sigma;
}

void ImageProcessorWorker::setThreshold(double threshold)
{
    QMutexLocker locker(&m_mutex);
    m_threshold = threshold;
}

void ImageProcessorWorker::setOpticalParams(double D, double f, double lambda, double pixelSize)
{
    QMutexLocker locker(&m_mutex);
    m_D = D / 100.0;
    m_f = f / 100.0;
    m_lambda = lambda * 1e-9;
    m_pixelSize = pixelSize * 1e-6;
}

void ImageProcessorWorker::setCurrentRoi(int cameraIndex, const RoiRect& roi)
{
    if (cameraIndex < 0 || cameraIndex >= 2) return;
    QMutexLocker locker(&m_mutex);
    m_currentRoi[cameraIndex] = roi;
}

cv::Mat ImageProcessorWorker::preprocess(const cv::Mat& image)
{
    cv::Mat processed;
    if (image.channels() == 3) {
        cv::cvtColor(image, processed, cv::COLOR_BGR2GRAY);
    } else {
        processed = image.clone();
    }
    processed.convertTo(processed, CV_64F);
    if (m_kernelSize > 0) {
        cv::GaussianBlur(processed, processed,
            cv::Size(m_kernelSize, m_kernelSize), m_sigma);
    }
    if (m_threshold > 0) {
        cv::threshold(processed, processed, m_threshold, 0, cv::THRESH_TOZERO);
    }
    return processed;
}

CentroidResult ImageProcessorWorker::calculateCentroid(const cv::Mat& roiImage)
{
    if (roiImage.empty()) return CentroidResult();
    cv::Mat processed;
    if (roiImage.type() != CV_64F) {
        processed = preprocess(roiImage);
    } else {
        processed = roiImage;
    }
    if (m_method == 1) {
        return gaussianFit(processed);
    }
    return centerOfGravity(processed);
}

CentroidResult ImageProcessorWorker::centerOfGravity(const cv::Mat& image)
{
    CentroidResult result;
    if (image.empty()) return result;
    double sumX = 0, sumY = 0, sumW = 0, peakValue = 0;
    for (int y = 0; y < image.rows; ++y) {
        const double* row = image.ptr<double>(y);
        for (int x = 0; x < image.cols; ++x) {
            double val = row[x];
            if (val > peakValue) peakValue = val;
            sumX += x * val;
            sumY += y * val;
            sumW += val;
        }
    }
    if (sumW > 0) {
        result.x = sumX / sumW;
        result.y = sumY / sumW;
        result.valid = true;
        result.peakValue = peakValue;
        result.totalFlux = sumW;
    }
    return result;
}

CentroidResult ImageProcessorWorker::gaussianFit(const cv::Mat& image)
{
    CentroidResult result;
    if (image.empty()) return result;
    CentroidResult cog = centerOfGravity(image);
    if (!cog.valid) return result;
    double cx = cog.x, cy = cog.y, sigmaEst = 3.0;
    for (int iter = 0; iter < 3; ++iter) {
        double sumX = 0, sumY = 0, sumW = 0;
        for (int y = 0; y < image.rows; ++y) {
            const double* row = image.ptr<double>(y);
            for (int x = 0; x < image.cols; ++x) {
                double dx = x - cx, dy = y - cy;
                double weight = row[x] * exp(-(dx*dx + dy*dy) / (2 * sigmaEst * sigmaEst));
                sumX += x * weight;
                sumY += y * weight;
                sumW += weight;
            }
        }
        if (sumW > 0) { cx = sumX / sumW; cy = sumY / sumW; }
    }
    result.x = cx; result.y = cy; result.valid = true;
    result.peakValue = cog.peakValue; result.totalFlux = cog.totalFlux;
    return result;
}

AtmosphericParams ImageProcessorWorker::calculateAtmosphere(const QList<CentroidResult>& centroids)
{
    AtmosphericParams params;
    if (centroids.size() < 2 || m_f <= 0 || m_lambda <= 0 || m_pixelSize <= 0)
        return params;
    double meanX = 0, meanY = 0;
    int validCount = 0;
    for (const auto& c : centroids) {
        if (!c.valid) continue;
        meanX += c.x; meanY += c.y; validCount++;
    }
    if (validCount < 2) return params;
    meanX /= validCount; meanY /= validCount;
    double varX = 0, varY = 0;
    for (const auto& c : centroids) {
        if (!c.valid) continue;
        varX += (c.x - meanX) * (c.x - meanX);
        varY += (c.y - meanY) * (c.y - meanY);
    }
    varX /= (validCount - 1); varY /= (validCount - 1);
    double pixelScale = m_pixelSize / m_f * 206265.0;
    double varArcsec2 = (varX + varY) / 2.0 * pixelScale * pixelScale;
    double seeing = sqrt(varArcsec2) * 2.35;
    if (seeing > 0) {
        params.r0 = 0.98 * m_lambda / (seeing * M_PI / 180.0 / 3600.0) * 100;
        params.seeing = seeing;
    }
    if (params.r0 > 0)
        params.theta0 = 0.31 * params.r0 / 100.0 / (0.98 * m_lambda) * 206265.0 / 3600.0;
    if (params.r0 > 0)
        params.tau0 = 0.31 * params.r0 / 100.0 / 10.0 * 1000;
    return params;
}

void ImageProcessorWorker::processFrame(int cameraIndex, cv::Mat frame)
{
    if (frame.empty() || cameraIndex < 0 || cameraIndex >= 2) return;

    RoiRect roi;
    {
        QMutexLocker locker(&m_mutex);
        roi = m_currentRoi[cameraIndex];
    }

    // 提取ROI
    int x = std::max(0, roi.x), y = std::max(0, roi.y);
    int w = std::min(roi.w, frame.cols - x);
    int h = std::min(roi.h, frame.rows - y);
    if (w <= 0 || h <= 0) return;
    cv::Mat roiImage = frame(cv::Rect(x, y, w, h)).clone();
    if (roiImage.empty()) return;

    // 计算质心
    CentroidResult centroid = calculateCentroid(roiImage);
    if (centroid.valid) {
        emit centroidReady(cameraIndex, roi.x + centroid.x, roi.y + centroid.y);

        // 保存历史
        m_centroidHistory[cameraIndex].append(centroid);
        if (m_centroidHistory[cameraIndex].size() > MAX_HISTORY)
            m_centroidHistory[cameraIndex].removeFirst();

        // 每积累足够数据后计算大气参数
        int histSize = m_centroidHistory[cameraIndex].size();
        if (histSize >= 10 && histSize % 5 == 0) {
            AtmosphericParams params = calculateAtmosphere(m_centroidHistory[cameraIndex]);
            if (params.r0 > 0)
                emit atmosphereReady(params.r0, params.seeing, params.theta0, params.tau0);
        }
    }
    emit roiImageReady(cameraIndex, roiImage);
}

// ============================================================
// ImageProcessor（主线程接口）
// ============================================================

ImageProcessor::ImageProcessor(QObject* parent)
    : QObject(parent)
{
    m_workerThread = new QThread(this);
    m_worker = new ImageProcessorWorker();
    m_worker->moveToThread(m_workerThread);

    // 转发信号
    connect(m_worker, &ImageProcessorWorker::centroidReady,
            this, &ImageProcessor::centroidReady);
    connect(m_worker, &ImageProcessorWorker::roiImageReady,
            this, &ImageProcessor::roiImageReady);
    connect(m_worker, &ImageProcessorWorker::atmosphereReady,
            this, &ImageProcessor::atmosphereReady);

    m_workerThread->start();
}

ImageProcessor::~ImageProcessor()
{
    m_workerThread->quit();
    m_workerThread->wait();
}

void ImageProcessor::setCentroidMethod(int method)
{
    QMetaObject::invokeMethod(m_worker, "setCentroidMethod", Qt::QueuedConnection, Q_ARG(int, method));
}

void ImageProcessor::setGaussianKernelSize(int size)
{
    QMetaObject::invokeMethod(m_worker, "setGaussianKernelSize", Qt::QueuedConnection, Q_ARG(int, size));
}

void ImageProcessor::setGaussianSigma(double sigma)
{
    QMetaObject::invokeMethod(m_worker, "setGaussianSigma", Qt::QueuedConnection, Q_ARG(double, sigma));
}

void ImageProcessor::setThreshold(double threshold)
{
    QMetaObject::invokeMethod(m_worker, "setThreshold", Qt::QueuedConnection, Q_ARG(double, threshold));
}

void ImageProcessor::setOpticalParams(double D, double f, double lambda, double pixelSize)
{
    QMetaObject::invokeMethod(m_worker, "setOpticalParams", Qt::QueuedConnection,
        Q_ARG(double, D), Q_ARG(double, f), Q_ARG(double, lambda), Q_ARG(double, pixelSize));
}

void ImageProcessor::setCurrentRoi(int cameraIndex, const RoiRect& roi)
{
    if (cameraIndex < 0 || cameraIndex >= 2) return;
    m_currentRoi[cameraIndex] = roi;
    QMetaObject::invokeMethod(m_worker, "setCurrentRoi", Qt::QueuedConnection,
        Q_ARG(int, cameraIndex), Q_ARG(RoiRect, roi));
}

RoiRect ImageProcessor::getCurrentRoi(int cameraIndex) const
{
    if (cameraIndex < 0 || cameraIndex >= 2) return RoiRect();
    return m_currentRoi[cameraIndex];
}

void ImageProcessor::processFrame(int cameraIndex, const cv::Mat& frame)
{
    if (frame.empty() || cameraIndex < 0 || cameraIndex >= 2) return;
    // 深拷贝帧数据，安全传入工作线程
    cv::Mat frameCopy = frame.clone();
    QMetaObject::invokeMethod(m_worker, "processFrame", Qt::QueuedConnection,
        Q_ARG(int, cameraIndex), Q_ARG(cv::Mat, frameCopy));
}
