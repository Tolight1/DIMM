from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AstronomyTransformPhaseCStaticTest(unittest.TestCase):
    def test_astronomy_transform_exposes_safe_polar_math(self):
        header = read("src/AstronomyTransform.h")
        cpp = read("src/AstronomyTransform.cpp")

        self.assertIn("struct CatalogSkyPosition", header)
        self.assertIn("propagateProperMotion(const CatalogStar& star", header)
        self.assertIn("catalogVectorAtEpoch(const CatalogStar& star", header)
        self.assertIn("northPolePlaneFromRaDecDeg(double raDeg, double decDeg)", header)
        self.assertIn("northPolePlaneAtEpoch(const CatalogStar& star", header)
        self.assertIn("angularDistanceDeg(const CatalogStar& a, const CatalogStar& b)", header)
        self.assertIn("QVector3D", header)
        self.assertIn("catalogEpochYear", cpp)
        self.assertIn("properMotionRaMasYr", cpp)
        self.assertIn("properMotionDecMasYr", cpp)
        self.assertIn("qDegreesToRadians", cpp)
        self.assertIn("rho =", cpp)
        self.assertIn("std::acos", cpp)
        self.assertIn("std::clamp", cpp)
        self.assertNotIn("raDeg -", cpp)
        self.assertNotIn("J2000", header)

    def test_solver_includes_catalog_and_transform_foundation(self):
        solver_header = read("src/PolarisSolver.h")
        catalog_cpp = read("src/PolarisCatalog.cpp")

        self.assertIn('#include "PolarisCatalog.h"', solver_header)
        self.assertIn("PolarisCatalog::isValid()", catalog_cpp)


if __name__ == "__main__":
    unittest.main()
