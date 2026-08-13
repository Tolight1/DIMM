# Polaris Field Catalog

This directory contains the generated development artifact used to build
the embedded Polaris alignment catalog.

- Data source: Hipparcos Main Catalogue, ESA 1997.
- Access service: CDS VizieR.
- VizieR table: I/239/hip_main.
- Coordinate system: ICRS, epoch J1991.25 as published by Hipparcos.
- Embedded epoch field: catalogEpochYear = 1991.25 for every row.
- Query region: dec > 86 degrees.
- Magnitude limit: Hpmag <= 9.5.
- Generated date: 2026-07-21.
- Generated row count: 97.
- Generation command: python tools/build_polaris_catalog.py

The production application does not query VizieR at runtime. The C++
runtime uses src/PolarisCatalogData.h, generated from the CSV in this
directory.

Source landing page: https://cdsarc.cds.unistra.fr/viz-bin/cat/I/239
VizieR query endpoint: https://vizier.cds.unistra.fr/viz-bin/asu-tsv
