# Thumbnail cache and codec lifecycle

Full memory trim previously emptied only the queue/cache. An already-running
decode could immediately insert its result again, and its platform codec stayed
open until the idle timeout. Project clear invalidated the result but did not
wake the worker to close its codecs. Both operations now invalidate in-flight
work and wake the background worker to retire codecs as soon as the active
platform operation returns. The calling UI/memory-pressure thread does not join
or destroy codecs. A platform decode already in progress is not forcibly killed.

Completion checks the request generation before changing pending requests,
failure suppression, cache accounting or the thumbnail-ready generation. This
also prevents an old project request from modifying the replacement project's
request state when asset identifiers are reused.

Relinking changed thumbnail cache keys but previously reused the decoder solely
by asset identifier. This could cache the old video's pixels under the new
source's key. Decoder reuse and failed-open suppression now include the source
identity. Replacement closes the stale codec on the thumbnail worker. Admission
evicts before opening another codec, so the two-session limit no longer briefly
requires a third hardware decoder during timeline scrolling.

Two deterministic shared-core regressions exercise blocked in-flight decoding,
clear/full-trim accounting, prompt asynchronous retirement, relinked landscape
and portrait output, recovery from a missing source and peak live codec count.
These are lifecycle correctness tests, not measurements on physical devices.
Consolidated host regression passed: 681 tests, 4,210,839 checks, zero failures
in 226.14 seconds (`engine/build/host/beta-final-ctest.log`). The targeted
thumbnail run passed 11 tests and 137 checks. Android arm64/x86_64 builds and
unit tests passed. Native iOS execution remains required. The implementation is shared by Android
and iOS and does not change preview/export frame selection.
