# iOS 2110 — ncnn RTTI propagation and VideoSourceFactory link failure

GitHub run36185214397 compiled Swift but failed the arm64 app link:
`typeinfo for aurea::VideoSourceFactory`, referenced by
`typeinfo for aurea::ios::VideoToolboxFactory` in IOSVideoDecoder.mm.o.
Evidence: build/releases/2110/ios-job-108236817668.txt, lines4059 onward.

The pinned ncnn defaults NCNN_DISABLE_RTTI=ON. Its src/CMakeLists.txt lines381–385
publishes -fno-rtti as a PUBLIC usage requirement. Aurea links ncnn PRIVATE to
its static core: the core receives that compiler option, while downstream
ObjC++ bridge compilation does not. VideoSourceFactory has an out-of-line
virtual cache_identity implementation in MediaManager.cpp, so its RTTI belongs
to that core object. The derived bridge emits a reference to RTTI that the core
was inadvertently configured not to emit. The generated Android probe core
commands also contained the propagated -fno-rtti, confirming the scope leak.

Correction: AureaNcnn.cmake explicitly sets NCNN_DISABLE_RTTI=OFF, preserving
the original RTTI behavior across the public core/bridge interfaces. No fake
symbol, duplicate typeinfo, virtual-interface change or linker suppression is
introduced. Inference exception handling remains separately configured.

This is a compile-option correction based on actual native link diagnostics
and the pinned dependency's CMake source. Native Apple recompilation/link is
required to validate the final app; no successful iOS link is claimed yet.
