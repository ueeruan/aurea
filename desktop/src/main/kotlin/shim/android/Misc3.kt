@file:Suppress("unused", "UNUSED_PARAMETER")

package android.text.format

import android.content.Context

object Formatter {
    private fun fmt(context: Context?, bytes: Long, short: Boolean): String {
        val units = arrayOf("B", "kB", "MB", "GB", "TB")
        var v = bytes.toDouble(); var u = 0
        while (v >= 900 && u < units.lastIndex) { v /= 1000.0; u++ }
        val pattern = if (u == 0 || v >= 100 || (short && v >= 10)) "%.0f" else if (short || v >= 10) "%.1f" else "%.2f"
        return String.format(java.util.Locale.getDefault(), pattern, v) + " " + units[u]
    }
    @JvmStatic fun formatFileSize(context: Context?, bytes: Long): String = fmt(context, bytes, false)
    @JvmStatic fun formatShortFileSize(context: Context?, bytes: Long): String = fmt(context, bytes, true)
}

object DateUtils {
    const val SECOND_IN_MILLIS = 1000L
    const val MINUTE_IN_MILLIS = 60_000L
    const val HOUR_IN_MILLIS = 3_600_000L
    const val DAY_IN_MILLIS = 86_400_000L
    const val WEEK_IN_MILLIS = 604_800_000L
    const val FORMAT_ABBREV_RELATIVE = 0x40000
    const val FORMAT_SHOW_DATE = 0x10
    const val FORMAT_SHOW_TIME = 1
    const val FORMAT_SHOW_YEAR = 4
    const val FORMAT_ABBREV_MONTH = 0x10000
    @JvmStatic fun getRelativeTimeSpanString(time: Long): CharSequence = getRelativeTimeSpanString(time, System.currentTimeMillis(), MINUTE_IN_MILLIS)
    @JvmStatic fun getRelativeTimeSpanString(time: Long, now: Long, minResolution: Long): CharSequence = getRelativeTimeSpanString(time, now, minResolution, 0)
    @JvmStatic fun getRelativeTimeSpanString(time: Long, now: Long, minResolution: Long, flags: Int): CharSequence {
        val d = (now - time).coerceAtLeast(0)
        val pt = java.util.Locale.getDefault().language == "pt"
        return when {
            d < MINUTE_IN_MILLIS -> if (pt) "agora" else "now"
            d < HOUR_IN_MILLIS -> (d / MINUTE_IN_MILLIS).let { if (pt) "há $it min" else "$it min ago" }
            d < DAY_IN_MILLIS -> (d / HOUR_IN_MILLIS).let { if (pt) "há $it h" else "$it h ago" }
            d < WEEK_IN_MILLIS -> (d / DAY_IN_MILLIS).let { if (pt) "há $it d" else "$it d ago" }
            else -> java.text.DateFormat.getDateInstance(java.text.DateFormat.MEDIUM).format(java.util.Date(time))
        }
    }
    @JvmStatic fun formatElapsedTime(seconds: Long): String =
        if (seconds >= 3600) String.format("%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60) else String.format("%02d:%02d", seconds / 60, seconds % 60)
    @JvmStatic fun formatDateTime(context: Context?, millis: Long, flags: Int): String =
        java.text.DateFormat.getDateTimeInstance(java.text.DateFormat.MEDIUM, java.text.DateFormat.SHORT).format(java.util.Date(millis))
    @JvmStatic fun isToday(millis: Long): Boolean =
        java.time.Instant.ofEpochMilli(millis).atZone(java.time.ZoneId.systemDefault()).toLocalDate() == java.time.LocalDate.now()
}

object DateFormat {
    @JvmStatic fun is24HourFormat(context: Context?): Boolean = true
    @JvmStatic fun getDateFormat(context: Context?): java.text.DateFormat = java.text.DateFormat.getDateInstance()
    @JvmStatic fun getTimeFormat(context: Context?): java.text.DateFormat = java.text.DateFormat.getTimeInstance(java.text.DateFormat.SHORT)
}
