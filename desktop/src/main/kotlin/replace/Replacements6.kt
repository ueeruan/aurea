package com.aurea.aurea

import android.app.Application

/** O Application do Aurea PC: o contexto único do app (pastas, textos, serviços). */
class AureaApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        com.aurea.aurea.engine.DeviceMemoryClass.init(this)
    }
}
