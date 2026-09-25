# Text animation creation — 2026-09-25

Add animation previously created opacity 100 with no keys: valid data but no
visible animation. The default now creates a selector-driven opacity reveal
over approximately one second from the current playhead, clamped to the clip.
Earlier frames remain visible. Presets also start at the playhead when inside
the clip. Android refreshes new keys immediately; the iOS bridge now propagates
failure and its UI displays errors. UTF-8 typewriter timing counts codepoints
instead of bytes and handles multiple characters falling on the same frame.

GPU regression covers visible beginning/middle/end, earlier-frame preservation,
reverse seeking and save/reopen (36 checks). Text unit suite passed (82 checks).
The final host and native GLES suites both pass with these regressions included.
Actual Android Add animation produced Animação 1 and real keyframe diamonds;
next-key navigation changed hidden text at 0s to visible text at 1s. Evidence:
engine/build/android-p0/text-animation-created.xml and text-animation-start.png /
text-animation-end.png. The project uses a low-resolution test video, so these
screenshots establish interaction rather than final text quality.

Native iOS execution and encoded-file text export were not newly verified in
this checkpoint. Character offset/random order and subframe Hold behavior are
separate remaining text-animation concerns; this change does not claim them.
