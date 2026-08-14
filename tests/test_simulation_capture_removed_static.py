from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class SimulationCaptureRemovedStaticTest(unittest.TestCase):
    def test_simulation_translation_unit_and_cmake_registration_are_gone(self):
        self.assertFalse((ROOT / "src/DIMM.Simulation.cpp").exists())
        cmake = read("CMakeLists.txt")
        self.assertNotIn("src/DIMM.Simulation.cpp", cmake)

    def test_capture_state_and_lifecycle_have_no_simulation_path(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        ui = read("src/DIMM.Ui.cpp")
        for text in [header, cpp, ui]:
            for token in [
                "CaptureState::Simulation",
                "onStartSimulation",
                "onUpdateSimulation",
                "startSimulationCapture",
                "stopSimulationCapture",
                "m_simulationTimer",
                "m_actionStartSimulation",
                "m_simulationRuntime",
            ]:
                self.assertNotIn(token, text)

    def test_active_focuser_message_has_no_simulation_wording(self):
        focuser = read("src/EafFocuserManager.cpp")
        self.assertNotIn("capture, simulation, or alignment", focuser)


if __name__ == "__main__":
    unittest.main()
