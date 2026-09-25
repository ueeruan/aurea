package com.aurea.aurea.ai

import java.io.ByteArrayOutputStream
import java.io.InputStream

internal const val MAX_IMAGE_UPLOAD_BYTES = 8 * 1024 * 1024

/** Stop before a document provider can allocate an arbitrarily large image. */
internal fun readImageUploadBytes(input: InputStream, limit: Int = MAX_IMAGE_UPLOAD_BYTES): ByteArray? {
    val output = ByteArrayOutputStream(minOf(limit, 64 * 1024))
    val buffer = ByteArray(64 * 1024)
    var total = 0
    while (true) {
        val count = input.read(buffer, 0, minOf(buffer.size, limit - total + 1))
        if (count < 0) return output.toByteArray()
        if (count == 0) continue
        total += count
        if (total > limit) return null
        output.write(buffer, 0, count)
    }
}
