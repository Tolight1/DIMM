from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class SyncDiagnosticsStaticTest(unittest.TestCase):
    def test_sync_diagnostics_are_exposed_and_saved(self):
        image_processor_h = read("src/ImageProcessor.h")
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.Results.cpp")

        self.assertIn("syncSampleReady", image_processor_h)
        self.assertIn("latestSyncResidualUs", dimm_h)
        self.assertIn("latestSyncJitterUs", dimm_h)
        self.assertIn("averageSyncJitterUs", dimm_h)
        self.assertIn("maxSyncJitterUs", dimm_h)
        self.assertIn("sync_residual_us", dimm_cpp)
        self.assertIn("sync_jitter_us", dimm_cpp)
        self.assertIn("sync_jitter_avg_us", dimm_cpp)
        self.assertIn("sync_jitter_max_us", dimm_cpp)
        self.assertNotIn("sync_delta_raw_us", dimm_cpp)
        self.assertNotIn("sync_delta_offset_us", dimm_cpp)
        self.assertNotIn("syncOffsetSampleCount", dimm_cpp)
        self.assertNotIn("syncOffsetUs", dimm_cpp)


if __name__ == "__main__":
    unittest.main()
