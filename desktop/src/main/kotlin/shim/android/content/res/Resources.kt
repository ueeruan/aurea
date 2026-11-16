package android.content.res

import android.os.LocaleList
import android.util.DisplayMetrics
import com.aurea.aurea.RNames
import org.w3c.dom.Element
import org.w3c.dom.Node
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap
import javax.xml.parsers.DocumentBuilderFactory

/**
 * Os textos do Aurea no PC: os MESMOS XML de `res/values-xx/strings_*.xml` do Android,
 * lidos do classpath (`aurea-res/`). A busca segue a do Android: idioma+região,
 * idioma, catálogo padrão (pt-BR).
 */
open class Resources internal constructor(val configuration: Configuration) {

    open val displayMetrics: DisplayMetrics get() = DisplayMetrics.current

    private val chain: List<Catalog> by lazy { Catalog.chainFor(configuration.locales[0]) }
    private val locale: Locale get() = configuration.locales[0]

    class NotFoundException(message: String?) : RuntimeException(message)

    private fun raw(kind: String, id: Int): Any {
        val (k, name) = RNames.byId[id] ?: throw NotFoundException("id 0x${id.toString(16)}")
        for (c in chain) c.get(k, name)?.let { return it }
        throw NotFoundException("$k/$name")
    }

    fun getString(id: Int): String = raw("string", id) as String

    fun getString(id: Int, vararg args: Any?): String = format(getString(id), args)

    fun getText(id: Int): CharSequence = getString(id)

    fun getQuantityString(id: Int, quantity: Int): String {
        @Suppress("UNCHECKED_CAST")
        val forms = raw("plurals", id) as Map<String, String>
        val q = Plurals.select(locale, quantity)
        return forms[q] ?: forms["other"] ?: forms.values.first()
    }

    fun getQuantityString(id: Int, quantity: Int, vararg args: Any?): String = format(getQuantityString(id, quantity), args)

    fun getQuantityText(id: Int, quantity: Int): CharSequence = getQuantityString(id, quantity)

    fun getStringArray(id: Int): Array<String> {
        @Suppress("UNCHECKED_CAST")
        return (raw("array", id) as List<String>).toTypedArray()
    }

    fun getColor(id: Int, theme: Any?): Int = Catalog.parseColor(raw("color", id) as String)
    fun getColor(id: Int): Int = getColor(id, null)
    fun getIdentifier(name: String, defType: String, defPackage: String?): Int =
        RNames.byId.entries.firstOrNull { it.value.first == defType && it.value.second == name }?.key ?: 0
    fun getResourceEntryName(id: Int): String = RNames.byId[id]?.second ?: throw NotFoundException("$id")
    fun getResourceName(id: Int): String = RNames.byId[id]?.let { "com.aurea.aurea:${it.first}/${it.second}" } ?: throw NotFoundException("$id")

    fun openRawResource(id: Int): java.io.InputStream {
        val (kind, name) = RNames.byId[id] ?: throw NotFoundException("$id")
        return Catalog.openResource(kind, name) ?: throw NotFoundException("$kind/$name")
    }

    val assets: AssetManager get() = AssetManager.app

    @Deprecated("Android")
    fun updateConfiguration(config: Configuration?, metrics: DisplayMetrics?) {}

    private fun format(pattern: String, args: Array<out Any?>): String =
        if (args.isEmpty()) pattern else runCatching { String.format(locale, pattern, *args) }.getOrElse { pattern }

    companion object {
        private val byTag = ConcurrentHashMap<String, Resources>()

        /** Os recursos do app (idioma do sistema ou o escolhido em Ajustes). */
        val app: Resources get() = forConfiguration(Configuration.current)

        fun forConfiguration(c: Configuration): Resources {
            val tag = c.locales[0].toLanguageTag()
            val cached = byTag[tag]
            if (cached != null && cached.configuration.sameScreen(c)) return cached
            return Resources(Configuration(c)).also { byTag[tag] = it }
        }

        fun getSystem(): Resources = app
    }
}

/** Um catálogo (`values` ou `values-xx[-rYY]`): strings, plurals, arrays e cores. */
internal class Catalog(private val qualifier: String) {
    private val strings = HashMap<String, String>(8192)
    private val plurals = HashMap<String, Map<String, String>>()
    private val arrays = HashMap<String, List<String>>()
    private val colors = HashMap<String, String>()

    fun get(kind: String, name: String): Any? = when (kind) {
        "string" -> strings[name]
        "plurals" -> plurals[name]
        "array" -> arrays[name]
        "color" -> colors[name]
        else -> null
    }

    private fun load(): Catalog {
        val files = listXml(qualifier)
        val dbf = DocumentBuilderFactory.newInstance().apply { isNamespaceAware = false; isExpandEntityReferences = false }
        for (file in files) {
            val stream = javaClass.getResourceAsStream("/aurea-res/$qualifier/$file") ?: continue
            val doc = stream.use { dbf.newDocumentBuilder().parse(it) }
            val children = doc.documentElement.childNodes
            for (i in 0 until children.length) {
                val e = children.item(i) as? Element ?: continue
                val name = e.getAttribute("name").replace('.', '_')
                if (name.isEmpty()) continue
                when (e.tagName) {
                    "string" -> strings[name] = unescape(textOf(e))
                    "plurals" -> {
                        val forms = LinkedHashMap<String, String>()
                        val items = e.getElementsByTagName("item")
                        for (j in 0 until items.length) {
                            val it = items.item(j) as Element
                            forms[it.getAttribute("quantity")] = unescape(textOf(it))
                        }
                        plurals[name] = forms
                    }
                    "string-array", "array" -> {
                        val items = e.getElementsByTagName("item")
                        arrays[name] = (0 until items.length).map { unescape(textOf(items.item(it) as Element)) }
                    }
                    "color" -> colors[name] = textOf(e).trim()
                }
            }
        }
        return this
    }

    companion object {
        private val loaded = ConcurrentHashMap<String, Catalog>()

        private fun catalog(q: String): Catalog? {
            if (listXml(q).isEmpty()) return null
            return loaded.getOrPut(q) { Catalog(q).load() }
        }

        fun chainFor(locale: Locale): List<Catalog> {
            val lang = when (locale.language) { "in" -> "id"; "iw" -> "he"; else -> locale.language }
            val qs = buildList {
                if (locale.country.isNotEmpty()) add("values-$lang-r${locale.country}")
                add("values-$lang")
                if (lang == "id") add("values-in")
                add("values")
            }
            return qs.distinct().mapNotNull { catalog(it) }
        }

        /** O índice dos XML de cada pasta (gerado no build: aurea-res/index.txt). */
        private val index: Map<String, List<String>> by lazy {
            val txt = Catalog::class.java.getResourceAsStream("/aurea-res/index.txt")?.bufferedReader()?.readText()
            val out = HashMap<String, MutableList<String>>()
            txt?.lineSequence()?.map { it.trim() }?.filter { it.isNotEmpty() }?.forEach { line ->
                val dir = line.substringBefore('/')
                out.getOrPut(dir) { mutableListOf() } += line.substringAfter('/')
            }
            out
        }

        fun listXml(q: String): List<String> = index[q].orEmpty().filter { it.endsWith(".xml") }

        fun openResource(kind: String, name: String): java.io.InputStream? {
            for ((dir, files) in index) {
                if (dir != kind && !dir.startsWith("$kind-")) continue
                files.firstOrNull { it.substringBefore('.') == name }?.let {
                    return Catalog::class.java.getResourceAsStream("/aurea-res/$dir/$it")
                }
            }
            return null
        }

        fun parseColor(s: String): Int = android.graphics.Color.parseColor(s)

        /** O texto do nó com as tags de dentro (xliff:g, b, i…) reduzidas ao conteúdo. */
        private fun textOf(e: Element): String {
            val sb = StringBuilder()
            fun walk(n: Node) {
                when (n.nodeType) {
                    Node.TEXT_NODE, Node.CDATA_SECTION_NODE -> sb.append(n.nodeValue)
                    Node.ELEMENT_NODE -> { val c = n.childNodes; for (i in 0 until c.length) walk(c.item(i)) }
                }
            }
            val c = e.childNodes
            for (i in 0 until c.length) walk(c.item(i))
            return sb.toString()
        }

        /** As regras do aapt2: aspas protegem espaços, `\` escapa, espaço repetido vira um. */
        fun unescape(s: String): String {
            val out = StringBuilder(s.length)
            var quoted = false
            var lastSpace = false
            var i = 0
            while (i < s.length) {
                val ch = s[i]
                when {
                    ch == '\\' && i + 1 < s.length -> {
                        i++
                        when (val n = s[i]) {
                            'n' -> out.append('\n')
                            't' -> out.append('\t')
                            'u' -> if (i + 4 < s.length) { out.append(s.substring(i + 1, i + 5).toInt(16).toChar()); i += 4 }
                            else -> out.append(n)
                        }
                        lastSpace = false
                    }
                    ch == '"' -> quoted = !quoted
                    !quoted && ch.isWhitespace() -> {
                        if (!lastSpace) out.append(' ')
                        lastSpace = true
                    }
                    else -> { out.append(ch); lastSpace = false }
                }
                i++
            }
            // aapt2 corta só os espaços das pontas que não estavam entre aspas.
            return if (s.trimStart().startsWith('"') && s.trimEnd().endsWith('"')) out.toString() else out.toString().trim()
        }
    }
}

/** Regras de plural CLDR dos idiomas do Aurea (inteiros). */
internal object Plurals {
    fun select(locale: Locale, n: Int): String {
        val a = kotlin.math.abs(n)
        return when (locale.language) {
            "pt" -> if (a == 0 || a == 1) "one" else if (a != 0 && a % 1_000_000 == 0) "many" else "other"
            "en", "es", "hi" -> if (a == 1) "one" else if (locale.language == "es" && a != 0 && a % 1_000_000 == 0) "many" else "other"
            "ru" -> when {
                a % 10 == 1 && a % 100 != 11 -> "one"
                a % 10 in 2..4 && a % 100 !in 12..14 -> "few"
                else -> "many"
            }
            "ar" -> when {
                a == 0 -> "zero"
                a == 1 -> "one"
                a == 2 -> "two"
                a % 100 in 3..10 -> "few"
                a % 100 in 11..99 -> "many"
                else -> "other"
            }
            "id", "in" -> "other"
            else -> if (a == 1) "one" else "other"
        }
    }
}

/**
 * A configuração da tela. O Aurea PC se apresenta como um iPad DEITADO: largura
 * e altura em dp vêm da janela (ver com.aurea.pc.TabletFrame), sempre paisagem.
 */
open class Configuration() {
    @JvmField var screenWidthDp: Int = 1366
    @JvmField var screenHeightDp: Int = 1024
    @JvmField var smallestScreenWidthDp: Int = 1024
    @JvmField var orientation: Int = ORIENTATION_LANDSCAPE
    @JvmField var densityDpi: Int = 160
    @JvmField var fontScale: Float = 1f
    @JvmField var uiMode: Int = UI_MODE_TYPE_NORMAL or UI_MODE_NIGHT_YES
    @JvmField var screenLayout: Int = SCREENLAYOUT_SIZE_XLARGE or SCREENLAYOUT_LONG_NO
    @JvmField var keyboard: Int = KEYBOARD_QWERTY
    @JvmField var touchscreen: Int = TOUCHSCREEN_FINGER
    @JvmField var navigation: Int = 1
    private var localeList: LocaleList = LocaleList(Locale.getDefault())
    private var direction: Int = android.view.View.LAYOUT_DIRECTION_LTR

    constructor(o: Configuration) : this() { setTo(o) }

    fun setTo(o: Configuration) {
        screenWidthDp = o.screenWidthDp; screenHeightDp = o.screenHeightDp
        smallestScreenWidthDp = o.smallestScreenWidthDp; orientation = o.orientation
        densityDpi = o.densityDpi; fontScale = o.fontScale; uiMode = o.uiMode
        screenLayout = o.screenLayout; keyboard = o.keyboard; touchscreen = o.touchscreen
        localeList = o.localeList; direction = o.direction
    }

    val locales: LocaleList get() = localeList
    @Deprecated("Android") val locale: Locale get() = localeList[0]
    val layoutDirection: Int get() = direction
    val isScreenRound: Boolean get() = false

    fun setLocale(l: Locale?) {
        localeList = LocaleList(l ?: Locale.getDefault())
        setLayoutDirection(l)
    }
    fun setLocales(l: LocaleList?) { localeList = l ?: LocaleList(Locale.getDefault()) }
    fun setLayoutDirection(l: Locale?) {
        direction = if (l != null && l.language in setOf("ar", "fa", "he", "iw", "ur")) android.view.View.LAYOUT_DIRECTION_RTL
        else android.view.View.LAYOUT_DIRECTION_LTR
    }

    internal fun sameScreen(o: Configuration) = localeList == o.localeList

    override fun equals(other: Any?): Boolean = other is Configuration &&
        screenWidthDp == other.screenWidthDp && screenHeightDp == other.screenHeightDp &&
        densityDpi == other.densityDpi && fontScale == other.fontScale && localeList == other.localeList &&
        orientation == other.orientation && uiMode == other.uiMode

    override fun hashCode(): Int = arrayOf(screenWidthDp, screenHeightDp, densityDpi, fontScale, localeList, orientation, uiMode).contentHashCode()

    companion object {
        const val ORIENTATION_UNDEFINED = 0
        const val ORIENTATION_PORTRAIT = 1
        const val ORIENTATION_LANDSCAPE = 2
        const val UI_MODE_NIGHT_MASK = 0x30
        const val UI_MODE_NIGHT_NO = 0x10
        const val UI_MODE_NIGHT_YES = 0x20
        const val UI_MODE_TYPE_MASK = 0x0f
        const val UI_MODE_TYPE_NORMAL = 0x01
        const val SCREENLAYOUT_SIZE_MASK = 0x0f
        const val SCREENLAYOUT_SIZE_SMALL = 1
        const val SCREENLAYOUT_SIZE_NORMAL = 2
        const val SCREENLAYOUT_SIZE_LARGE = 3
        const val SCREENLAYOUT_SIZE_XLARGE = 4
        const val SCREENLAYOUT_LONG_NO = 0x10
        const val SCREENLAYOUT_LAYOUTDIR_RTL = 0x80
        const val KEYBOARD_QWERTY = 2
        const val TOUCHSCREEN_FINGER = 3
        const val SCREEN_WIDTH_DP_UNDEFINED = 0
        const val DENSITY_DPI_UNDEFINED = 0

        /** A configuração atual do app (janela + idioma), mantida por com.aurea.pc. */
        @Volatile var current: Configuration = Configuration()
    }
}

class AssetFileDescriptor(val parcelFileDescriptor: android.os.ParcelFileDescriptor) : java.io.Closeable {
    val fileDescriptor: java.io.FileDescriptor get() = parcelFileDescriptor.fileDescriptor
    val startOffset: Long get() = 0
    val length: Long get() = parcelFileDescriptor.statSize
    fun createInputStream(): java.io.FileInputStream = java.io.FileInputStream(parcelFileDescriptor.file)
    override fun close() = parcelFileDescriptor.close()
}

/**
 * `assets/` do APK = `engine/assets` (fontes, LUTs, presets), que o instalador
 * leva para a pasta do app.
 */
class AssetManager private constructor() {
    fun open(fileName: String): java.io.InputStream {
        com.aurea.pc.PcPaths.asset(fileName)?.let { return java.io.FileInputStream(it) }
        return javaClass.getResourceAsStream("/assets/$fileName") ?: throw java.io.FileNotFoundException(fileName)
    }
    fun open(fileName: String, accessMode: Int): java.io.InputStream = open(fileName)
    fun list(path: String): Array<String>? = com.aurea.pc.PcPaths.assetList(path)
    fun openFd(fileName: String): AssetFileDescriptor =
        AssetFileDescriptor(android.os.ParcelFileDescriptor.open(com.aurea.pc.PcPaths.asset(fileName) ?: throw java.io.FileNotFoundException(fileName), "r"))
    companion object {
        val app = AssetManager()
        const val ACCESS_STREAMING = 2
        const val ACCESS_BUFFER = 3
    }
}
