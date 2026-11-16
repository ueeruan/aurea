package android.annotation

@Retention(AnnotationRetention.SOURCE)
annotation class SuppressLint(vararg val value: String)

@Retention(AnnotationRetention.SOURCE)
annotation class TargetApi(val value: Int)
