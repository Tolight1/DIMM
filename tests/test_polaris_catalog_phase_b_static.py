from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class PolarisCatalogPhaseBStaticTest(unittest.TestCase):
    def test_catalog_contract_and_runtime_are_embedded(self):
        header = read("src/PolarisCatalog.h")
        cpp = read("src/PolarisCatalog.cpp")
        data = read("src/PolarisCatalogData.h")

        self.assertIn("struct CatalogStar", header)
        self.assertIn("class PolarisCatalog", header)
        self.assertIn("static const QVector<CatalogStar>& stars()", header)
        self.assertIn("static const CatalogStar* polaris()", header)
        self.assertIn("sourceId", header)
        self.assertIn("raDeg", header)
        self.assertIn("decDeg", header)
        self.assertIn("catalogEpochYear", header)
        self.assertIn("properMotionRaMasYr", header)
        self.assertIn("isPolaris", header)
        self.assertNotIn("raDegJ2000", header)
        self.assertNotIn("decDegJ2000", header)

        self.assertIn("kPolarisCatalogStars", data)
        self.assertIn("11767ULL", data)
        self.assertIn("Polaris", data)
        self.assertIn("validateCatalog", cpp)
        self.assertNotIn("QFile", cpp)
        self.assertNotIn("QNetwork", cpp)

    def test_generated_catalog_has_expected_provenance_and_shape(self):
        csv_text = read("resources/catalog/polaris_field.csv")
        readme = read("resources/catalog/README.md")
        script = read("tools/build_polaris_catalog.py")

        self.assertIn("Hipparcos Main Catalogue", readme)
        self.assertIn("CDS VizieR", readme)
        self.assertIn("I/239/hip_main", readme)
        self.assertIn("dec > 86", readme)
        self.assertIn("Hpmag <= 9.5", readme)
        self.assertIn("2026-07-21", readme)
        self.assertIn("urllib.request", script)
        self.assertIn("PolarisCatalogData.h", script)

        rows = [line for line in csv_text.splitlines() if line and not line.startswith("#")]
        self.assertGreaterEqual(len(rows), 80)
        self.assertEqual(rows[0], "source_id,name,ra_deg_j1991_25,dec_deg_j1991_25,pmra_mas_yr,pmdec_mas_yr,mag,is_polaris")
        self.assertRegex(csv_text, r"(?m)^11767,Polaris,37\.94614689,89\.26413805,44\.22,-11\.74,2\.1077,true$")


if __name__ == "__main__":
    unittest.main()
