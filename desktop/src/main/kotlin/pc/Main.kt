package com.aurea.pc

import android.content.res.Configuration
import android.util.DisplayMetrics
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.isAltPressed
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.WindowPlacement
import androidx.compose.ui.window.WindowPosition
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import androidx.lifecycle.viewmodel.compose.LocalViewModelStoreOwner
import com.aurea.aurea.MainActivity
import java.io.File

/**
 * O tablet do Aurea PC: a UI enxerga uma tela de iPad Pro 12,9" DEITADO
 * (1366 x 1024 pt), na escala que a janela permitir (ver TabletScreen).
 */
object TabletFrame {
    const val WIDTH_DP = 1366
    const val HEIGHT_DP = 1024
}

fun main(args: Array<String>) {
    // Antes de qualquer janela: a thread da UI e as DLLs do FFmpeg que a aurea.dll usa.
    preloadNativeLibraries()
    PcKeyStore.install()
    PcKeys.install()
    // Mouse na timeline: arrastar o clipe move na hora (no toque continua segurar e arrastar).
    com.aurea.aurea.editor.timeline.TimelinePlatform.mouseDragMovesClips = true
    com.aurea.aurea.ui.platform.PlatformFeatures.desktopAe = true
    // Fonte da interface mais grossa no PC (a padrão do Skia sai fina no Windows).
    com.aurea.aurea.ui.platform.PlatformFeatures.uiFontFamily = PcFonts.uiFamily()
    // Cena 3D na organização do Blender (replace/PcScene3D*.kt): só no PC.
    com.aurea.aurea.ui.platform.PlatformFeatures.desktopScene3d = true
    com.aurea.aurea.editor.installPcScene3D()
    // Timeline do Aurea com o tempo ancorado no início, como no After Effects.
    com.aurea.aurea.editor.timeline.TimelinePlatform.anchorAtStart = true
    // O editor de curva abre na janelinha de Curvas (o painel antigo não aparece no PC).
    com.aurea.aurea.editor.PanelRouting.curveHost = { com.aurea.aurea.editor.PcCurvePopup.open = true }
    // Como no AE: Efeitos ficam no cartão da direita (Controles de efeitos); os outros painéis
    // abrem na janela flutuante do editor (PcPanelWindow).
    com.aurea.aurea.editor.PanelRouting.panelHost = { com.aurea.aurea.editor.PcPanelWindow.route(it) }
    // AUREA_SELFTEST=vault: confere o cofre de chaves (o token da conta) e sai.
    // AUREA_SELFTEST=probe:<arquivo>: o que o MediaMetadataRetriever do PC lê do arquivo.
    System.getenv("AUREA_SELFTEST")?.takeIf { it.startsWith("probe:") }?.let { arg ->
        val f = File(arg.removePrefix("probe:"))
        println("SELFTEST probe exists=${f.isFile} meta=" + PcMedia.probe(f))
        kotlin.system.exitProcess(0)
    }
    if (System.getenv("AUREA_SELFTEST") == "vault") {
        val v = com.aurea.aurea.captions.KeyVault(PcApp.context)
        v.put("selftest", "ok-123")
        val back = v.get("selftest")
        v.remove("selftest")
        println("SELFTEST vault=" + back)
        kotlin.system.exitProcess(if (back == "ok-123") 0 else 1)
    }
    Thread.setDefaultUncaughtExceptionHandler { t, e -> PcLog.write('E', "AureaPC", "exceção em ${t.name}", e) }
    args.firstOrNull { File(it).isFile }?.let { path ->
        PcApp.launchIntent = android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.fromFile(File(path)))
    }
    // O idioma do app (Ajustes) manda no Configuration desde o início, como no attachBaseContext.
    Configuration.current = Configuration().also { base ->
        com.aurea.aurea.ui.i18n.AppLanguage.override(PcApp.context, base)?.let { base.setTo(it) }
        java.util.Locale.setDefault(base.locales[0])
    }
    PcApp.application.onCreate()

    application(exitProcessOnExit = true) {
        val screen = java.awt.GraphicsEnvironment.getLocalGraphicsEnvironment().maximumWindowBounds
        val fit = minOf(screen.width * 0.94f / TabletFrame.WIDTH_DP, (screen.height * 0.94f - 32f) / TabletFrame.HEIGHT_DP)
        val state = rememberWindowState(
            size = DpSize((TabletFrame.WIDTH_DP * fit).dp + 16.dp, (TabletFrame.HEIGHT_DP * fit).dp + 39.dp),
            position = WindowPosition(Alignment.Center),
        )
        val activity = remember { MainActivity().also { it.create() } }

        Window(
            onCloseRequest = {
                activity.onStop()
                activity.destroy()
                PcApp.closing = true
                exitApplication()
            },
            state = state,
            title = "Aurea",
            icon = remember { runCatching { BitmapPainter(PcDrawables.bitmap(com.aurea.aurea.R.mipmap.ic_launcher)) }.getOrNull() },
            // Atalhos AE/Premiere: depois de quem tem o foco (um campo de texto digita primeiro).
            onKeyEvent = { e -> PcShortcuts.handle(e, activity.store) },
            onPreviewKeyEvent = { e ->
                when {
                    // Cena 3D: Esc cancela mover/girar/escalar ou fecha o menu antes de ser "voltar"; Tab é do Blender.
                    com.aurea.aurea.editor.PcScene3DKeys.preview(e, activity.store) -> true
                    e.type == KeyEventType.KeyDown && e.key == Key.Escape -> PcBack.dispatch()
                    e.type == KeyEventType.KeyDown && e.key == Key.F11 -> {
                        state.placement = if (state.placement == WindowPlacement.Fullscreen) WindowPlacement.Floating else WindowPlacement.Fullscreen
                        true
                    }
                    else -> false
                }
            },
        ) {
            LaunchedEffect(Unit) {
                PcApp.window = window
                PcApp.refreshRate = window.graphicsConfiguration?.device?.displayMode?.refreshRate?.takeIf { it > 0 }?.toFloat() ?: 60f
                PcUiThread.mark()
                activity.onStart()
            }
            LaunchedEffect(PcApp.closeRequested) { if (PcApp.closeRequested) { activity.onStop(); activity.destroy(); exitApplication() } }
            LaunchedEffect(PcApp.minimizeRequested) { if (PcApp.minimizeRequested > 0) state.isMinimized = true }
            DisposableEffect(window) {
                val focus = object : java.awt.event.WindowAdapter() {
                    override fun windowIconified(e: java.awt.event.WindowEvent?) = activity.onLeaving()
                    override fun windowDeactivated(e: java.awt.event.WindowEvent?) = activity.onLeaving()
                }
                window.addWindowListener(focus)
                // Botão "voltar" do mouse (4) = voltar do Android.
                val mouse = java.awt.event.AWTEventListener { ev ->
                    if (ev is java.awt.event.MouseEvent && ev.id == java.awt.event.MouseEvent.MOUSE_RELEASED && ev.button == 4) PcBack.dispatch()
                }
                java.awt.Toolkit.getDefaultToolkit().addAWTEventListener(mouse, java.awt.AWTEvent.MOUSE_EVENT_MASK)
                onDispose { window.removeWindowListener(focus); java.awt.Toolkit.getDefaultToolkit().removeAWTEventListener(mouse) }
            }

            TabletScreen {
                key(PcApp.generation) {
                    CompositionLocalProvider(
                        LocalContext provides activity,
                        LocalViewModelStoreOwner provides activity,
                        // Galeria de efeitos: na biblioteca; quando aberta de outro lugar, um cartão (não a tela inteira).
                        com.aurea.aurea.editor.panels.LocalEffectsBrowserHost provides { com.aurea.aurea.editor.PcLibraryState.effects = true },
                        com.aurea.aurea.effects.LocalEffectSheetMaxSize provides DpSize(560.dp, 760.dp),
                        com.aurea.aurea.effects.LocalEffectTapOpensDetail provides true,
                    ) {
                        activity.Content()
                        // A janelinha de Curvas flutua por cima do editor.
                        com.aurea.aurea.editor.PcCurvePopupHost(activity.store)
                    }
                }
            }
        }
    }
}

/**
 * A tela do tablet deitado: a janela inteira, medida como um iPad Pro 12,9"
 * na horizontal. A escala faz 1366 x 1024 pt caberem; o que a janela tiver a
 * mais vira largura (ou altura) a mais, como num tablet maior — diálogos e
 * painéis usam a mesma escala.
 */
@Composable
private fun TabletScreen(content: @Composable () -> Unit) {
    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
        val outer = LocalDensity.current
        val wPx = with(outer) { maxWidth.toPx() }
        val hPx = with(outer) { maxHeight.toPx() }
        val scale = minOf(wPx / TabletFrame.WIDTH_DP, hPx / TabletFrame.HEIGHT_DP).coerceAtLeast(0.25f)
        val wDp = (wPx / scale).toInt()
        val hDp = (hPx / scale).toInt()
        val config = remember(wDp, hDp, scale, PcApp.generation) {
            Configuration(Configuration.current).apply {
                screenWidthDp = wDp
                screenHeightDp = hDp
                smallestScreenWidthDp = minOf(wDp, hDp)
                orientation = if (wDp >= hDp) Configuration.ORIENTATION_LANDSCAPE else Configuration.ORIENTATION_PORTRAIT
                densityDpi = (160 * scale).toInt()
            }.also { c ->
                Configuration.current = c
                DisplayMetrics.current = DisplayMetrics().apply {
                    this.density = scale; scaledDensity = scale; densityDpi = c.densityDpi
                    widthPixels = wPx.toInt(); heightPixels = hPx.toInt()
                    xdpi = 160f * scale; ydpi = 160f * scale
                }
            }
        }
        CompositionLocalProvider(LocalDensity provides Density(scale, 1f), LocalConfiguration provides config) {
            Box(Modifier.fillMaxSize().pointerInput(Unit) {
                // Alt + roda do mouse = zoom da timeline (AE e Premiere), antes de quem está embaixo.
                awaitPointerEventScope {
                    while (true) {
                        val ev = awaitPointerEvent(PointerEventPass.Initial)
                        // Clique fora da janelinha de Curvas: Ctrl+C/V voltam a ser de camadas e keyframes
                        // (a janelinha religa no mesmo clique quando é nela; ver PcCurvePopup.hot).
                        if (ev.type == PointerEventType.Press) com.aurea.aurea.editor.PcCurvePopup.hot = false
                        if (ev.type == PointerEventType.Scroll && ev.keyboardModifiers.isAltPressed) {
                            val dy = ev.changes.firstOrNull()?.scrollDelta?.y ?: 0f
                            if (dy != 0f) com.aurea.aurea.editor.timeline.TimelineZoomRequests.request(if (dy < 0f) 1.2f else 1f / 1.2f)
                            ev.changes.forEach { it.consume() }
                        }
                    }
                }
            }) {
                content()
                Toasts(Modifier.align(Alignment.BottomCenter))
                PcShortcuts.HelpOverlay()
            }
        }
    }
}

@Composable
private fun Toasts(modifier: Modifier) {
    Column(modifier.padding(bottom = 64.dp), verticalArrangement = Arrangement.spacedBy(8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        PcToasts.visible.forEach { t ->
            key(t.id) {
                AnimatedVisibility(true, enter = fadeIn(), exit = fadeOut()) {
                    Text(
                        t.text, color = Color.White, fontSize = 14.sp,
                        modifier = Modifier.widthIn(max = 560.dp)
                            .background(Color(0xE6202830), RoundedCornerShape(20.dp))
                            .padding(horizontal = 18.dp, vertical = 10.dp),
                    )
                }
            }
        }
    }
}

private object PcUiThread {
    fun mark() { android.os.PcUi.uiThread = Thread.currentThread() }
}

/**
 * A aurea.dll importa as DLLs do FFmpeg; o Windows procura importações na
 * pasta do executável (java.exe), não na da DLL. Carregá-las antes, pelo
 * caminho completo, resolve as importações por nome.
 */
private fun preloadNativeLibraries() {
    val dirs = (System.getProperty("java.library.path") ?: "").split(File.pathSeparatorChar).map { File(it) }
    val dir = dirs.firstOrNull { File(it, "aurea.dll").isFile } ?: return
    for (prefix in listOf("avutil-", "swresample-", "swscale-", "avcodec-", "avformat-")) {
        dir.listFiles { f -> f.name.startsWith(prefix) && f.name.endsWith(".dll") }?.firstOrNull()?.let { f ->
            runCatching { System.load(f.absolutePath) }.onFailure { PcLog.write('W', "AureaPC", "não carregou ${f.name}", it) }
        }
    }
}
