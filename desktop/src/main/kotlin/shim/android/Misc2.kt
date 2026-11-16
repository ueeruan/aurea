@file:Suppress("unused", "UNUSED_PARAMETER")

package android.database

/** Cursor em memória: o que o ContentResolver do PC devolve. */
class MatrixCursor(private val columns: Array<String>) : Cursor {
    private val rows = ArrayList<Array<Any?>>()
    private var position = -1
    private var closed = false
    override fun getColumnNames(): Array<String> = columns
    override fun getPosition() = position
    override fun isClosed() = closed
    fun addRow(values: Array<Any?>) { rows += values }
    fun addRow(values: Iterable<Any?>) { rows += values.toList().toTypedArray() }
    override fun getCount() = rows.size
    override fun getColumnCount() = columns.size
    override fun moveToFirst() = moveToPosition(0)
    override fun moveToNext() = moveToPosition(position + 1)
    override fun moveToLast() = moveToPosition(rows.size - 1)
    override fun moveToPosition(p: Int): Boolean { position = p.coerceIn(-1, rows.size); return p in rows.indices }
    override fun getColumnIndex(name: String) = columns.indexOf(name)
    override fun getColumnIndexOrThrow(name: String) = getColumnIndex(name).also { require(it >= 0) { "coluna $name" } }
    override fun getColumnName(i: Int) = columns[i]
    private fun v(i: Int): Any? = rows[position][i]
    override fun getString(i: Int) = v(i)?.toString()
    override fun getLong(i: Int) = (v(i) as? Number)?.toLong() ?: v(i)?.toString()?.toLongOrNull() ?: 0L
    override fun getInt(i: Int) = getLong(i).toInt()
    override fun getFloat(i: Int) = (v(i) as? Number)?.toFloat() ?: 0f
    override fun getDouble(i: Int) = (v(i) as? Number)?.toDouble() ?: 0.0
    override fun getBlob(i: Int) = v(i) as? ByteArray
    override fun isNull(i: Int) = v(i) == null
    override fun getType(i: Int) = when (v(i)) { null -> 0; is Long, is Int -> 1; is Number -> 2; is ByteArray -> 4; else -> 3 }
    override fun close() { closed = true }
}

abstract class ContentObserver(handler: android.os.Handler?) {
    open fun onChange(selfChange: Boolean) {}
    open fun onChange(selfChange: Boolean, uri: android.net.Uri?) = onChange(selfChange)
}
