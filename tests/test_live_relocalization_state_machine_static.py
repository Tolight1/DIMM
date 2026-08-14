from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class LiveRelocalizationStateMachineStaticTest(unittest.TestCase):
    def test_relocalization_resets_frame_gates_when_switching_back_to_full_frame(self):
        source = read("src/DIMM.LiveRoi.cpp")
        request_body = source.split("void DIMM::requestLiveFullFrameRelocalization", 1)[1].split(
            "void DIMM::handleLiveRoiCentroidLoss",
            1,
        )[0]
        full_frame_body = source.split("bool DIMM::applyLiveFullFrameForRelocalization", 1)[1].split(
            "bool DIMM::selectLiveRelocalizationCentroid",
            1,
        )[0]

        self.assertIn("resetLiveFrameAcceptanceGates()", source)
        self.assertIn("advanceLiveAcquisitionGeneration()", source)
        self.assertIn("resetLiveFrameAcceptanceGates();", request_body)
        self.assertIn("advanceLiveAcquisitionGeneration();", full_frame_body)

    def test_watchdog_retries_hardware_full_frame_switch_not_only_state_cleanup(self):
        source = read("src/DIMM.LiveRoi.cpp")
        watchdog_body = source.split("void DIMM::handleLiveRelocalizationWatchdog", 1)[1].split(
            "void DIMM::updateFullFrameRoiOverlay",
            1,
        )[0]

        self.assertIn("applyLiveFullFrameForRelocalization(&switchReason)", watchdog_body)
        self.assertLess(watchdog_body.find("clearPendingLiveRelocalizationRois()"),
                        watchdog_body.find("applyLiveFullFrameForRelocalization(&switchReason)"))
        self.assertIn("resetLiveFrameAcceptanceGates();", watchdog_body)

    def test_runtime_relocalization_does_not_use_alignment_polaris_as_preference(self):
        source = read("src/DIMM.LiveRoi.cpp")
        relocalization_body = source.split("bool DIMM::selectLiveRelocalizationCentroid", 1)[1].split(
            "bool DIMM::maybeSeedRoiFromFrame",
            1,
        )[0]

        self.assertIn("runtime.hasLastTargetPosition[cameraIndex]", relocalization_body)
        self.assertNotIn("hasConfirmedPolarisPosition", relocalization_body)
        self.assertNotIn("confirmedPolarisPosition", relocalization_body)

    def test_live_relocalization_uses_single_detector_path_without_attempt_counter_limit(self):
        header = read("src/DIMM.h")
        source = read("src/DIMM.LiveRoi.cpp")
        relocalization_body = source.split("bool DIMM::selectLiveRelocalizationCentroid", 1)[1].split(
            "bool DIMM::maybeSeedRoiFromFrame",
            1,
        )[0]
        seed_body = source.split("bool DIMM::maybeSeedRoiFromFrame", 1)[1].split(
            "void DIMM::handleLiveRelocalizationWatchdog",
            1,
        )[0]

        self.assertNotIn("kLiveRelocalizationMaxFullFrameAttempts", source)
        self.assertNotIn("liveRelocalizationFullFrameAttempts", header)
        self.assertIn("detectInitialStarCandidates", relocalization_body)
        self.assertNotIn("detectRawInitialStarPeakCandidate(fullFrame", relocalization_body)
        self.assertNotIn("detectInitialStarCentroid(", relocalization_body)
        self.assertNotIn("detectInitialStarCentroidFast", relocalization_body)
        self.assertNotIn("已检查", seed_body)


if __name__ == "__main__":
    unittest.main()
