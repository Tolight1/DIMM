from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmRemainingCppSplitsStaticTest(unittest.TestCase):
    def test_simulation_members_live_in_dimm_simulation_cpp(self):
        dimm = read("src/DIMM.cpp")
        sim = read("src/DIMM.Simulation.cpp")
        for token in [
            "void DIMM::onStartSimulation()",
            "void DIMM::stopSimulationCapture()",
            "bool DIMM::startSimulationCapture()",
            "cv::Mat DIMM::buildSimulationFrame(int cameraIndex) const",
            "void DIMM::onUpdateSimulation()",
        ]:
            self.assertIn(token, sim)
            self.assertNotIn(token, dimm)

    def test_results_members_live_in_dimm_results_cpp(self):
        dimm = read("src/DIMM.cpp")
        results = read("src/DIMM.Results.cpp")
        for token in [
            "void DIMM::initResultFile()",
            "void DIMM::initDetailResultFile()",
            "void DIMM::closeResultFile()",
            "void DIMM::saveResultRow(int frame)",
            "void DIMM::saveDetailResultRows",
            "void DIMM::flushPendingWrites()",
            "QString DIMM::csvSafeField(QString value) const",
            "void DIMM::reportMeasurement()",
            "void DIMM::reportDeviceStatus()",
        ]:
            self.assertIn(token, results)
            self.assertNotIn(token, dimm)

    def test_comm_camera_members_live_in_dimm_comm_camera_cpp(self):
        dimm = read("src/DIMM.cpp")
        comm = read("src/DIMM.CommCamera.cpp")
        for token in [
            "void DIMM::onConnectAll()",
            "void DIMM::onDisconnectAll()",
            "void DIMM::onCameraConnected",
            "void DIMM::onCameraDisconnected",
            "void DIMM::onCameraError",
            "void DIMM::onFrameReady",
            "void DIMM::onCapturedFramePacket",
            "void DIMM::handleLiveFramePacket",
            "void DIMM::scheduleHardwareTriggerStartupCheck()",
            "void DIMM::checkHardwareTriggerStartup()",
            "void DIMM::onCommCommand",
        ]:
            self.assertIn(token, comm)
            self.assertNotIn(token, dimm)

    def test_auto_exposure_members_live_in_dimm_auto_exposure_cpp(self):
        dimm = read("src/DIMM.cpp")
        ae = read("src/DIMM.AutoExposure.cpp")
        for token in [
            "void DIMM::handleAutoExposureSample",
            "void DIMM::resetAutoExposureState()",
            "QString DIMM::autoExposureStateName",
            "QString DIMM::autoExposureStateShortText",
            "QString DIMM::autoExposureUiStatusText()",
        ]:
            self.assertIn(token, ae)
            self.assertNotIn(token, dimm)

    def test_all_remaining_splits_are_registered_in_cmake(self):
        cmake = read("CMakeLists.txt")
        for rel in [
            "src/DIMM.Simulation.cpp",
            "src/DIMM.Results.cpp",
            "src/DIMM.CommCamera.cpp",
            "src/DIMM.AutoExposure.cpp",
        ]:
            self.assertIn(rel, cmake)

if __name__ == "__main__":
    unittest.main()
