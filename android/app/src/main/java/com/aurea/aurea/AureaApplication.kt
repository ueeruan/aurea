package com.aurea.aurea

import android.app.Application
import android.content.Context
import com.aurea.aurea.diagnostics.CrashReporter

class AureaApplication : Application() {
    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(base)
        // Install before providers and activity startup, in every app process.
        CrashReporter.instalar(this)
    }

    override fun onCreate() {
        super.onCreate()
        // Classe de memória LOW (até ~4 GB): caches de bitmap e limiar de pressão.
        com.aurea.aurea.engine.DeviceMemoryClass.init(this)
    }
}
