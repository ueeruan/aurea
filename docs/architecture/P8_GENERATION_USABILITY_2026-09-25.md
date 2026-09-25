# Generation usability and bounded image memory — 2026-09-25

Android and iOS now place generation state, progress and the downloaded result
before the long form. Cached video stays accessible when the backend is offline.
Cancel remains near progress; network/backend errors retain their actual cause
instead of falsely claiming generation already started. Upload failure preserves
the previous valid reference; generation waits while a new reference uploads.

Android reads at most 8 MiB + 1 byte from image providers before rejecting an
oversized image. iOS imports a temporary file, checks size before reading and
uses an ImageIO thumbnail capped at 2048 pixels when converting other formats
instead of decoding a full-resolution image. Both show upload errors explicitly.

Validation: gradle-graph-ai-tests.log: Kotlin compilation and 89 JVM tests passed,
including bounded-reader test consuming only limit+1 bytes from an infinite stream.
iOS scope/API/pbxproj/symbol checks passed; they do not compile Swift.
Actual paid generation/network cancellation and native iOS still need execution.
No remote jobs were submitted. This is an incremental UI rebuild, not closure of
all requested generation or AI upscaler functionality.
