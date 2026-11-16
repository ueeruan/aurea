package com.aurea.aurea.editor.timeline

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow

/**
 * Zoom da timeline pedido de fora dela (multiplicador do zoom atual). O Aurea
 * PC manda por aqui os atalhos `=`/`-` e Alt + roda do mouse; no Android e no
 * iOS ninguém pede, e o zoom continua só na pinça.
 */
object TimelineZoomRequests {
    private val requests = MutableSharedFlow<Float>(extraBufferCapacity = 16)
    internal val flow: SharedFlow<Float> get() = requests
    fun request(factor: Float) { if (factor.isFinite() && factor > 0f) requests.tryEmit(factor) }
}
