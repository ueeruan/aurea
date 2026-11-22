package com.aurea.aurea.engine

// A JNI jlong holds all pointer bits. ARM64 Android may set the sign bit in
// the heap pointer's tag. Keep that tag intact and reserve only zero.
internal inline fun initializeNativeContext(handle: Long, initialize: (Long) -> Boolean): Boolean =
    handle != 0L && initialize(handle)
