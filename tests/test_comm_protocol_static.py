import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


class CommProtocolStaticTest(unittest.TestCase):
    def test_documented_monitoring_frame_constants_are_present(self):
        header = read("src/CommProtocol.h")
        implementation = read("src/CommProtocol.cpp")
        self.assertIn("0x49, 0x96, 0x02, 0xD2", implementation)
        self.assertIn("0xB6, 0x69, 0xFD, 0x2E", implementation)
        self.assertIn("0x01, 0x03, 0x03, 0x02, 0x00, 0x00", implementation)
        self.assertIn("0x01, 0x03, 0x03, 0x05, 0x00, 0x00", implementation)
        self.assertIn("MSG_TYPE = 0x07", header)
        self.assertIn("DATA_SIZE = DATA_FLOAT_SIZE + DEVICE_STATUS_SIZE", header)
        self.assertIn("DEVICE_STATUS_SIZE = 4", header)
        self.assertIn("DEVICE_STATUS_CAMERA_A_CONNECTION", header)

    def test_dimm_uses_single_autonomous_monitoring_frame(self):
        manager = read("src/CommManager.h")
        results = read("src/DIMM.Results.cpp")
        dimm = read("src/DIMM.cpp")
        settings = read("src/SettingsDialog.cpp")
        self.assertIn("sendMonitoringFrame", manager)
        self.assertIn("sendMonitoringFrame", results)
        self.assertNotIn("sendDeviceStatus", results)
        self.assertIn("m_reportTimer->start", dimm)
        self.assertIn('QStringLiteral("169.254.100.2")', manager)
        self.assertIn('QStringLiteral("169.254.100.2")', settings)
        self.assertIn("LEN: 85 bytes, total frame: 93 bytes", settings)
        self.assertNotIn("LEN: 65 bytes, total frame: 73 bytes", settings)

    def test_monitoring_data_uses_per_camera_values_and_nan_for_invalid_values(self):
        protocol = read("src/CommProtocol.h")
        manager = read("src/CommManager.h")
        results = read("src/DIMM.Results.cpp")
        self.assertIn("float peakBrightnessCameraA", protocol)
        self.assertIn("float peakBrightnessCameraB", protocol)
        self.assertIn("float exposureTimeCameraAUs", protocol)
        self.assertIn("float exposureTimeCameraBUs", protocol)
        self.assertIn("float frameRateHz", protocol)
        self.assertIn("std::uint32_t deviceStatus", protocol)
        self.assertIn("float peakBrightnessCameraA", manager)
        self.assertIn("float peakBrightnessCameraB", manager)
        self.assertIn("std::numeric_limits<float>::quiet_NaN()", results)
        self.assertIn("runtime.peakBrightness[0]", results)
        self.assertIn("runtime.peakBrightness[1]", results)
        self.assertIn("m_cameraExposureUs[0]", results)
        self.assertIn("m_cameraExposureUs[1]", results)
        self.assertIn("frameRateHz", results)
        self.assertIn("monitoringDeviceStatus", results)
        self.assertIn("runtime.latestAtmosphere.tau0Valid", results)

    def test_invalid_atmosphere_still_allows_nan_monitoring_frame(self):
        dimm = read("src/DIMM.cpp")
        can_report = dimm.split("bool DIMM::canReportMeasurements() const", 1)[1].split(
            "QString DIMM::captureModeName() const", 1
        )[0]
        self.assertIn("m_commConnected", can_report)
        self.assertIn("m_reporting", can_report)
        self.assertIn("isLiveCaptureActive()", can_report)
        self.assertNotIn("hasValidAtmosphere", can_report)


if __name__ == "__main__":
    unittest.main()
