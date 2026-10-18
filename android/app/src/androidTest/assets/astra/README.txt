legacy44.aurea is an AUREA-owned regression fixture with one colored rectangle,
640x360/30 fps, 12 frames, no external assets, one composition and no markers.
The generator encodes the current Project, removes the two layer-tail flags
added in timeline versions45/46, adjusts the timeline section length/CRC and
verifies the native loader reports timeline44 and olderFormat=true while
preserving the rectangle, dimensions, color and duration.

Generator and build/reload logs:
build/validation/astra-export-20261013/make-legacy-fixture.py
build/validation/astra-export-20261013/legacy44-generate.log

This tests the earlier file layout. It is not a user-supplied project and does
not establish behavior for every older project with missing external media.
