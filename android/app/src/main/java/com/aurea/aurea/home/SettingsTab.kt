package com.aurea.aurea.home

import android.content.Context
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Language
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import kotlinx.coroutines.launch
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.core.content.pm.PackageInfoCompat
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon

private val LayerSeconds = listOf(2, 3, 5)
private val Engine3D = listOf("Automático", "Sempre GPU", "Sempre CPU")
private val Quality3D = listOf("Automática", "Máxima", "Equilibrada", "Leve")

/** Rótulo curto de resolução (720p, 1080p, 4K). */
private fun shortResolution(r: Int) = when (r) {
    720 -> "720p"
    1080 -> "1080p"
    1440 -> "1440p"
    2160 -> "4K"
    else -> "${r}p"
}

/** Versão real do APK. */
private class AppVersion(val name: String, val build: Long)

private fun readVersion(context: Context): AppVersion = try {
    val pm = context.packageManager
    val info: PackageInfo = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        pm.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
    } else {
        @Suppress("DEPRECATION")
        pm.getPackageInfo(context.packageName, 0)
    }
    AppVersion(info.versionName ?: "?", PackageInfoCompat.getLongVersionCode(info))
} catch (_: Exception) {
    AppVersion("?", 0)
}

/**
 * A ABA AJUSTES (Fase 7.3 §71): o que o app realmente configura, mais o Sobre
 * no fim.
 *
 * Reorganizada depois que Comunidade e Perfil saíram: os grupos agora são
 * Padrões de novos projetos · Legendas · Exportação · Cena 3D · Geral · Sobre.
 * As linhas que só existiam para parecer opção (Idioma, Tema, Motor 3D) saíram
 * — eram botões que avisavam "em breve".
 *
 * De verdade: proporção/resolução/fps padrão (a folha "Novo projeto" usa),
 * a chave da Groq das legendas, limpar cache e as ferramentas de
 * desenvolvedor (sete toques na versão).
 */
@Composable
internal fun SettingsTab(store: EditorStore, vm: HomeViewModel, listState: LazyListState, bottomBar: Dp) {
    var keyDialog by remember { mutableStateOf(false) }
    val context = LocalContext.current
    val version = remember { readVersion(context) }
    var taps by remember { mutableIntStateOf(0) }
    if (keyDialog) GroqKeyDialog(store, onDismiss = { keyDialog = false })

    LazyColumn(
        state = listState,
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(start = AureaDims.Gutter, top = AureaDims.S3, end = AureaDims.Gutter, bottom = AureaDims.ListEndSpace + bottomBar - AureaDims.TabBarHeight),
    ) {
        item(key = "titulo") {
            Text("Ajustes", style = AureaType.HeadlineLarge)
            Spacer(Modifier.height(AureaDims.S5))
        }
        item(key = "padroes") {
            GroupHeader("Padrões de novos projetos")
            Group {
                SegmentedRow("Proporção", ProjectPresets.aspects.map { it.key }, vm.defaultAspectKey, { it }, vm::changeDefaultAspect)
                GroupDivider()
                SegmentedRow("Resolução", ProjectPresets.resolutions, vm.defaultResolution, ::shortResolution, vm::changeDefaultResolution)
                GroupDivider()
                SegmentedRow("Quadros por segundo", ProjectPresets.fpsOptions, vm.defaultFps, { "$it" }, vm::changeDefaultFps)
            }
            GroupNote("É o que a folha \"Novo projeto\" já traz escolhido.")
        }
        item(key = "legendas") {
            Spacer(Modifier.height(AureaDims.S5))
            GroupHeader("Legendas")
            Group {
                TapRow(
                    "Chave da Groq",
                    if (store.captions.hasGroqKey) "Configurada — toque para trocar ou remover" else "Não configurada — toque para colocar",
                ) { keyDialog = true }
                GroupNote("A chave fica só neste aparelho, cifrada. O áudio só vai à Groq quando você toca em Gerar legendas; sem chave, dá para usar um arquivo SRT.")
            }
        }
        item(key = "cena3d") {
            Spacer(Modifier.height(AureaDims.S5))
            GroupHeader("Cena 3D")
            Group {
                SegmentedRow("Qualidade 3D", Quality3D, Quality3D[0], { it }) { store.showToast("Qualidade 3D: em breve no Aurea novo") }
            }
            GroupNote("A cena 3D usa a GPU do aparelho. O motor não tem caminho de CPU — por isso não há \"Sempre CPU\".")
        }
        item(key = "aparelho") {
            Spacer(Modifier.height(AureaDims.S5))
            GroupHeader("Este aparelho")
            val rep = store.deviceReport
            Group {
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.Bolt, 21.dp, AureaColors.Accent) },
                    title = store.deviceName.ifEmpty { "Medindo o aparelho…" },
                    subtitle = rep?.summary() ?: "O motor ainda está subindo",
                )
                if (rep != null) {
                    GroupDivider()
                    TileRow(
                        leading = { CupertinoIcon(CupertinoGlyph.Film, 21.dp, AureaColors.Muted) },
                        title = "Prévia até ${rep.maxPreviewHeight}p · exporta até ${rep.maxExportHeight}p",
                        subtitle = "Textura máx. ${rep.maxTexture} px · ${rep.workers} tarefas em paralelo",
                    )
                }
                GroupDivider()
                TapRow("Medir o aparelho de novo", "Refaz a otimização automática na próxima abertura") { store.remeasureDevice() }
            }
            GroupNote("Na primeira vez que abre, o Aurea mede este celular (memória, GPU e codecs de vídeo) e ajusta sozinho a qualidade da prévia, o uso de memória e o limite de exportação. Fica guardado; só mede de novo se o sistema for atualizado.")
        }
        item(key = "armazenamento") {
            Spacer(Modifier.height(AureaDims.S5))
            StorageSection(store)
        }
        item(key = "geral") {
            Spacer(Modifier.height(AureaDims.S5))
            GroupHeader("Geral")
            Group {
                TapRow("Limpar recentes de efeitos", "A lista de \"Recentes\" do navegador de efeitos") {
                    store.effectPrefs.clearRecents()
                    store.showToast("Recentes de efeitos limpos")
                }
            }
        }
        item(key = "sobre") {
            Spacer(Modifier.height(AureaDims.S5))
            GroupHeader("Sobre")
            Group {
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.Bolt, 21.dp, AureaColors.Accent) },
                    title = "Tecnologia",
                    subtitle = "Jetpack Compose + motor C++ / Vulkan",
                )
                GroupDivider()
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.PersonCropCircle, 21.dp, AureaColors.Accent) },
                    title = "Criador",
                    subtitle = "Ruanzitwo  ·  @ofruanzitwo  ·  TikTok @ruanzitwo",
                )
            }
            Spacer(Modifier.height(AureaDims.S4))
            BetaBanner(version, onTap = {
                taps++
                if (taps >= 7) {
                    taps = 0
                    val on = vm.toggleDevTools()
                    store.showToast(if (on) "Ferramentas de desenvolvedor ligadas" else "Ferramentas de desenvolvedor desligadas")
                }
            })
            if (vm.devTools) {
                Spacer(Modifier.height(AureaDims.S4))
                Group {
                    TileRow(
                        leading = { CupertinoIcon(CupertinoGlyph.Wrench, 21.dp, AureaColors.Accent) },
                        title = "Ferramentas de desenvolvedor",
                        subtitle = "Versão ${version.name} · build ${version.build}",
                    )
                }
            }
            Spacer(Modifier.height(AureaDims.S5))
            Text("Feito por Ruanzitwo", style = AureaType.Footer, textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth())
        }
    }
}

/** Um tamanho legível: "0 KB", "812 KB", "3,4 MB", "1,2 GB". */
private fun sizeLabel(bytes: Long): String = when {
    bytes < 1024L * 1024L -> "${(bytes + 1023) / 1024} KB"
    bytes < 1024L * 1024L * 1024L -> "%.1f MB".format(bytes / (1024.0 * 1024.0))
    else -> "%.2f GB".format(bytes / (1024.0 * 1024.0 * 1024.0))
}

/** O que a tela Armazenamento mostra: disco por tipo + memória do motor agora. */
private class StorageSnapshot(
    val kinds: List<com.aurea.aurea.state.CacheKind>,
    val thumbsRam: Long,
    val waveRam: Long,
    val framesRam: Long,
    val engineRam: Long,
)

/**
 * AJUSTES › ARMAZENAMENTO (Fase 8B §49–51). Tamanhos REAIS, lidos do disco e
 * do motor quando a seção aparece e depois de cada limpeza. Cada linha limpa
 * só o próprio tipo; "Limpar tudo" limpa todos. Nada disso é projeto: os
 * projetos, presets, fontes e modelos importados não aparecem aqui porque não
 * são apagáveis por esta tela.
 */
@Composable
private fun StorageSection(store: EditorStore) {
    val scope = androidx.compose.runtime.rememberCoroutineScope()
    var snapshot by remember { mutableStateOf<StorageSnapshot?>(null) }
    var refresh by remember { mutableIntStateOf(0) }
    var busy by remember { mutableStateOf(false) }
    androidx.compose.runtime.LaunchedEffect(refresh, store.engineReady) {
        snapshot = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
            val mem = store.engineMemory()
            fun used(i: Int) = mem?.getOrNull(i * 2) ?: 0L
            StorageSnapshot(
                kinds = store.storage.scan(store.exporter.busy),
                thumbsRam = used(com.aurea.aurea.engine.AureaEngine.MEM_THUMBNAILS) + store.uiThumbnailBytes(),
                waveRam = used(com.aurea.aurea.engine.AureaEngine.MEM_WAVEFORMS),
                framesRam = used(com.aurea.aurea.engine.AureaEngine.MEM_DECODED_FRAMES),
                engineRam = (0 until com.aurea.aurea.engine.AureaEngine.MEMORY_CLASSES).sumOf { used(it) },
            )
        }
    }
    fun clear(id: String?) {
        if (busy) return
        busy = true
        scope.launch {
            val freed = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                if (id == null) store.clearCache() else store.clearStorage(id)
            }
            busy = false
            if (id != null) store.showToast("Liberado: ${sizeLabel(freed)}")
            refresh++
        }
    }

    GroupHeader("Armazenamento")
    val snap = snapshot
    Group {
        if (snap == null) {
            TileRow(
                leading = { CupertinoIcon(CupertinoGlyph.Film, 21.dp, AureaColors.Muted) },
                title = "Medindo…",
                subtitle = "Lendo o tamanho de cada tipo de cache",
            )
        } else {
            val diskTotal = snap.kinds.sumOf { it.bytes }
            snap.kinds.forEachIndexed { i, k ->
                if (i > 0) GroupDivider()
                TapRow(
                    "${k.title} · ${sizeLabel(k.bytes)}",
                    if (k.bytes > 0) "${k.detail}. Toque para limpar." else "${k.detail}. Vazio.",
                ) { if (k.bytes > 0) clear(k.id) }
            }
            GroupDivider()
            TapRow(
                "Na memória agora · ${sizeLabel(snap.engineRam + store.uiThumbnailBytes())}",
                "Miniaturas ${sizeLabel(snap.thumbsRam)} · waveform ${sizeLabel(snap.waveRam)} · quadros de vídeo ${sizeLabel(snap.framesRam)}. Toque para soltar.",
            ) { clear(EditorStore.MEMORY) }
            GroupDivider()
            TapRow(
                if (busy) "Limpando…" else "Limpar tudo · ${sizeLabel(diskTotal)} no disco",
                "Todo o cache acima, no disco e na memória",
            ) { clear(null) }
        }
    }
    GroupNote("Só cache: tudo aqui se refaz sozinho quando for preciso. Projetos, presets, fontes e modelos importados nunca são apagados por esta tela. Quando um tipo passa do limite, o Aurea apaga sozinho os arquivos mais antigos.")
}

/**
 * A faixa beta com a versão. Sete toques ligam as ferramentas de
 * desenvolvedor — é o único jeito de chegar nelas.
 */
@Composable
private fun BetaBanner(version: AppVersion, onTap: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clip(AureaShape.Md)
            .background(AureaColors.BetaFill)
            .border(1.dp, AureaColors.BetaBorder, AureaShape.Md)
            .clickable(interactionSource = null, indication = null, onClick = onTap)
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.ExclamationmarkTriangle, AureaDims.IconMd, AureaColors.Beta)
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Text("Versão beta para testes · ${version.name}", style = AureaType.BetaTitle)
            Spacer(Modifier.height(3.dp))
            Text(
                "Pode ter erros, travar ou perder alterações não salvas. Achou um problema? Toque aqui sete vezes para abrir as ferramentas de desenvolvedor.",
                style = AureaType.BetaBody,
            )
        }
        Spacer(Modifier.width(AureaDims.S2))
        CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Beta)
    }
}

/** Colar/trocar/remover a chave da Groq (guardada cifrada, nunca mostrada de volta). */
@Composable
private fun GroqKeyDialog(store: EditorStore, onDismiss: () -> Unit) {
    var key by remember { mutableStateOf("") }
    androidx.compose.material3.AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Chave da Groq") },
        text = {
            Column {
                Text("Crie a chave em console.groq.com e cole aqui. Ela fica só neste aparelho.", style = AureaType.Note)
                Spacer(Modifier.height(AureaDims.S3))
                androidx.compose.material3.OutlinedTextField(
                    value = key,
                    onValueChange = { key = it },
                    singleLine = true,
                    placeholder = { Text("gsk_…") },
                    visualTransformation = androidx.compose.ui.text.input.PasswordVisualTransformation(),
                )
            }
        },
        confirmButton = {
            androidx.compose.material3.TextButton(enabled = key.isNotBlank(), onClick = { store.captions.setGroqKey(key); onDismiss() }) { Text("Salvar") }
        },
        dismissButton = {
            Row {
                if (store.captions.hasGroqKey) {
                    androidx.compose.material3.TextButton(onClick = { store.captions.clearGroqKey(); onDismiss() }) { Text("Remover") }
                }
                androidx.compose.material3.TextButton(onClick = onDismiss) { Text("Cancelar") }
            }
        },
    )
}
