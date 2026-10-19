# Synthetic video fixture

`preview-vfr.mp4` is a copy of `engine/tests/data/preview-vfr.mp4`, generated
from FFmpeg's `testsrc2` pattern. It contains no user media or audio.

The source is 160 × 90 at 30 fps for three seconds. Selecting frames 0, 10,
20 and every frame from 30 onwards produces 63 H.264 samples with variable
presentation intervals. The generation commands are documented in
`engine/tests/data/preview-bframes.md`.

`MotionBlurVideoExportTest` imports it through the production decoder and
exports RSMB, transform motion blur, and both effects together. The test also
decodes frames from each resulting MP4 to verify that the files are readable.

`animated-character.fbx` is the small synthetic skinned mesh produced by
`write_skinned_fbx()` in `engine/tests/test_scene3d.cpp`: five vertices, two
animated takes and an embedded 8 × 8 test texture. It contains no user model.

`video-with-tone.mp4` is synthetic too: FFmpeg `testsrc2` at 160 × 90, 30 fps,
three seconds, H.264 baseline, with a 330 Hz sine tone (peak about -2 dBFS) in AAC 64 kbps.
`EditModeUiTest.editClipsShowTheAudioWave` imports it to check the audio
waveform under the video clip in the Editing interface. Command:

    ffmpeg -f lavfi -i "testsrc2=size=160x90:rate=30:duration=3" -f lavfi -i "sine=frequency=330:sample_rate=48000:duration=3,volume=6" -c:v libx264 -pix_fmt yuv420p -profile:v baseline -crf 30 -c:a aac -b:a 64k -shortest -movflags +faststart video-with-tone.mp4
