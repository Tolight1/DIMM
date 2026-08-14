from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class ResultWriterStaticTest(unittest.TestCase):
    def test_dimm_delegates_result_io_to_result_writer(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.Results.cpp")
        init_body = cpp.split("void DIMM::initResultFile()", 1)[1].split(
            "void DIMM::closeResultFile()", 1
        )[0]
        close_body = cpp.split("void DIMM::closeResultFile()", 1)[1].split(
            "void DIMM::saveResultRow", 1
        )[0]
        save_body = cpp.split("void DIMM::saveResultRow", 1)[1].split(
            "void DIMM::flushPendingWrites", 1
        )[0]
        flush_body = cpp.split("void DIMM::flushPendingWrites", 1)[1].split(
            "void DIMM::reportMeasurement", 1
        )[0]

        self.assertIn('#include "ResultWriter.h"', header)
        self.assertIn("ResultWriter m_resultWriter", header)
        self.assertNotIn("QFile* m_resultFile", header)
        self.assertNotIn("QTextStream* m_resultStream", header)
        self.assertNotIn("QStringList m_pendingWrites", header)
        self.assertIn("ResultFileConfig config", init_body)
        self.assertIn("m_resultWriter.open(config", init_body)
        self.assertIn("m_resultWriter.close()", close_body)
        self.assertIn("m_resultWriter.enqueue(MeasurementRecord{fields})", save_body)
        self.assertIn("m_resultWriter.flush()", flush_body)

    def test_result_writer_owns_buffered_file_lifecycle(self):
        header = read("src/ResultWriter.h")
        cpp = read("src/ResultWriter.cpp")

        self.assertIn("QFile m_file", header)
        self.assertIn("QTextStream m_stream", header)
        self.assertIn("QStringList m_pendingLines", header)
        self.assertIn("m_stream << config.headerLine", cpp)
        self.assertIn("m_pendingLines.append(line)", cpp)
        self.assertIn("m_stream.flush()", cpp)
        self.assertIn("m_file.close()", cpp)


if __name__ == "__main__":
    unittest.main()
