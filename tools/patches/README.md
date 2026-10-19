The MediaCodec safety patch applies to FFmpeg 8.0.3's LGPL encoder adapter.
It does not change compression algorithms, the decoder or AUREA preview.
`tools/build_ffmpeg.py` checks the exact original and patched source digests,
applies only `libavcodec/mediacodecenc.c`, records the patch in dependency
manifests and includes it alongside the distributed LGPL notice.

`tools/test_ffmpeg_mediacodec_adapter.py` compiles the relevant source functions
against real public FFmpeg frame, packet and image helpers with a controlled
MediaCodec boundary. It reproduces short-buffer writes, ignored queue errors,
uninitialized coded padding, loss of the final packet when EOS accompanies
data, unreleased empty EOS buffers and unchecked output ranges. The test
substitutes Android codec calls; it is not a manufacturer compatibility test.

The patch also replaces the dummy-header probe's encoder flush after EOS with
retirement followed by a fresh codec instance using the same retained
MediaFormat. A separate native
Android probe reproduced lost first packets and premature EOS on
`c2.android.avc.encoder`; controls without the dummy/EOS/flush sequence preserved
all twelve timestamps. Android documents encoder flush behavior as undefined.
Reconfiguring the same codec instance still failed in that test; creating a
fresh instance preserved all twelve packets/timestamps for both NV12 and I420.
The proof applies to that tested encoder and device context, not to every OEM.

The original source is available in the checksum-pinned upstream archive listed
in `tools/build_ffmpeg.py`. FFmpeg's original copyright and LGPL header remain
in the modified file. Full upstream license terms are included with the build.
