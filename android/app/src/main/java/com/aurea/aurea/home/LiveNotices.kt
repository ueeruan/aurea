package com.aurea.aurea.home

import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
import com.aurea.aurea.R
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URI
import java.net.URL
import java.time.Instant

internal data class LiveNotice(
    val id: String, val text: String, val level: String,
    val link: String?, val expires: Instant?, val popup: Boolean,
)

/** Read-only client for the existing Aurea Cloudflare Worker /aviso contract. */
internal class LiveNotices(context: Context) {
    private val prefs = context.getSharedPreferences("aurea.avisos", Context.MODE_PRIVATE)
    private val dismissed = prefs.getStringSet("dismissed", emptySet()).orEmpty().toMutableSet()
    private val seen = prefs.getStringSet("popupSeen", emptySet()).orEmpty().toMutableSet()
    private var cached = parse(prefs.getString("cache", "[]") ?: "[]")
    private var lastFetch = 0L
    var visible by mutableStateOf<List<LiveNotice>>(emptyList())
        private set
    val popup: LiveNotice? get() = visible.firstOrNull { it.popup && it.id !in seen }

    init { expire() }

    fun expire() {
        val now = Instant.now()
        visible = cached.filter { it.id !in dismissed && (it.expires == null || now.isBefore(it.expires)) }
    }

    fun dismiss(id: String) {
        dismissed.add(id)
        prefs.edit().putStringSet("dismissed", dismissed.takeLastSet(100)).apply()
        expire()
    }

    fun closePopup(id: String) {
        seen.add(id)
        prefs.edit().putStringSet("popupSeen", seen.takeLastSet(100)).apply()
        // The set is not Compose state; publish a new state value for the dialog.
        popupRevision++
    }

    var popupRevision by mutableIntStateOf(0)
        private set

    suspend fun refreshIfDue() {
        expire()
        val now = android.os.SystemClock.elapsedRealtime()
        if (lastFetch != 0L && now - lastFetch < 600_000L) return
        lastFetch = now
        val json = withContext(Dispatchers.IO) {
            var connection: HttpURLConnection? = null
            try {
                connection = URL(ENDPOINT).openConnection() as HttpURLConnection
                connection.connectTimeout = 8000
                connection.readTimeout = 8000
                if (connection.responseCode != 200) return@withContext null
                val bytes = connection.inputStream.use { input ->
                    val out = java.io.ByteArrayOutputStream()
                    val buffer = ByteArray(4096)
                    while (out.size() <= 65_536) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        out.write(buffer, 0, n)
                    }
                    out.toByteArray()
                }
                if (bytes.size > 65_536) return@withContext null
                val root = JSONObject(bytes.toString(Charsets.UTF_8))
                (root.optJSONArray("avisos") ?: JSONArray().apply {
                    root.optJSONObject("aviso")?.let { put(it) }
                }).toString()
            } catch (e: CancellationException) { throw e }
            catch (_: Exception) { null }
            finally { connection?.disconnect() }
        } ?: return
        cached = parse(json)
        prefs.edit().putString("cache", json).apply()
        expire()
    }

    companion object {
        const val ENDPOINT = "https://mural-do-aurea.aureaapp.workers.dev/aviso"
        internal fun parse(json: String): List<LiveNotice> = try {
            val array = JSONArray(json)
            (0 until minOf(array.length(), 3)).mapNotNull { index ->
                val item = array.optJSONObject(index) ?: return@mapNotNull null
                val id = item.optString("id").trim()
                val text = item.optString("texto").trim()
                if (id.isEmpty() || text.isEmpty()) return@mapNotNull null
                val link = item.optString("link").takeIf {
                    runCatching { URI(it).let { uri -> uri.scheme in listOf("https", "http") && !uri.host.isNullOrBlank() } }.getOrDefault(false)
                }
                val expiry = item.optString("ate").takeIf { it.isNotBlank() && it != "null" }
                val expires = expiry?.let { runCatching { Instant.parse(it) }.getOrNull() }
                if (expiry != null && expires == null) return@mapNotNull null
                LiveNotice(id, text, item.optString("nivel", "info"), link, expires, item.optBoolean("popup"))
            }
        } catch (_: Exception) { emptyList() }
    }
}

private fun Set<String>.takeLastSet(limit: Int) = toList().takeLast(limit).toSet()

@Composable
internal fun LiveNoticeBanners(service: LiveNotices) {
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val uriHandler = LocalUriHandler.current
    LaunchedEffect(service, lifecycle) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) {
            while (true) { service.refreshIfDue(); delay(60_000) }
        }
    }
    val openLink: (String) -> Unit = { runCatching { uriHandler.openUri(it) } }
    Column(Modifier.fillMaxWidth()) {
        service.visible.forEach { notice ->
            Surface(color = when (notice.level) {
                "problema" -> MaterialTheme.colorScheme.errorContainer
                "atencao" -> MaterialTheme.colorScheme.tertiaryContainer
                else -> MaterialTheme.colorScheme.secondaryContainer
            }) {
                Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)) {
                    Text(notice.text, style = MaterialTheme.typography.bodySmall)
                    Row {
                        notice.link?.let { link -> TextButton(onClick = { openLink(link) }) { Text(stringResource(R.string.notice_more)) } }
                        TextButton(onClick = { service.dismiss(notice.id) }) { Text(stringResource(R.string.notice_dismiss)) }
                    }
                }
            }
        }
    }
    service.popupRevision // Observe dismissal independently of banner contents.
    service.popup?.let { notice ->
        AlertDialog(
            onDismissRequest = { service.closePopup(notice.id) },
            title = { Text(stringResource(R.string.notice_title)) },
            text = { Text(notice.text) },
            confirmButton = { TextButton(onClick = { service.closePopup(notice.id) }) { Text(stringResource(R.string.notice_ok)) } },
            dismissButton = { notice.link?.let { link -> TextButton(onClick = { service.closePopup(notice.id); openLink(link) }) { Text(stringResource(R.string.notice_more)) } } },
        )
    }
}
