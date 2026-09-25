# Presentation-order regression fixture

`preview-bframes.mp4` is generated test media, not user content. It contains
90 moving test-pattern frames (160×90, 30 fps, 3 seconds), H.264 Main, YUV420p,
no audio. The encoding log reports 2 I-frames, 23 P-frames and 65 B-frames.

Generated with FFmpeg 7.1 / libx264 (development tool only, not shipped in Aurea):

```
ffmpeg -f lavfi -i testsrc2=size=160x90:rate=30:duration=3 -c:v libx264 -profile:v main -pix_fmt yuv420p -g 60 -bf 3 -x264-params b-adapt=0:scenecut=0 -an -movflags +faststart preview-bframes.mp4
```

The iOS `export-render` parity scene compares all production-decoder pixels
and presentation timestamps against an independent AVAssetReader, in both
CPU and IOSurface modes. It also checks EOS, seeks, retained frame ownership,
and suspend/resume. This fixture can be imported unchanged on Android.
