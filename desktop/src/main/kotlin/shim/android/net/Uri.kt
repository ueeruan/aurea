package android.net

import java.io.File

/**
 * `Uri` do Android sobre `java.net.URI`. No PC os arquivos do usuário chegam
 * como `file:///C:/...` e os links como `https://...`.
 */
class Uri private constructor(private val text: String) : Comparable<Uri>, android.os.Parcelable, java.io.Serializable {
    @Transient private var parsed: java.net.URI? = null
    private val uri: java.net.URI?
        get() = parsed ?: runCatching { java.net.URI(text) }.getOrElse {
            runCatching { java.net.URI(text.replace(" ", "%20").replace("\\", "/")) }.getOrNull()
        }?.also { parsed = it }

    val scheme: String? get() = uri?.scheme ?: text.substringBefore(':', "").ifEmpty { null }
    val path: String? get() = when (scheme) {
        "file" -> uri?.let { u -> runCatching { File(u).path }.getOrNull() ?: u.path }
        else -> uri?.path
    }
    val encodedPath: String? get() = uri?.rawPath
    val host: String? get() = uri?.host
    val port: Int get() = uri?.port ?: -1
    val authority: String? get() = uri?.authority
    val encodedAuthority: String? get() = uri?.rawAuthority
    val query: String? get() = uri?.query
    val encodedQuery: String? get() = uri?.rawQuery
    val fragment: String? get() = uri?.fragment
    val schemeSpecificPart: String? get() = uri?.schemeSpecificPart
    val userInfo: String? get() = uri?.userInfo
    val isAbsolute: Boolean get() = scheme != null
    val isRelative: Boolean get() = !isAbsolute
    val isHierarchical: Boolean get() = uri?.isOpaque == false
    val isOpaque: Boolean get() = uri?.isOpaque == true
    val pathSegments: List<String> get() = (path ?: "").split('/', '\\').filter { it.isNotEmpty() }
    val lastPathSegment: String? get() = pathSegments.lastOrNull()

    fun getQueryParameter(key: String): String? = query?.split('&')?.firstNotNullOfOrNull { p ->
        val k = p.substringBefore('=')
        if (k == key) java.net.URLDecoder.decode(p.substringAfter('=', ""), Charsets.UTF_8) else null
    }
    fun getQueryParameters(key: String): List<String> = query?.split('&')?.mapNotNull { p ->
        if (p.substringBefore('=') == key) java.net.URLDecoder.decode(p.substringAfter('=', ""), Charsets.UTF_8) else null
    }.orEmpty()
    fun getBooleanQueryParameter(key: String, def: Boolean): Boolean = getQueryParameter(key)?.let { it != "false" && it != "0" } ?: def
    val queryParameterNames: Set<String> get() = query?.split('&')?.map { it.substringBefore('=') }?.toSet().orEmpty()

    fun buildUpon(): Builder = Builder().also { b ->
        b.scheme(scheme); b.encodedAuthority(authority); b.path(uri?.path); b.encodedQuery(encodedQuery); b.fragment(fragment)
    }
    fun normalizeScheme(): Uri = this

    override fun toString(): String = text
    override fun equals(other: Any?): Boolean = other is Uri && other.text == text
    override fun hashCode(): Int = text.hashCode()
    override fun compareTo(other: Uri): Int = text.compareTo(other.text)

    class Builder {
        private var scheme: String? = null
        private var authority: String? = null
        private var path: String? = null
        private val query = StringBuilder()
        private var fragment: String? = null
        fun scheme(s: String?) = apply { scheme = s }
        fun authority(a: String?) = apply { authority = a }
        fun encodedAuthority(a: String?) = apply { authority = a }
        fun path(p: String?) = apply { path = p }
        fun encodedPath(p: String?) = apply { path = p }
        fun appendPath(seg: String) = apply { path = (path ?: "").trimEnd('/') + "/" + encode(seg) }
        fun appendEncodedPath(seg: String) = apply { path = (path ?: "").trimEnd('/') + "/" + seg }
        fun appendQueryParameter(k: String, v: String?) = apply {
            if (query.isNotEmpty()) query.append('&')
            query.append(encode(k)).append('=').append(encode(v ?: ""))
        }
        fun encodedQuery(q: String?) = apply { query.setLength(0); if (q != null) query.append(q) }
        fun query(q: String?) = encodedQuery(q)
        fun fragment(f: String?) = apply { fragment = f }
        fun clearQuery() = apply { query.setLength(0) }
        fun build(): Uri {
            val sb = StringBuilder()
            if (scheme != null) sb.append(scheme).append(':')
            if (authority != null) sb.append("//").append(authority)
            if (path != null) { if (authority != null && !path!!.startsWith('/')) sb.append('/'); sb.append(path) }
            if (query.isNotEmpty()) sb.append('?').append(query)
            if (fragment != null) sb.append('#').append(fragment)
            return Uri(sb.toString())
        }
    }

    companion object {
        @JvmField val EMPTY: Uri = Uri("")
        @JvmStatic fun parse(s: String): Uri = Uri(s)
        @JvmStatic fun fromFile(f: File): Uri = Uri(f.absoluteFile.toURI().toString())
        @JvmStatic fun fromParts(scheme: String, ssp: String, fragment: String?): Uri =
            Uri("$scheme:$ssp" + (fragment?.let { "#$it" } ?: ""))
        @JvmStatic fun withAppendedPath(base: Uri, pathSegment: String): Uri = base.buildUpon().appendEncodedPath(pathSegment).build()
        @JvmStatic fun encode(s: String?): String = s?.let { java.net.URLEncoder.encode(it, Charsets.UTF_8).replace("+", "%20") } ?: ""
        @JvmStatic fun encode(s: String?, allow: String?): String {
            if (s == null) return ""
            var e = encode(s)
            allow?.forEach { c -> e = e.replace(java.net.URLEncoder.encode(c.toString(), Charsets.UTF_8), c.toString()) }
            return e
        }
        @JvmStatic fun decode(s: String?): String = s?.let { java.net.URLDecoder.decode(it.replace("+", "%2B"), Charsets.UTF_8) } ?: ""
    }
}

object ConnectivityManager
