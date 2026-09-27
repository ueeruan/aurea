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
}
