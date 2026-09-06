# Thermion source and local changes

Sources: published `thermion_dart 0.5.0` and `thermion_flutter 0.5.0`, downloaded from the pub.dev API on 2026-09-06. Original Apache-2.0 LICENSE files are retained in both directories.

Archive SHA-256:

- thermion_dart: `79d0328a045333a9e7044987c82624ed5a3cc168d03f5663cc59d2f612a83dd4`
- thermion_flutter: `a5890c72caa2116eb174b79c8307744b3c1cb69af946295df22c3c057cb2dc61`

Changes made by Aurea:

1. Both packages use hooks 2.x; native_toolchain_c is pinned to 0.19.3, compatible with code_assets 1.x. hooks 2's ProtocolExtension change is not implemented directly by these packages. This resolves the coexistence constraint with flutter_scene 0.20.
2. Windows import-library paths use `Uri.resolve`/`File.fromUri` so spaces are not interpreted as literal `%20`.
3. Windows compiler response files live in the target build directory, quote source paths and no longer collide between builds in the OS temp directory.
4. Native source/header dependencies are recorded even when compilation uses a response file; otherwise modified C++ could leave a stale DLL in use.
5. Android compileSdk is 36 to meet the AndroidX dependencies of the host app.
6. The Flutter package exports its texture descriptor. Aurea serializes surface allocation, resize, frame rendering and destruction through its viewport queue.
7. Three small native functions execute on Thermion's render thread: configure Filament dynamic resolution, read the last effective scale, and read a valid GPU duration from Filament frame history. Pending/unsupported durations remain unavailable.
8. Aurea preview uses a fourth native function that submits begin/render/end as one render-thread task, checks `beginFrame` backpressure, and never calls `flushAndWait`. It targets only surfaces attached to that view. Upstream `renderSingleFrame` remains available for callers needing its existing behavior.

Filament libraries are fetched by the upstream build hook (v1.69.1 default). The matching headers in `native/include/filament` are supplied by the published Thermion source archive and must be committed: the hook does not download them. Downloaded binary build artifacts are ignored. No shader/runtime binary is generated or patched by hand.

This is a migration branch in the working tree, not an upstream Thermion release. Rebase these patches when upgrading the adapter, and run native builds and hardware tests before changing the production renderer default. Keep source licenses and notices when redistributing.
