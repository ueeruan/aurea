package com.aurea.aurea.ads

/** Quarantine the provider version in the reproduced Unity media-session crash.
 * Never catch a fatal JNI exception and recursively restart Android's Looper.
 * Other versions are not claimed to be fixed; they retain the normal SDK flow.
 */
internal object AdWebViewCompatibility {
    fun allowsLevelPlay(version: String?): Boolean {
        val normalized = version?.trim()?.substringBefore(' ')
        return !normalized.isNullOrEmpty() && normalized != "124.0.6367.219"
    }
}
