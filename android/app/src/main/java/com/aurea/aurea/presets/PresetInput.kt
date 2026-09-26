package com.aurea.aurea.presets

import java.io.ByteArrayOutputStream
import java.io.InputStream

/** The same 64 MiB ceiling as the native converter, enforced before allocation. */
internal fun InputStream.readPreset(maxBytes: Int = 64 * 1024 * 1024): ByteArray {
    val output = ByteArrayOutputStream()
    val chunk = ByteArray(8192)
    while (true) {
        val count = read(chunk)
        if (count < 0) break
        require(count <= maxBytes - output.size()) { "Preset exceeds the import size limit" }
        output.write(chunk, 0, count)
    }
    return output.toByteArray()
}
