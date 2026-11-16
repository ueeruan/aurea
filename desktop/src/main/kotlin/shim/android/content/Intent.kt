package android.content

import android.net.Uri
import android.os.Bundle
import java.awt.Toolkit
import java.awt.datatransfer.DataFlavor
import java.awt.datatransfer.StringSelection

/** Só o que o app usa de `Intent`: ação, dados, extras. Quem executa é [com.aurea.pc.PcIntents]. */
open class Intent() {
    @set:JvmName("assignAction") var action: String? = null
    @set:JvmName("assignData") var data: Uri? = null
    @set:JvmName("assignType") var type: String? = null
    @set:JvmName("assignFlags") var flags: Int = 0
    var component: ComponentName? = null
    var clipData: ClipData? = null
    private val extras = LinkedHashMap<String, Any?>()
    private val categories = LinkedHashSet<String>()

    constructor(action: String?) : this() { this.action = action }
    constructor(action: String?, uri: Uri?) : this() { this.action = action; this.data = uri }
    constructor(context: Context, cls: Class<*>) : this() { component = ComponentName(context, cls) }
    constructor(o: Intent) : this() {
        action = o.action; data = o.data; type = o.type; flags = o.flags; component = o.component
        extras.putAll(o.extras); categories.addAll(o.categories)
    }

    fun setAction(a: String?) = apply { action = a }
    fun setData(u: Uri?) = apply { data = u }
    fun setType(t: String?) = apply { type = t }
    fun setDataAndType(u: Uri?, t: String?) = apply { data = u; type = t }
    fun setPackage(p: String?) = apply { }
    fun setClassName(pkg: String, cls: String) = apply { component = ComponentName(pkg, cls) }
    fun addFlags(f: Int) = apply { flags = flags or f }
    fun setFlags(f: Int) = apply { flags = f }
    fun addCategory(c: String) = apply { categories += c }

    fun putExtra(name: String, value: String?) = apply { extras[name] = value }
    fun putExtra(name: String, value: CharSequence?) = apply { extras[name] = value }
    fun putExtra(name: String, value: Int) = apply { extras[name] = value }
    fun putExtra(name: String, value: Long) = apply { extras[name] = value }
    fun putExtra(name: String, value: Float) = apply { extras[name] = value }
    fun putExtra(name: String, value: Boolean) = apply { extras[name] = value }
    fun putExtra(name: String, value: Uri?) = apply { extras[name] = value }
    fun putExtra(name: String, value: Array<String>?) = apply { extras[name] = value }
    fun putExtra(name: String, value: java.io.Serializable?) = apply { extras[name] = value }
    fun putParcelableArrayListExtra(name: String, value: ArrayList<out Any?>?) = apply { extras[name] = value }
    fun putStringArrayListExtra(name: String, value: ArrayList<String>?) = apply { extras[name] = value }

    fun hasExtra(name: String) = name in extras
    fun getStringExtra(name: String): String? = extras[name] as? String
    fun getIntExtra(name: String, def: Int): Int = (extras[name] as? Int) ?: def
    fun getLongExtra(name: String, def: Long): Long = (extras[name] as? Long) ?: def
    fun getFloatExtra(name: String, def: Float): Float = (extras[name] as? Float) ?: def
    fun getBooleanExtra(name: String, def: Boolean): Boolean = (extras[name] as? Boolean) ?: def
    @Suppress("UNCHECKED_CAST")
    fun <T> getParcelableExtra(name: String, cls: Class<T>): T? = extras[name] as? T
    @Suppress("UNCHECKED_CAST")
    fun <T> getParcelableExtra(name: String): T? = extras[name] as? T
    @Suppress("UNCHECKED_CAST")
    fun getStringArrayExtra(name: String): Array<String>? = extras[name] as? Array<String>
    fun getCharSequenceExtra(name: String): CharSequence? = extras[name] as? CharSequence
    fun getExtras(): Bundle? = null
    fun resolveActivity(pm: android.content.pm.PackageManager): ComponentName? = ComponentName("pc", "pc")

    companion object {
        const val ACTION_VIEW = "android.intent.action.VIEW"
        const val ACTION_SEND = "android.intent.action.SEND"
        const val ACTION_SEND_MULTIPLE = "android.intent.action.SEND_MULTIPLE"
        const val ACTION_MAIN = "android.intent.action.MAIN"
        const val ACTION_SENDTO = "android.intent.action.SENDTO"
        const val ACTION_OPEN_DOCUMENT = "android.intent.action.OPEN_DOCUMENT"
        const val ACTION_CHOOSER = "android.intent.action.CHOOSER"
        const val CATEGORY_BROWSABLE = "android.intent.category.BROWSABLE"
        const val CATEGORY_LAUNCHER = "android.intent.category.LAUNCHER"
        const val CATEGORY_DEFAULT = "android.intent.category.DEFAULT"
        const val EXTRA_STREAM = "android.intent.extra.STREAM"
        const val EXTRA_TEXT = "android.intent.extra.TEXT"
        const val EXTRA_SUBJECT = "android.intent.extra.SUBJECT"
        const val EXTRA_EMAIL = "android.intent.extra.EMAIL"
        const val EXTRA_TITLE = "android.intent.extra.TITLE"
        const val EXTRA_INTENT = "android.intent.extra.INTENT"
        const val EXTRA_MIME_TYPES = "android.intent.extra.MIME_TYPES"
        const val FLAG_GRANT_READ_URI_PERMISSION = 1
        const val FLAG_GRANT_WRITE_URI_PERMISSION = 2
        const val FLAG_GRANT_PERSISTABLE_URI_PERMISSION = 0x40
        const val FLAG_ACTIVITY_NEW_TASK = 0x10000000
        const val FLAG_ACTIVITY_SINGLE_TOP = 0x20000000
        const val FLAG_ACTIVITY_CLEAR_TOP = 0x04000000
        const val FLAG_ACTIVITY_NO_HISTORY = 0x40000000
        const val FLAG_UPDATE_CURRENT = 0x08000000
        const val FLAG_IMMUTABLE = 0x04000000

        fun createChooser(target: Intent, title: CharSequence?): Intent =
            Intent(ACTION_CHOOSER).also { it.extras[EXTRA_INTENT] = target }

        /** O alvo de um [createChooser] (o PC não tem seletor: executa direto). */
        fun chooserTarget(intent: Intent): Intent? = intent.extras[EXTRA_INTENT] as? Intent
    }
}

class ClipData private constructor(val description: ClipDescription, private val items: MutableList<Item>) {
    class Item(val text: CharSequence?, val uri: Uri? = null) {
        constructor(uri: Uri) : this(null, uri)
        fun coerceToText(context: Context?): CharSequence = text ?: uri?.toString() ?: ""
    }
    class ClipDescription(val label: CharSequence?, private val mimeTypes: Array<String>) {
        fun hasMimeType(mime: String) = mimeTypes.any { it == mime || it.startsWith(mime.substringBefore('*')) }
        fun getMimeType(i: Int) = mimeTypes[i]
        val mimeTypeCount get() = mimeTypes.size
    }

    val itemCount: Int get() = items.size
    fun getItemAt(i: Int) = items[i]
    fun addItem(item: Item) { items += item }

    companion object {
        fun newPlainText(label: CharSequence?, text: CharSequence?) =
            ClipData(ClipDescription(label, arrayOf("text/plain")), mutableListOf(Item(text)))
        fun newRawUri(label: CharSequence?, uri: Uri) =
            ClipData(ClipDescription(label, arrayOf("text/uri-list")), mutableListOf(Item(uri)))
        fun newUri(resolver: ContentResolver, label: CharSequence?, uri: Uri) = newRawUri(label, uri)
    }
}

/** A área de transferência do Windows (texto). */
class ClipboardManager private constructor() {
    private val clipboard get() = Toolkit.getDefaultToolkit().systemClipboard
    private val listeners = mutableListOf<OnPrimaryClipChangedListener>()

    fun interface OnPrimaryClipChangedListener { fun onPrimaryClipChanged() }

    fun setPrimaryClip(clip: ClipData) {
        val text = if (clip.itemCount > 0) clip.getItemAt(0).coerceToText(null).toString() else ""
        runCatching { clipboard.setContents(StringSelection(text), null) }
        listeners.forEach { it.onPrimaryClipChanged() }
    }

    val primaryClip: ClipData?
        get() = runCatching {
            if (clipboard.isDataFlavorAvailable(DataFlavor.stringFlavor))
                ClipData.newPlainText("", clipboard.getData(DataFlavor.stringFlavor) as String)
            else null
        }.getOrNull()

    fun hasPrimaryClip() = primaryClip != null
    val primaryClipDescription: ClipData.ClipDescription? get() = primaryClip?.description
    fun clearPrimaryClip() { runCatching { clipboard.setContents(StringSelection(""), null) } }
    fun addPrimaryClipChangedListener(l: OnPrimaryClipChangedListener) { listeners += l }
    fun removePrimaryClipChangedListener(l: OnPrimaryClipChangedListener) { listeners -= l }

    companion object { val app = ClipboardManager() }
}
