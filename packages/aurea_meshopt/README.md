# meshoptimizer integration

Upstream: https://github.com/zeux/meshoptimizer/tree/v1.1
Version: v1.1. License: MIT (LICENSE). Upstream source files are unmodified;
`meshoptimizer.h`, `allocator.cpp` and `vcacheoptimizer.cpp` match the tag
byte for byte, and the other sources come from the same tag.

Compiled sources: allocator, vcacheoptimizer, vfetchoptimizer, indexgenerator,
simplifier, vertexcodec, indexcodec and vertexfilter. The Dart build hook
compiles them for the target platform, including Android, iOS and the test
host.

The wrapper (`src/aurea_meshopt.cpp`) exports checked C ABI operations. Every
operation validates indices and sizes, works on a copy, and returns 0 on
failure, so a native error never leaves a half-modified mesh:

| Dart | Native | Used for |
| --- | --- | --- |
| `optimizeVertexCache` | `aurea_meshopt_cache` | triangle order for the GPU vertex cache (opaque only) |
| `weldVertices` | `aurea_meshopt_weld` | merge vertices equal in every attribute stream |
| `optimizeVertexFetch` | `aurea_meshopt_fetch` | renumber vertices in first-use order |
| `simplifyMesh` | `aurea_meshopt_simplify` | level-of-detail indices over the same vertices |
| `decodeMeshopt` | `aurea_meshopt_decode` | glTF `EXT_meshopt_compression` / `KHR_meshopt_compression` |
| `encodeMeshoptVertices`, `encodeMeshoptTriangles` | `aurea_meshopt_encode_*` | tests and tools |

Imported models are prepared once, inside the import isolate
(`lib/src/features/editor/domain/malha_importada.dart`): weld, vertex cache,
vertex fetch and two simplified levels of detail. Nothing runs in an isolate on
the 3D scene path — opening one there costs ~1.4 s on an iPhone 13.
