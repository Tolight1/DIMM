from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class EnvironmentSensorIntegrationStaticTest(unittest.TestCase):
    def test_wsq_sources_use_local_include_paths_after_migration(self):
        for rel_path in [
            "src/WsqSensor.h",
            "src/WsqSensor.cpp",
            "src/SerialPortWin.h",
            "src/SerialPortWin.cpp",
            "src/ModbusRtu.cpp",
        ]:
            content = read(rel_path)
            self.assertNotIn('"wsq/', content)

    def test_environment_sensor_manager_owns_worker_thread_and_cached_data(self):
        header = read("src/EnvironmentSensorManager.h")
        cpp = read("src/EnvironmentSensorManager.cpp")
        cmake = read("CMakeLists.txt")

        self.assertIn("struct EnvironmentSensorData", header)
        self.assertIn("double temperatureC", header)
        self.assertIn("double humidityRh", header)
        self.assertIn("double pressureHpa", header)
        self.assertIn("QThread* m_workerThread", header)
        self.assertIn("QTimer* m_pollTimer", cpp)
        self.assertIn("m_worker->moveToThread(m_workerThread)", cpp)
        self.assertIn("m_pollTimer->start(m_pollIntervalMs)", cpp)
        self.assertIn("latestData() const", header)
        self.assertIn("dataUpdated(EnvironmentSensorData data)", header)
        self.assertIn("src/EnvironmentSensorManager.h", cmake)
        self.assertIn("src/EnvironmentSensorManager.cpp", cmake)

    def test_dimm_uses_environment_sensor_for_status_ui_reporting_and_results(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        results_cpp = read("src/DIMM.Results.cpp")
        ui = read("src/DIMM.ui")

        self.assertIn('#include "EnvironmentSensorManager.h"', header)
        self.assertIn("EnvironmentSensorManager* m_environmentSensor", header)
        self.assertIn("EnvironmentSensorData m_latestEnvironment", header)
        self.assertIn("m_environmentSensor->start(", cpp)

        update_body = cpp.split("void DIMM::updateCameraInfo()", 1)[1].split(
            "void DIMM::updateCurrentRoi()", 1
        )[0]
        self.assertNotIn("getTemperature", update_body)
        self.assertIn("lblEnvironmentInfo", update_body)
        self.assertIn("m_latestEnvironment.temperatureC", update_body)
        self.assertIn("m_latestEnvironment.humidityRh", update_body)
        self.assertIn("m_latestEnvironment.pressureHpa", update_body)

        report_body = results_cpp.split("void DIMM::reportDeviceStatus()", 1)[1].split(
            "void DIMM::handleAutoExposureSample", 1
        )[0]
        self.assertNotIn("getTemperature", report_body)
        self.assertIn("m_latestEnvironment.valid", report_body)
        self.assertIn("m_latestEnvironment.temperatureC", report_body)

        self.assertIn("lblEnvironmentName", ui)
        self.assertIn("lblEnvironmentStatus", ui)
        self.assertIn("lblEnvironmentInfo", ui)

        result_body = results_cpp.split("void DIMM::initResultFile()", 1)[1].split(
            "void DIMM::closeResultFile()", 1
        )[0]
        save_body = results_cpp.split("void DIMM::saveResultRow", 1)[1].split(
            "void DIMM::flushPendingWrites", 1
        )[0]
        for field_name in [
            "env_temperature_c",
            "env_humidity_rh",
            "env_pressure_hpa",
            "env_sensor_valid",
        ]:
            self.assertIn(field_name, result_body)
        self.assertIn("QString::number(m_latestEnvironment.temperatureC", save_body)
        self.assertIn("QString::number(m_latestEnvironment.humidityRh", save_body)
        self.assertIn("QString::number(m_latestEnvironment.pressureHpa", save_body)
        self.assertIn("m_latestEnvironment.valid ? QStringLiteral(\"1\")", save_body)

    def test_environment_sensor_can_be_configured_from_trigger_settings(self):
        app_config = read("src/AppConfig.h")
        settings_h = read("src/SettingsDialog.h")
        settings_cpp = read("src/SettingsDialog.cpp")
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        config_cpp = read("src/DIMM.Config.cpp")

        self.assertIn("struct EnvironmentSensorConfig", app_config)
        self.assertIn("portName = QStringLiteral(\"COM6\")", app_config)
        self.assertIn("bool enabled = true", app_config)
        self.assertIn("EnvironmentSensorConfig environmentSensor", app_config)

        self.assertIn("onApplyEnvironmentSensor", settings_h)
        self.assertIn("envSensorEnableCheck", settings_h)
        self.assertIn("envSensorPortEdit", settings_h)
        self.assertIn("envSensorBaudCombo", settings_h)
        self.assertIn("envSensorAddressEdit", settings_h)
        self.assertIn("envSensorPollIntervalEdit", settings_h)
        self.assertIn("new QLineEdit(QStringLiteral(\"COM6\"))", settings_cpp)
        self.assertIn("new QLineEdit(QStringLiteral(\"COM9\"))", settings_cpp)

        apply_body = settings_cpp.split("bool SettingsDialog::applySettings()", 1)[1]
        self.assertIn("EnvironmentSensorConfig environmentSensorConfig", apply_body)
        self.assertIn("appConfig.environmentSensor = environmentSensorConfig", apply_body)
        self.assertIn("configCallbacks.applyEnvironmentSensor = onApplyEnvironmentSensor", apply_body)

        self.assertIn("EnvironmentSensorConfig m_environmentSensorConfig", dimm_h)
        self.assertIn("onApplyEnvironmentSensor", config_cpp)
        self.assertIn("m_environmentSensorConfig = config", config_cpp)
        self.assertIn("m_environmentSensor->start(m_environmentSensorConfig)", dimm_cpp)
        self.assertIn("m_environmentSensor->stop()", config_cpp)

    def test_camera_status_cards_use_single_line_compact_text(self):
        cpp = read("src/DIMM.cpp")
        ui_cpp = read("src/DIMM.Ui.cpp")
        ui = read("src/DIMM.ui")
        main_window_body = ui_cpp.split("void DIMM::setupMainWindowUi", 1)[1].split(
            "void DIMM::setupPreviewCanvases", 1
        )[0]
        update_body = cpp.split("void DIMM::updateCameraInfo()", 1)[1].split(
            "void DIMM::updateCurrentRoi()", 1
        )[0]

        self.assertIn("ui->cam1Card->setMinimumHeight", main_window_body)
        self.assertIn("ui->cam2Card->setMinimumHeight", main_window_body)
        self.assertIn("SN:", update_body)
        self.assertNotIn("\\n帧率", update_body)
        self.assertIn("SN:", ui)
        self.assertNotIn("序列号:", ui)


if __name__ == "__main__":
    unittest.main()
