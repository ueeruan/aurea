package com.aurea.pc

import android.view.SurfaceView
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.layout.onSizeChanged
import com.aurea.aurea.desktop.DesktopFrames
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.Pixmap
import org.jetbrains.skia.Image
import org.jetbrains.skia.ImageInfo
import org.jetbrains.skia.Rect
import org.jetbrains.skia.impl.BufferUtil
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * O palco do motor no PC — o papel da SurfaceView no Android.
 *
 * O motor renderiza na GPU (Vulkan, placa dedicada) na própria thread, no
 * tamanho em pixels reais deste espaço, e publica cada quadro pronto. A cada
 * vsync da janela, [FramePump] pega o quadro novo (se houver) e o Skia o
 * desenha (Direct3D 12) — com a UI do Compose por cima, como no tablet.
 * Recompor a UI nunca toca no palco; um quadro novo só invalida o desenho.
 */
@Composable
fun EnginePreview(view: SurfaceView, modifier: Modifier) {
    val holder = view.holder
    val tick = remember { mutableIntStateOf(0) }
    val pump = remember { FramePump() }
    val state = remember { object { var created = false; var w = 0; var h = 0 } }

    DisposableEffect(view) {
        onDispose {
            if (state.created) view.callbacks.forEach { it.surfaceDestroyed(holder) }
            state.created = false
            pump.close()
        }
    }
    LaunchedEffect(pump) {
        // 0.5.3: com quadros chegando, a busca segue o vsync da janela (como antes).
        // Parado, pedir um quadro ao relógio do Compose a cada vsync mantinha a janela
        // inteira redesenhando e apresentando 60 vezes por segundo sem nada novo.
        // Depois de QUIET_POLLS buscas vazias, a busca passa a um temporizador curto,
        // que não pede quadro ao Compose; o primeiro quadro novo invalida o palco
        // (tick) e a busca volta ao vsync.
        var quiet = 0
        while (true) {
            if (quiet < QUIET_POLLS) withFrameNanos { } else kotlinx.coroutines.delay(IDLE_POLL_MS)
            if (pump.poll()) {
                tick.intValue++
                quiet = 0
            } else if (quiet < QUIET_POLLS) {
                quiet++
            }
        }
    }

    Box(
        modifier
            .onSizeChanged { size ->
                val w = size.width.coerceAtLeast(1)
                val h = size.height.coerceAtLeast(1)
                if (state.created && w == state.w && h == state.h) return@onSizeChanged
                state.w = w; state.h = h
                holder.surfaceFrame.set(0, 0, w, h)
                if (!state.created) {
                    state.created = true
                    view.callbacks.forEach { it.surfaceCreated(holder) }
                }
                view.callbacks.forEach { it.surfaceChanged(holder, 1, w, h) }
            }
            .drawBehind {
                tick.intValue // lê o estado: um quadro novo redesenha só esta camada
                val img = pump.image ?: return@drawBehind
                drawIntoCanvas { c ->
                    c.nativeCanvas.drawImageRect(img, Rect.makeWH(img.width.toFloat(), img.height.toFloat()),
                        Rect.makeWH(size.width, size.height))
                }
            },
    )
}

/** Buscas vazias seguidas (≈ meio segundo a 60 Hz) antes de sair do vsync. */
private const val QUIET_POLLS = 30
/** Parado: intervalo da busca fora do relógio de quadros (atraso máximo do 1º quadro novo). */
private const val IDLE_POLL_MS = 5L

/**
 * Busca o último quadro publicado pelo motor (cópia única: GPU → memória
 * mapeada → este buffer) e o embrulha numa imagem do Skia sem copiar de novo.
 *
 * O Compose do PC guarda o desenho do palco em cache e pode redesenhá-lo sem
 * gravar de novo: a imagem do quadro mostrado (e o buffer dela) ficam intactos
 * até o buffer voltar na roda. Quatro buffers: o que está na tela, o anterior
 * (ainda num desenho em cache) e dois livres para o motor escrever. Uma imagem
 * só é fechada quando o buffer dela vai ser reescrito.
 */
internal class FramePump {
    private val buffers = arrayOfNulls<ByteBuffer>(4)
    private val images = arrayOfNulls<Image>(4)
    private var slot = 0
    private val info = IntArray(3)
    @Volatile var image: Image? = null; private set
    private var lastSerial = 0L

    fun poll(): Boolean {
        val handle = PcEngine.handle
        if (handle == 0L) return false
        val i = (slot + 1) % buffers.size     // o de 3 quadros atrás: o mostrado e os 2 anteriores ficam intactos
        val w = PcEngine.lastWidth.coerceAtLeast(1)
        val h = PcEngine.lastHeight.coerceAtLeast(1)
        val need = w * h * 4
        var buf = buffers[i]
        if (buf == null || buf.capacity() < need) {
            images[i]?.close(); images[i] = null
            buf = ByteBuffer.allocateDirect(need + (need shr 3)).order(ByteOrder.nativeOrder())
            buffers[i] = buf
        }
        // O motor só escreve no buffer quando há quadro novo: a imagem velha dele sai antes.
        val serial = DesktopFrames.nativeAcquireFrame(handle, buf, buf!!.capacity(), info)
        if (serial == 0L || serial == lastSerial) {
            // Quadro maior que o buffer: o motor diz o tamanho e a próxima volta realoca.
            if (info[0] > 0 && info[1] > 0 && info[2].toLong() * info[1] > (buf?.capacity() ?: 0)) {
                PcEngine.lastWidth = info[2] / 4; PcEngine.lastHeight = info[1]
            }
            return false
        }
        lastSerial = serial
        val (fw, fh, rowBytes) = Triple(info[0], info[1], info[2])
        PcEngine.lastWidth = rowBytes / 4; PcEngine.lastHeight = fh
        val pixmap = Pixmap.make(ImageInfo(fw, fh, ColorType.BGRA_8888, ColorAlphaType.PREMUL), BufferUtil.getPointerFromByteBuffer(buf), rowBytes)
        val img = Image.makeFromPixmap(pixmap)
        pixmap.close()
        images[i]?.close()
        images[i] = img
        image = img
        slot = i
        return true
    }

    fun close() {
        image = null
        for (k in images.indices) { images[k]?.close(); images[k] = null }
    }
}

/** O motor do app (o mesmo AureaEngine do EditorStore) e o tamanho do último quadro. */
object PcEngine {
    @Volatile var handle: Long = 0L
    @Volatile var lastWidth: Int = 1280
    @Volatile var lastHeight: Int = 720

    fun bind(engine: com.aurea.aurea.engine.AureaEngine) {
        handle = runCatching {
            com.aurea.aurea.engine.AureaEngine::class.java.getDeclaredField("nativeHandle").apply { isAccessible = true }.getLong(engine)
        }.getOrDefault(0L)
    }
}
