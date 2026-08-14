from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ParameterValidationExportStaticTest(unittest.TestCase):
    def test_atmospheric_params_include_second_variances_and_component_r0(self):
        header = read("src/ImageProcessor.h")
        cpp = read("src/ImageProcessor.cpp")
        atmospheric_params = header.split("struct AtmosphericParams", 1)[1].split("};", 1)[0]

        self.assertIn("double longitudinalVariancePx2", atmospheric_params)
        self.assertIn("double transverseVariancePx2", atmospheric_params)
        self.assertIn("double longitudinalVarianceRad2", atmospheric_params)
        self.assertIn("double transverseVarianceRad2", atmospheric_params)
        self.assertIn("double r0LongitudinalCm", atmospheric_params)
        self.assertIn("double r0TransverseCm", atmospheric_params)
        self.assertIn("quint64 sampleCount", atmospheric_params)

        self.assertIn("params.longitudinalVariancePx2 = varLongitudinalPx", cpp)
        self.assertIn("params.transverseVariancePx2 = varTransversePx", cpp)
        self.assertIn("params.longitudinalVarianceRad2 = sigmaLongitudinal2", cpp)
        self.assertIn("params.transverseVarianceRad2 = sigmaTransverse2", cpp)
        self.assertIn("params.r0LongitudinalCm = r0Longitudinal * 100.0", cpp)
        self.assertIn("params.r0TransverseCm = r0Transverse * 100.0", cpp)
        self.assertIn("params.sampleCount = static_cast<quint64>(samples.size())", cpp)

    def test_paired_centroid_detail_is_emitted_from_worker(self):
        header = read("src/ImageProcessor.h")
        cpp = read("src/ImageProcessor.cpp")

        differential_sample = header.split("struct DifferentialSample", 1)[1].split("};", 1)[0]
        self.assertIn("quint64 frameId1", differential_sample)
        self.assertIn("quint64 frameId2", differential_sample)
        self.assertIn("quint64 cameraTimestamp1", differential_sample)
        self.assertIn("quint64 cameraTimestamp2", differential_sample)
        self.assertIn("double syncResidualUs", differential_sample)

        self.assertIn("void differentialSampleDetailReady", header)
        self.assertIn("sample.frameId1 = cam0.frameId", cpp)
        self.assertIn("sample.frameId2 = cam1.frameId", cpp)
        self.assertIn("sample.cameraTimestamp1 = cam0.cameraTimestamp", cpp)
        self.assertIn("sample.cameraTimestamp2 = cam1.cameraTimestamp", cpp)
        self.assertIn("sample.syncResidualUs = syncResidualUs", cpp)
        self.assertIn("emit differentialSampleDetailReady(", cpp)

    def test_dimm_saves_main_variance_columns_and_detail_file(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.Results.cpp")

        self.assertIn("struct PairedCentroidDetail", dimm_h)
        self.assertIn("QVector<PairedCentroidDetail> pendingPairedCentroidDetails", dimm_h)
        self.assertIn("ResultWriter m_detailResultWriter", dimm_h)
        self.assertIn("void initDetailResultFile()", dimm_h)
        self.assertIn("void saveDetailResultRows", dimm_h)

        self.assertIn("var_longitudinal_px2,var_transverse_px2", dimm_cpp)
        self.assertIn("sigma_longitudinal_rad2,sigma_transverse_rad2", dimm_cpp)
        self.assertIn("r0_longitudinal_cm,r0_transverse_cm", dimm_cpp)
        self.assertIn("variance_sample_count", dimm_cpp)
        self.assertIn("runtime.latestAtmosphere.longitudinalVariancePx2", dimm_cpp)
        self.assertIn("runtime.latestAtmosphere.transverseVariancePx2", dimm_cpp)
        self.assertIn("runtime.latestAtmosphere.longitudinalVarianceRad2", dimm_cpp)
        self.assertIn("runtime.latestAtmosphere.transverseVarianceRad2", dimm_cpp)
        self.assertIn("runtime.latestAtmosphere.r0LongitudinalCm", dimm_cpp)
        self.assertIn("runtime.latestAtmosphere.r0TransverseCm", dimm_cpp)
        self.assertIn("runtime.latestAtmosphere.sampleCount", dimm_cpp)

        self.assertIn("DIMM_%2_paired_centroids_%3.txt", dimm_cpp)
        self.assertIn("pair_index,cam1_frame_id,cam2_frame_id", dimm_cpp)
        self.assertIn("cam1_centroid_x,cam1_centroid_y,cam2_centroid_x,cam2_centroid_y", dimm_cpp)
        self.assertIn("longitudinal_px,transverse_px,sync_residual_us", dimm_cpp)
        self.assertIn("m_detailResultWriter.enqueue(MeasurementRecord{fields})", dimm_cpp)
        self.assertIn("runtime.pendingPairedCentroidDetails.clear()", dimm_cpp)

        save_body = dimm_cpp.split("void DIMM::saveResultRow(int frame)", 1)[1].split(
            "void DIMM::saveDetailResultRows", 1
        )[0]
        self.assertIn("auto& runtime = activeRuntime()", save_body)
        self.assertNotIn("const auto& runtime = activeRuntime()", save_body)

    def test_parameter_validation_mode_is_exposed_in_storage_settings(self):
        app_config = read("src/AppConfig.h")
        settings_h = read("src/SettingsDialog.h")
        settings_cpp = read("src/SettingsDialog.cpp")
        controller_h = read("src/ConfigApplicationController.h")
        controller_cpp = read("src/ConfigApplicationController.cpp")
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        config_cpp = read("src/DIMM.Config.cpp")

        storage_config = app_config.split("struct StorageConfig", 1)[1].split("};", 1)[0]
        self.assertIn("bool parameterValidationEnabled = false", storage_config)
        self.assertIn("bool syncDiagnosticLoggingEnabled = false", storage_config)

        self.assertIn("QCheckBox* parameterValidationCheck", settings_h)
        self.assertIn("bool parameterValidationEnabled", settings_h)
        self.assertIn("bool syncDiagnosticLoggingEnabled", settings_h)
        self.assertIn("parameterValidationCheck = new QCheckBox", settings_cpp)
        self.assertIn("parameterValidationCheck->setChecked(false)", settings_cpp)
        self.assertIn("parameterValidationCheck->isChecked()", settings_cpp)

        self.assertIn("bool parameterValidationEnabled", controller_h)
        self.assertIn("bool syncDiagnosticLoggingEnabled", controller_h)
        self.assertIn("config.storage.parameterValidationEnabled", controller_cpp)
        self.assertIn("config.storage.syncDiagnosticLoggingEnabled", controller_cpp)

        self.assertIn("bool m_parameterValidationEnabled = false", dimm_h)
        self.assertIn("bool m_syncDiagnosticLoggingEnabled = false", dimm_h)
        self.assertIn("m_parameterValidationEnabled = parameterValidationEnabled", config_cpp)
        self.assertIn("m_syncDiagnosticLoggingEnabled = syncDiagnosticLoggingEnabled", config_cpp)
        self.assertIn("m_settingsDialog->parameterValidationCheck->setChecked(m_parameterValidationEnabled)", dimm_cpp)
        self.assertIn("if (!m_parameterValidationEnabled)", config_cpp)


if __name__ == "__main__":
    unittest.main()
