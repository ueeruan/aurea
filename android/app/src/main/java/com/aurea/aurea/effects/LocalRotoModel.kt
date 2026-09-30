package com.aurea.aurea.effects

import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

/** Downloaded weights only. User images and video never leave the device. */
object LocalRotoModel {
    private const val BASE = "https://raw.githubusercontent.com/ueeruan/aurea/59f95b16a89d3b4c1e41d945507661440e6d36ac/engine/assets/rotobrush/"
    private val files = listOf(
        Triple("u2netp.param", 25100L, "ab9567c0bfebecf51e8bb1292e2b4ca1bfb09006122a01bbba38a429ca94a4b0"),
        Triple("u2netp.bin", 2257156L, "e9cbc6a779f02490ac16ef87e6b800d3b8dc29fe8cd65226318c089c5d21fd2e"),
        Triple("LICENSE-U2Net.txt", 11357L, "c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4"),
    )
    private fun digest(file: File): String {
        val hash = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input -> val b = ByteArray(65536); while (true) { val n=input.read(b); if(n<0)break; hash.update(b,0,n) } }
        return hash.digest().joinToString("") { "%02x".format(it) }
    }
    suspend fun prepare(directory: String, progress: (Int) -> Unit) {
        require(directory.isNotBlank())
        val dir=File(directory).apply { mkdirs() }
        val total=files.sumOf { it.second };var completed=0L
        for ((name,size,sha) in files) {
            currentCoroutineContext().ensureActive()
            val target=File(dir,name)
            if(target.length()==size && digest(target)==sha){completed+=size;continue}
            if(dir.usableSpace<size*2)throw IOException("storage")
            val partial=File.createTempFile(name,".part",dir)
            val connection=URL(BASE+name).openConnection() as HttpURLConnection
            connection.connectTimeout=20000;connection.readTimeout=30000
            try {
                if(connection.responseCode!=200)throw IOException("download")
                var received=0L
                connection.inputStream.use { input -> partial.outputStream().use { output ->
                    val buffer=ByteArray(65536)
                    while(true){currentCoroutineContext().ensureActive();val n=input.read(buffer);if(n<0)break
                        received+=n;if(received>size)throw IOException("size")
                        output.write(buffer,0,n);progress(((completed+received)*100/total).toInt())
                    }
                } }
                if(partial.length()!=size||digest(partial)!=sha)throw IOException("checksum")
                java.nio.file.Files.move(partial.toPath(),target.toPath(),java.nio.file.StandardCopyOption.REPLACE_EXISTING)
                completed+=size
            } finally {connection.disconnect();partial.delete()}
        }
        progress(100)
    }
}
