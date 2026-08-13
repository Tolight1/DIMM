from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AlignmentCoarseDiagnosticsStaticTest(unittest.TestCase):
    def test_estimator_header_exposes_diagnostic_fields(self):
        header = read("src/AlignmentCoarseEstimator.h")

        for token in [
            "minTrackSpeedPxSec",
            "medianFittedSpeedPxSec",
            "fittedTrackCount",
            "requiredTrackCount",
            "diagnosticText",
            "velocityFitValid",
            "rejectionReason",
            "displacementPx",
            "pointCount",
        ]:
            self.assertIn(token, header)

    def test_estimator_uses_configured_speed_threshold(self):
        cpp = read("src/AlignmentCoarseEstimator.cpp")

        self.assertIn("trackRejectionReason", cpp)
        self.assertIn("config.minTrackSpeedPxSec", cpp)
        self.assertNotIn("track.speedPxSec <= 0.005", cpp)
        self.assertIn("medianFittedSpeedPxSec", cpp)
        self.assertIn("fittedTrackCount", cpp)
        self.assertIn("buildTrackDiagnosticText", cpp)
        self.assertIn("selectDiagnosticTrack", cpp)

    def test_single_usable_track_has_explicit_status(self):
        cpp = read("src/AlignmentCoarseEstimator.cpp")

        self.assertIn("已获得1/%1条可用轨迹", cpp)
        self.assertIn("还需至少1条方向不同的轨迹", cpp)
        self.assertIn("净位移%1px，小于要求%2px", cpp)
        self.assertIn("线性拟合RMS", cpp)

    def test_canvas_displays_active_fitted_and_usable_counts(self):
        header = read("src/CanvasWidgets.h")
        cpp = read("src/CanvasWidgets.cpp")

        for token in [
            "activeTrackCount",
            "fittedTrackCount",
            "requiredTrackCount",
            "medianFittedSpeedPxSec",
            "diagnosticText",
        ]:
            self.assertIn(token, header)

        self.assertIn("活动轨迹 %2", cpp)
        self.assertIn("已拟合 %3", cpp)
        self.assertIn("可用轨迹 %4/%5", cpp)
        self.assertIn("拟合速度 %1 px/s", cpp)
        self.assertIn("求解速度 %2 px/s", cpp)
        self.assertIn("Qt::TextWordWrap", cpp)

    def test_dimm_copies_new_overlay_fields(self):
        cpp = read("src/DIMM.Alignment.cpp")

        for token in [
            "overlay.medianFittedSpeedPxSec",
            "overlay.activeTrackCount",
            "overlay.fittedTrackCount",
            "overlay.requiredTrackCount",
            "overlay.diagnosticText",
            "drawTrack.displacementPx",
            "drawTrack.velocityFitValid",
            "drawTrack.rejectionReason",
        ]:
            self.assertIn(token, cpp)

    def test_no_build_system_change_is_required(self):
        cmake = read("CMakeLists.txt")

        self.assertNotIn(
            "test_alignment_coarse_diagnostics_static.py",
            cmake,
        )


if __name__ == "__main__":
    unittest.main()
