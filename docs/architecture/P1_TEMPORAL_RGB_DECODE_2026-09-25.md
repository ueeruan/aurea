# Temporal RGB decode churn — 2110 app export follow-up

The real Android app export of font-export-current.aurea failed after delivering
56 encoded frames: timeout waiting for exact video samples, with repeated codec
flushes. The saved project contains TimeWarpRGB (FNV type0x2c2c4258), R+3/G0/B-3
composition-frame offsets, 100% amount, full mix. It is not Glitchify.
The source has63 frames in3seconds: five long VFR intervals followed by58 short
ones. See font-export-failure.log and preview-vfr-source.mp4 under android-p0.

Two concrete decode inefficiencies were corrected:
- Precise-timing streams can advance to any later PTS; nominal half-frame
  tolerance must not force a seek when an upcoming VFR interval is shorter.
- TimeWarpRGB Still requests retain bounded neighboring frames already decoded
  during preroll. Previously those pixels were discarded, then decoded again
  from the GOP when the next export frame needed them. Other Still requests keep
  their original single-target behavior. Existing cache/buffer caps are unchanged.

Host focused results:
- TemporalStillWindowReusesGopFramesThroughEnd:442 checks, 63 frames, one seek,
  exact base/forward/backward samples, bounded cache.
- PreciseShortIntervalsAdvanceWithoutNominalHalfFrameSeek:7 checks.
- VariableRateSeekWaitsForPresentationInterval:10 checks.
- StillRequestDeliversTheExactFrame:5 checks (old behavior preserved).
- TimeWarpRgb GPU cases:2 tests/16checks.
All passed. These are algorithm/renderer regressions, not proof that the reported
real MediaCodec export is fixed. The unchanged timeout still rejects missing
frames, and the app export must be rerun with the saved project before closure.

## Actual app retest — passed

The same saved project1000000022 was exported by the updated Android app with
TimeWarpRGB unchanged, H.264720p, AI off. Before export, the decoder was exercised
by seeking to the end and back to2.09s. Export delivered **63/63 frames** and
published MediaStore item1000000049, `1000000022 20260925-204353.mp4`.

Pulled output: `engine/build/android-p0/font-temporal-export-720p.mp4`,
1,365,241bytes. FFmpeg decoded all63 frames at1280x720,21fps,3.00s, exit0;
`font-temporal-export-decode.log`. A decoded frame showed legible fallback text.
`temporal-warm-export.log` records decode67.9/render6.6/readback0.6/encoder15.4
ms per frame for this run. These are emulator measurements, not a physical-phone
benchmark or a controlled before/after speed comparison: the earlier failure
occurred while another build was running. No timeout was increased, effect
removed or missing frame silently accepted. This reproduction now passes;
broader long-GOP/media/device stress coverage remains separate.
