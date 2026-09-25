# Font fallback for script-only selected fonts

The Samsung screenshot shows SECGujarati selected while a Latin caption renders
.notdef boxes. The existing shared text shaper does fall back on missing glyphs,
but its general fallback list contains Windows fonts, Arial Unicode on iOS and
legacy DroidSansFallback. It does not consult the configured default font. On
modern Android, those paths can all be absent even though Roboto works normally.

Text.cpp now checks the configured default font (the same Engine-selected font
used for ordinary text) after the authored font fails and before script-specific
reserves. Authored/imported fonts keep priority whenever they cover the cluster.
The temporary layout owns a shared reference to the default face until all raw
font pointers have finished raster/outline generation.

Font selection covers a base and combining marks, joiners/variation selectors,
emoji modifiers/tag characters and regional-indicator pairs together. Missing
marks are tested by HarfBuzz so a valid precomposed form remains in the authored
font; a mark missing from that face can move the entire cluster to one fallback.
This does not introduce color-emoji rendering or invent absent emoji glyphs.

Validation:
- Baseline regression: real Windows MDL2 glyph font lacks Latin; configured
  Calibri fallback ignored, 7 failures / 24 checks (glyph IDs and metrics).
- Fixed regression: 35 checks PASS, including accented Latin, decomposed accent,
  actual authored U+E700, ZWJ HarfBuzz identity and a present Impact base with a
  missing Hebrew combining mark.
- Final Text/Text3D/GPU-filtered suite: 58 tests / 77,998 checks / 0 failures.
  Existing steady-state text atlas test still reports zero uploads/rasterizations
  over 45 animated frames; Cyrillic GPU pixels also pass.
- Logs: engine/build/host/font-fallback-baseline-test.log,
  font-cluster-final-test.log, text-fallback-final-suite.log.

No Android/iOS bridge or font-catalog filtering changed. Actual Samsung device
retesting remains pending; the original selected font is retained in the project.
