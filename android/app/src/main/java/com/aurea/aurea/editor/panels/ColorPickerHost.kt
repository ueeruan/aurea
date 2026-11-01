package com.aurea.aurea.editor.panels

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.ColorPickerFull
import com.aurea.aurea.ui.ds.ColorPopover
import com.aurea.aurea.ui.ds.EmbeddedColorPicker
import com.aurea.aurea.ui.ds.FullEyedropper

// =============================================================================
// Omega aparência (área 05): o seletor de cor ligado ao editor — o conta-gotas
// ao vivo do palco (P1-1), o seletor embutido com o passo de desfazer do store
// e a apresentação do pedido de cor dos painéis (balão com âncora, tela cheia
// sem). O desenho do seletor mora em `ui/ds/ColorPicker.kt` / `ColorPickerFull.kt`.
// =============================================================================

/** O conta-gotas de um seletor: aceso e o botão que liga/desliga. */
internal class EyedropperHook(val active: Boolean, val toggle: () -> Unit)

/**
 * Liga o conta-gotas AO VIVO do palco (`store.eyedropper`, desenhado em
 * [ColorStageOverlay]): cada arrasto no palco é UM passo de desfazer (aberto na
 * primeira amostra, fechado ao soltar) e o modo continua ligado até o botão ser
 * tocado de novo, outro seletor pedir o palco, a camada mudar ou o seletor sair
 * da tela. A amostra entra com o alfa atual da cor ([currentAlpha]). Cor de efeito
 * (0.4.5): [skipLayer]/[skipEffect] desligam esse efeito só na captura do palco.
 */
@Composable
internal fun rememberStageEyedropper(
    store: EditorStore,
    currentAlpha: () -> Float,
    onColor: (FloatArray) -> Unit,
    onGestureStart: () -> Unit,
    onGestureEnd: () -> Unit,
    skipLayer: Long = 0L,
    skipEffect: Int = -1,
): EyedropperHook {
    val mine = remember { mutableStateOf<EditorStore.EyedropperRequest?>(null) }
    val owner = remember { longArrayOf(Long.MIN_VALUE) }
    val emit by rememberUpdatedState(onColor)
    val alpha by rememberUpdatedState(currentAlpha)
    val begin by rememberUpdatedState(onGestureStart)
    val end by rememberUpdatedState(onGestureEnd)
    val active = mine.value != null && store.eyedropper === mine.value

    fun cancelMine() {
        val m = mine.value ?: return
        mine.value = null
        if (store.eyedropper === m) store.eyedropper = null
        m.onCancel()
    }

    DisposableEffect(store) { onDispose { cancelMine() } }
    // Outra camada escolhida: a amostra iria para ela. Desliga.
    val primary = store.primary
    LaunchedEffect(primary) {
        if (mine.value != null && owner[0] != (primary ?: Long.MIN_VALUE)) cancelMine()
    }

    val toggle: () -> Unit = {
        if (mine.value != null && store.eyedropper === mine.value) {
            cancelMine()
        } else {
            var open = false
            val close = {
                if (open) {
                    open = false
                    end()
                }
            }
            val withAlphaOf = { rgba: FloatArray -> floatArrayOf(rgba[0], rgba[1], rgba[2], alpha()) }
            val request = EditorStore.EyedropperRequest(
                onSample = { rgba ->
                    if (!open) {
                        open = true
                        begin()
                    }
                    emit(withAlphaOf(rgba))
                },
                onCommit = { rgba ->
                    if (!open) {
                        open = true
                        begin()
                    }
                    emit(withAlphaOf(rgba))
                    close()
                },
                onCancel = { close() },
                skipLayer = skipLayer,
                skipEffect = skipEffect,
            )
            // Um palco, um conta-gotas: quem tinha o modo sai dele.
            store.eyedropper?.onCancel()
            mine.value = request
            owner[0] = store.primary ?: Long.MIN_VALUE
            store.eyedropper = request
        }
    }
    return EyedropperHook(active, toggle)
}

/**
 * O conta-gotas da TELA CHEIA (0.4.5): o seletor se recolhe e o palco entra no modo
 * "recolhido" (`collapsed`): um toque/arrasto amostra, soltar devolve a cor ao seletor
 * e o modo sai sozinho. O passo de desfazer é o da tela cheia (nada abre aqui).
 */
@Composable
internal fun rememberFullEyedropper(store: EditorStore, skipLayer: Long = 0L, skipEffect: Int = -1): FullEyedropper {
    val mine = remember { arrayOfNulls<EditorStore.EyedropperRequest>(1) }
    return remember(store, skipLayer, skipEffect) {
        fun release(r: EditorStore.EyedropperRequest?) {
            if (r == null) return
            if (mine[0] === r) mine[0] = null
            if (store.eyedropper === r) store.eyedropper = null
        }
        FullEyedropper(
            start = { onSample, onCommit, onCancel ->
                var self: EditorStore.EyedropperRequest? = null
                val request = EditorStore.EyedropperRequest(
                    onSample = { rgba -> onSample(rgba) },
                    onCommit = { rgba ->
                        release(self)
                        onCommit(rgba)
                    },
                    onCancel = {
                        release(self)
                        onCancel()
                    },
                    skipLayer = skipLayer,
                    skipEffect = skipEffect,
                    collapsed = true,
                )
                self = request
                // Um palco, um conta-gotas: quem tinha o modo sai dele.
                store.eyedropper?.onCancel()
                if (store.playing) store.pause()
                mine[0] = request
                store.eyedropper = request
            },
            stop = { release(mine[0]) },
        )
    }
}

/**
 * O seletor embutido gravando no store: um passo de desfazer por gesto
 * ([gestureLabel]), conta-gotas ao vivo e os avisos no toast do editor.
 */
@Composable
internal fun StoreColorPicker(
    store: EditorStore,
    color: FloatArray,
    gestureLabel: String,
    onColor: (FloatArray) -> Unit,
    modifier: Modifier = Modifier,
    withAlpha: Boolean = true,
) {
    val latest by rememberUpdatedState(color)
    val label by rememberUpdatedState(gestureLabel)
    val start = remember(store) { { store.beginGesture(label) } }
    val end = remember(store) { { store.endGesture() } }
    val eye = rememberStageEyedropper(store, { latest.getOrElse(3) { 1f } }, onColor, start, end)
    EmbeddedColorPicker(
        color = color,
        onColor = onColor,
        modifier = modifier,
        withAlpha = withAlpha,
        onGestureStart = start,
        onGestureEnd = end,
        eyedropperActive = eye.active,
        onEyedropper = eye.toggle,
        onMessage = { store.showToast(it) },
        fullEyedropper = rememberFullEyedropper(store),
    )
}

/**
 * A APRESENTAÇÃO DO PEDIDO DE COR ([ColorRequest]) dos painéis: com âncora, o
 * balão com o seletor embutido (P0-5); sem, o seletor em tela cheia (P1-8).
 * Quem pediu já abriu um passo de desfazer; [onClose] chama `finish()`. O
 * pedido fecha sozinho se o painel ou a camada principal mudarem (a cor iria
 * para outro lugar).
 */
@Composable
internal fun ColorRequestHost(store: EditorStore, panel: EditorPanel, request: ColorRequest, onClose: () -> Unit) {
    val close by rememberUpdatedState(onClose)
    val openedOn = remember(request) { panel to store.primary }
    if (openedOn.first != panel || openedOn.second != store.primary) {
        SideEffect { close() }
        return
    }
    var current by remember(request) {
        val v = request.initial
        mutableStateOf(floatArrayOf(v.getOrElse(0) { 0f }, v.getOrElse(1) { 0f }, v.getOrElse(2) { 0f }, v.getOrElse(3) { 1f }))
    }
    val applyColor: (FloatArray) -> Unit = { c ->
        current = c
        request.onChange(c[0], c[1], c[2], c[3])
    }
    val anchor = request.anchor
    val full = rememberFullEyedropper(store, request.sampleLayer, request.sampleEffect)
    if (anchor != null) {
        // O balão segura o passo do pedido: o conta-gotas dele não abre outro.
        val eye = rememberStageEyedropper(store, { current[3] }, applyColor, {}, {}, request.sampleLayer, request.sampleEffect)
        ColorPopover(
            anchor = anchor,
            color = current,
            onColor = applyColor,
            onDismiss = { close() },
            withAlpha = request.withAlpha,
            eyedropperActive = eye.active,
            onEyedropper = eye.toggle,
            onMessage = { store.showToast(it) },
            fullEyedropper = full,
            // O "Mais cores" do balão amostrando: o balão some e deixa a prévia livre.
            collapsed = store.eyedropper?.collapsed == true,
        )
    } else {
        ColorPickerFull(
            initial = current,
            withAlpha = request.withAlpha,
            onChange = { r, g, b, a -> applyColor(floatArrayOf(r, g, b, a)) },
            onDone = { close() },
            eyedropper = full,
        )
    }
}
