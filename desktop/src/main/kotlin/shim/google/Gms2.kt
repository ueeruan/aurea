@file:Suppress("unused", "UNUSED_PARAMETER")

package com.google.android.gms.appset

import com.google.android.gms.tasks.Task
import com.google.android.gms.tasks.Tasks

/** Sem App Set ID no PC: quem chama cai no id reserva (o id deste PC). */
class AppSetIdInfo(val id: String, val scope: Int)

class AppSetIdClient internal constructor() {
    val appSetIdInfo: Task<AppSetIdInfo> get() = Tasks.forException(UnsupportedOperationException("pc"))
}

object AppSet {
    @JvmStatic fun getClient(context: android.content.Context): AppSetIdClient = AppSetIdClient()
}
