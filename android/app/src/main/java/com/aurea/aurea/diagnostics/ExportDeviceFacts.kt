package com.aurea.aurea.diagnostics

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import android.os.PowerManager
import android.os.Process
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * 0.5.3 (P1-3): a ficha do aparelho no relato LOCAL do export — o que permite
 * separar os relatos por processador sem ter o aparelho na mão (GPU e driver,
 * SoC em qualquer Android, memória, disco, calor). Fica no arquivo do
 * [ExportDiagnosticReport] e só sai do aparelho quando a pessoa manda "Relatar
 * um problema". Nada de nome de mídia, caminho, projeto ou conta. Chaves curtas.
 */
internal object ExportDeviceFacts {
    private const val TEXT_MAX = 80

    private fun clean(value: String?, max: Int = TEXT_MAX): String = value.orEmpty().trim().take(max)

    /** `ro.*` por reflexão (API escondida): o que der; nunca derruba o relato. */
    private fun systemProperty(name: String): String = runCatching {
        val type = Class.forName("android.os.SystemProperties")
        clean(type.getMethod("get", String::class.java).invoke(null, name) as? String)
    }.getOrDefault("")

    /** A linha `Hardware : …` de /proc/cpuinfo (kernels de 32 bits e Android antigos a trazem). */
    internal fun cpuinfoHardware(text: String): String =
        text.lineSequence().firstOrNull { it.trimStart().startsWith("Hardware", ignoreCase = true) && it.contains(':') }
            ?.substringAfter(':')?.let { clean(it) }.orEmpty()

    private fun readCpuinfoHardware(): String = runCatching {
        File("/proc/cpuinfo").bufferedReader().use { reader ->
            cpuinfoHardware(generateSequence { reader.readLine() }.take(400).joinToString("\n"))
        }
    }.getOrDefault("")

    /** O que não muda durante o export: lido uma vez por export, fora da main. */
    fun fixed(context: Context, gpu: String): JSONObject {
        val device = JSONObject().put("manufacturer", clean(Build.MANUFACTURER)).put("model", clean(Build.MODEL, 120))
            .put("androidSdk", Build.VERSION.SDK_INT).put("androidVersion", clean(Build.VERSION.RELEASE, 40))
            .put("buildId", clean(Build.ID)).put("hardware", clean(Build.HARDWARE)).put("board", clean(Build.BOARD))
            .put("process64Bit", Process.is64Bit()).put("abis", JSONArray(Build.SUPPORTED_ABIS.toList()))
            .put("socManufacturer", if (Build.VERSION.SDK_INT >= 31) clean(Build.SOC_MANUFACTURER) else JSONObject.NULL)
            .put("socModel", if (Build.VERSION.SDK_INT >= 31) clean(Build.SOC_MODEL) else JSONObject.NULL)
        // Só o que veio: a ficha tem teto e campo vazio não ajuda ninguém.
        fun add(key: String, value: String) { if (value.isNotEmpty()) device.put(key, value) }
        add("gpu", clean(ProblemReport.sanitize(gpu), 120))
        add("plat", systemProperty("ro.board.platform"))
        add("roHw", systemProperty("ro.hardware"))
        add("roSoc", systemProperty("ro.soc.model"))
        add("cpuHw", readCpuinfoHardware())
        runCatching {
            val am = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
            if (am != null) device.put("lowRam", am.isLowRamDevice).put("memClass", am.memoryClass)
        }
        return device
    }

    /** RAM do aparelho agora (total, livre, e se o sistema já se diz com pouca). */
    fun ram(context: Context, into: JSONObject): JSONObject {
        runCatching {
            val am = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
            if (am != null) {
                val info = ActivityManager.MemoryInfo()
                am.getMemoryInfo(info)
                into.put("ramTotal", info.totalMem).put("ramAvail", info.availMem).put("ramLow", info.lowMemory)
            }
        }
        return into
    }

    /** Bytes livres onde o export escreve (cache do app) e no volume da galeria. */
    fun disk(context: Context): JSONObject {
        val disk = JSONObject()
        runCatching { disk.put("cacheFree", context.cacheDir.usableSpace) }
        runCatching { context.getExternalFilesDir(null)?.let { disk.put("galleryFree", it.usableSpace) } }
        return disk
    }

    /** Calor (PowerManager.THERMAL_STATUS_*, Android 10+; −1 = o sistema não informa) e economia de energia. */
    fun power(context: Context): JSONObject {
        val power = JSONObject()
        runCatching {
            val pm = context.getSystemService(Context.POWER_SERVICE) as? PowerManager
            if (pm != null) {
                power.put("thermal", if (Build.VERSION.SDK_INT >= 29) pm.currentThermalStatus else -1)
                power.put("saver", pm.isPowerSaveMode)
            }
        }
        return power
    }
}
