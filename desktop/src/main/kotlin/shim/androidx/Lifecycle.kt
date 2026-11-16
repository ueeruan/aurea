@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.lifecycle

import android.app.Application

/** ViewModel com o Application (o contexto único do PC). */
open class AndroidViewModel(private val app: Application) : ViewModel() {
    @Suppress("UNCHECKED_CAST")
    fun <T : Application> getApplication(): T = app as T
}
