package com.aurea.aurea.state

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream

/** A document provider must not grow an in-memory import without a bound. */
internal fun InputStream.readBounded(limit: Int): ByteArray {
    require(limit >= 0)
    val output = ByteArrayOutputStream(minOf(limit, 64 * 1024))
    val buffer = ByteArray(16 * 1024)
    var total = 0
    while (true) {
        val count = read(buffer, 0, minOf(buffer.size.toLong(), limit.toLong() - total + 1).toInt())
        if (count < 0) return output.toByteArray()
        if (count == 0) {
            val single = read()
            if (single < 0) return output.toByteArray()
            if (total == limit) throw IOException("Document exceeds import limit")
            output.write(single)
            total++
        } else {
            if (count > limit - total) throw IOException("Document exceeds import limit")
            output.write(buffer, 0, count)
            total += count
        }
    }
}
