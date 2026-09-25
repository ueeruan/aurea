# Autosave session safety

Saving snapshots the project session as well as the edit revision. A queued save
is cancelled if the session changes before encoding. Completion of an older save
does not replace the new project's path, clean flag or recovery state. The
pathless overload resolves its destination inside the same snapshot lock.

Android and iOS idle autosave call the shared `autosave_project` entry point.
The engine drains queued edits and skips disk writes while playing, scrubbing,
inside an undo group, or already clean. Both shells debounce model/playhead
activity for three seconds and back off thirty seconds after failure. iOS also
saves on background transition; autosave disk IO uses a separate queue.

Android no longer rewrites thumbnails and Home sidecars on every idle save;
explicit save/close/background retains that derived-file update path.

Validation: deterministic snapshot/IO interleaving regression (12 checks), idle
playback/scrub/group regression (44 checks), and concurrent save/edit regression
(7 checks) passed on the host. Android arm64 Debug assembly and Kotlin unit tests
passed. iOS API/project static checks passed; native Apple compilation/runtime
remain unverified on the Windows host.
