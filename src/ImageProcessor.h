#pragma once

#include <QObject>
#include <QVector>
#include <QList>
#include <QThread>
#include <QMutex>
#include <opencv2/opencv.hpp>

// ROI矩形
struct RoiRect {
    int x = 0, y = 0, w = 64, h = 64;
};

// 质心结果
struct CentroidResult {
    double x = 0.0, y = 0.0;
    bool valid = false;
    double peakValue = 0.0;
    double totalFlux = 0.0;
};

// 大气参数
struct AtmosphericParams {
    double r0 = 0.0;
    double seeing = 0.0;
    double theta0 = 0.0;
    double tau0 = 0.0;
};

// ============================================================
// 工作线程：执行耗时的图像处理计算
// ============================================================
class ImageProcessorWorker : public QObject {
    Q_OBJECT
public:
    explicit ImageProcessorWorker(QObject* parent = nullptr);

signals:
    void centroidReady(int cameraIndex, double x, double y);
    void roiImageReady(int cameraIndex, cv::Mat roiImage);
    void atmosphereReady(double r0, double seeing, double theta0, double tau0);

public slots:
    // 参数设置（线程安全，加锁）
    void setCentroidMethod(int method);
    void setGaussianKernelSize(int size);
    void setGaussianSigma(double sigma);
    void setThreshold(double threshold);
    void setOpticalParams(double D, double f, double lambda, double pixelSize);
    void setCurrentRoi(int cameraIndex, const RoiRect& roi);
    void processFrame(int cameraIndex, cv::Mat frame);

private:
    mutable QMutex m_mutex;
    int m_method = 0;
    int m_kernelSize = 3;
    double m_sigma = 1.0;
    double m_threshold = 0.0;
    double m_D = 0.56, m_f = 2.69, m_lambda = 550e-9, m_pixelSize = 2.5e-6;
    RoiRect m_currentRoi[2];
    QList<CentroidResult> m_centroidHistory[2];
    static constexpr int MAX_HISTORY = 200;

    cv::Mat preprocess(const cv::Mat& image);
    CentroidResult calculateCentroid(const cv::Mat& roiImage);
    CentroidResult centerOfGravity(const cv::Mat& image);
    CentroidResult gaussianFit(const cv::Mat& image);
    AtmosphericParams calculateAtmosphere(const QList<CentroidResult>& centroids);
};

// ============================================================
// ImageProcessor：主线程接口，管理工作线程
// ============================================================
class ImageProcessor : public QObject {
    Q_OBJECT
public:
    explicit ImageProcessor(QObject* parent = nullptr);
    ~ImageProcessor();

    void setCentroidMethod(int method);
    void setGaussianKernelSize(int size);
    void setGaussianSigma(double sigma);
    void setThreshold(double threshold);
    void setOpticalParams(double D, double f, double lambda, double pixelSize);
    void setCurrentRoi(int cameraIndex, const RoiRect& roi);
    RoiRect getCurrentRoi(int cameraIndex) const;

public slots:
    void processFrame(int cameraIndex, const cv::Mat& frame);

signals:
    void centroidReady(int cameraIndex, double x, double y);
    void roiImageReady(int cameraIndex, cv::Mat roiImage);
    void atmosphereReady(double r0, double seeing, double theta0, double tau0);

private:
    QThread* m_workerThread = nullptr;
    ImageProcessorWorker* m_worker = nullptr;
    RoiRect m_currentRoi[2]; // 主线程副本，供UI读取
};
