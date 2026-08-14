from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@dataclass
class Sample:
    t: int
    peak0: float
    peak1: float
    snr0: float
    snr1: float
    valid0: bool
    valid1: bool
    saturated0: bool = False
    saturated1: bool = False


def assert_source_contains(path: str, needle: str) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    if needle not in text:
        raise AssertionError(f"{path} missing {needle}")


def assert_source_not_contains_in_ae_region(path: str, forbidden: str) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    start_marker = "void DIMM::handleAutoExposureSample"
    end_marker = "QVector<int> DIMM::scanHotPixelExposureTemplates"
    start = text.find(start_marker)
    if start < 0:
        raise AssertionError(f"{path} missing {start_marker}")
    end = text.find(end_marker, start)
    if end < 0:
        raise AssertionError(f"{path} missing {end_marker}")
    region = text[start:end]
    if forbidden in region:
        raise AssertionError(f"{path} auto-exposure region contains forbidden call {forbidden}")


def test_static_interfaces() -> None:
    assert_source_contains("src/AutoExposureController.h", "enum class AutoExposureState")
    assert_source_contains("src/AutoExposureController.h", "struct AutoExposureFrameSample")
    assert_source_contains("src/AutoExposureController.h", "class AutoExposureController")
    assert_source_contains("src/ImageProcessor.h", "autoExposureSampleReady")
    assert_source_contains("src/DIMM.cpp", "handleAutoExposureSample")
    assert_source_contains("src/DIMM.cpp", "WEATHER_TOO_DARK")
    assert_source_not_contains_in_ae_region("src/DIMM.cpp", "stopLiveCapture(")
    assert_source_not_contains_in_ae_region("src/DIMM.cpp", "resetRunProcessingState(")


def test_auto_exposure_ui_thread_load_is_bounded() -> None:
    assert_source_contains("src/ImageProcessor.h", "m_autoExposureMetricsEnabled")
    assert_source_contains("src/ImageProcessor.h", "m_autoExposureMetricIntervalMs")
    assert_source_contains("src/ImageProcessor.cpp", "shouldEmitAutoExposureSample")
    assert_source_contains("src/ImageProcessor.cpp", "m_lastAutoExposureSampleMs")
    assert_source_contains("src/DIMM.h", "m_cachedHotPixelTemplateExposures")
    assert_source_contains("src/DIMM.cpp", "m_cachedHotPixelTemplateScanMs")
    assert_source_contains("src/DIMM.ui", "lblStatExposure")
    assert_source_contains("src/DIMM.cpp", "ui->lblStatExposure->setText")
    assert_source_contains("src/DIMM.ui", "lblStatAutoExposure")
    assert_source_contains("src/DIMM.cpp", "ui->lblStatAutoExposure->setText")
    assert_source_contains("src/DIMM.cpp", "autoExposureStateShortText")
    assert_source_contains("src/DIMM.cpp", "ROI峰值")


def test_auto_exposure_uses_roi_dn_scale_and_overbright_loss() -> None:
    assert_source_contains("src/ImageProcessor.cpp", "autoExposureDnScale")
    assert_source_contains("src/ImageProcessor.cpp", "4095.0 / 255.0")
    assert_source_contains("src/AutoExposureController.h", "sharedUsableRatio")
    assert_source_contains("src/AutoExposureController.h", "overbrightUnusable")
    assert_source_contains("src/AppConfig.h", "int brightPersistenceSec = 5")


def test_auto_exposure_default_and_lower_limit_status() -> None:
    assert_source_contains("src/AppConfig.h", "double exposureUs = 1000.0")
    assert_source_contains("src/DIMM.h", "double m_configExposureUs = 1000.0")
    assert_source_contains("src/SettingsDialog.cpp", "new QLineEdit(QStringLiteral(\"1000\"))")
    assert_source_contains("src/AutoExposureController.h", "LOWER_EXPOSURE_UNAVAILABLE")
    assert_source_contains("src/DIMM.cpp", "无低档模板")


def test_roi_pairing_not_blocked_by_edge_tail_only() -> None:
    assert_source_contains("src/ImageProcessor.cpp", "const bool measurementUsable = correctedMeasurementUsable")
    assert_source_contains("src/ImageProcessor.cpp", "kMaxMeasurementSignalPixels = 1600")
    assert_source_contains("src/ImageProcessor.cpp", "autoExposureMeasurementUsable")
    assert_source_contains("src/ImageProcessor.cpp", "centroid.signalPixelCount <= kMaxMeasurementSignalPixels")
    assert_source_contains("src/DIMM.h", "m_latestAutoExposureUsableRatio")
    assert_source_contains("src/DIMM.cpp", "measurementUsableRatio")
    assert_source_contains("src/DIMM.cpp", "AE质量")


def test_bright_adjustment_never_increases_exposure() -> None:
    assert_source_contains("src/AutoExposureController.h", "darken target must not increase exposure")
    assert_source_contains("src/AutoExposureController.h", "rawTarget >= currentExposureUs")
    assert_source_contains("src/AutoExposureController.h", "darken adjustment uses the next lower template")
    assert_source_contains("src/AutoExposureController.h", "nearestIt - exposures.begin() - 1")


def test_hot_pixel_template_path_preserves_directory_width() -> None:
    assert_source_contains("src/PathUtils.cpp", "const int width = valueEnd - valueStart")
    assert_source_contains("src/PathUtils.cpp", ".arg(exposureUs, width, 10")


def test_cooldown_waits_until_target_range() -> None:
    assert_source_contains("src/AutoExposureController.h", "m_waitingForTargetAfterAdjustment")
    assert_source_contains("src/AutoExposureController.h", "TARGET_REACHED_COOLDOWN")
    assert_source_contains("src/AutoExposureController.h", "cooldown only starts after the target range is reached")


def main() -> None:
    test_static_interfaces()
    test_auto_exposure_ui_thread_load_is_bounded()
    test_auto_exposure_uses_roi_dn_scale_and_overbright_loss()
    test_auto_exposure_default_and_lower_limit_status()
    test_roi_pairing_not_blocked_by_edge_tail_only()
    test_bright_adjustment_never_increases_exposure()
    test_hot_pixel_template_path_preserves_directory_width()
    test_cooldown_waits_until_target_range()
    print("auto exposure state-machine verification passed")


if __name__ == "__main__":
    main()
