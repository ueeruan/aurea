package com.aurea.aurea.home

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Language
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType

private val LayerSeconds = listOf(2, 3, 5)
private val Themes = listOf("Escuro", "Claro", "Sistema")
private val Engine3D = listOf("Automatico", "Sempre GPU", "Sempre CPU")
private val Quality3D = listOf("Automatica", "Maxima", "Equilibrada", "Leve")

/** Rótulo curto de resolução dos Ajustes (720p, 1080p, 4K). */
private fun shortResolution(r: Int) = when (r) {
    720 -> "720p"
    1080 -> "1080p"
    2160 -> "4K"
    else -> "${r}p"
}

/**
 * Aba Ajustes (`settings_tab.dart@762dbfe`, spec §6.1): listas agrupadas.
 *
 * De verdade: os PADRÕES DE NOVOS PROJETOS (proporção, resolução, fps), que
 * a folha "Novo projeto" usa. O resto não tem recurso por trás no Aurea
 * novo: a linha aparece como na A.01, com o valor padrão de lá, e mexer
 * nela avisa "em breve" em vez de fingir que mudou algo.
 *
 * Fora: o grupo GRÁFICOS ("Desenhar com OpenGL ES") — o motor novo é só
 * Vulkan, a opção não volta — e as notas longas da Cena 3D, que descreviam
 * o motor 3D antigo (GPU/CPU com queda automática).
 */
@Composable
internal fun SettingsTab(store: EditorStore, vm: HomeViewModel, listState: LazyListState, bottomBar: Dp) {
    val soon: (String) -> Unit = { store.comingSoon(it) }
    var keyDialog by remember { mutableStateOf(false) }
    if (keyDialog) GroqKeyDialog(store, onDismiss = { keyDialog = false })
    LazyColumn(
        state = listState,
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(start = 20.dp, top = 12.dp, end = 20.dp, bottom = HomeDims.ListEndSpace + bottomBar - HomeDims.TabBarHeight),
    ) {
        item(key = "titulo") {
            Text("Ajustes", style = AureaType.HeadlineLarge)
            Spacer(Modifier.height(24.dp))
        }
        item(key = "idioma") {
            TileRow(
                leading = { Icon(Icons.Filled.Language, contentDescription = null, tint = AureaColors.Muted) },
                title = "Idioma",
                subtitle = "Português",
                subtitleStyle = HomeType.TileSubtitleMedium,
                trailing = { MaterialChevron() },
                onClick = { soon("Idioma") },
            )
        }
        item(key = "padroes") {
            GroupHeader("Padroes de novos projetos")
            Group {
                SegmentedRow("Proporcao", ProjectPresets.aspects.map { it.key }, vm.defaultAspectKey, { it }, vm::changeDefaultAspect)
                GroupDivider()
                SegmentedRow("Resolucao", ProjectPresets.resolutions, vm.defaultResolution, ::shortResolution, vm::changeDefaultResolution)
                GroupDivider()
                SegmentedRow("Camada nova dura", LayerSeconds, 3, { "$it s" }) { soon("Camada nova dura") }
                GroupDivider()
                SegmentedRow("Quadros por segundo", ProjectPresets.fpsOptions, vm.defaultFps, { "$it" }, vm::changeDefaultFps)
            }
        }
        item(key = "aparencia") {
            Spacer(Modifier.height(26.dp))
            GroupHeader("Aparencia")
            Group { SegmentedRow("Tema", Themes, Themes[0], { it }) { soon("Tema") } }
        }
        item(key = "legendas") {
            Spacer(Modifier.height(26.dp))
            GroupHeader("Legendas")
            Group {
                TapRow(
                    "Chave da Groq",
                    if (store.captions.hasGroqKey) "Configurada — toque para trocar ou remover" else "Não configurada — toque para colocar",
                ) { keyDialog = true }
                GroupNote("A chave fica só neste aparelho, criptografada. O áudio só é enviado à Groq quando você toca em Gerar legendas; sem chave, dá para usar um arquivo SRT.")
            }
        }
        item(key = "exportacao") {
            Spacer(Modifier.height(26.dp))
            GroupHeader("Exportacao")
            Group {
                SwitchRow("Salvar na galeria", "Copia o video exportado para a galeria", checked = true) { soon("Exportação") }
            }
        }
        item(key = "cena3d") {
            Spacer(Modifier.height(26.dp))
            GroupHeader("Cena 3D")
            Group {
                SegmentedRow("Motor 3D", Engine3D, Engine3D[0], { it }) { soon("Cena 3D") }
                GroupDivider()
                SegmentedRow("Qualidade 3D", Quality3D, Quality3D[0], { it }) { soon("Cena 3D") }
                GroupDivider()
                TapRow("Travadas", "O que demorou, medido pelo proprio aparelho") { soon("Travadas") }
            }
        }
        item(key = "geral") {
            Spacer(Modifier.height(26.dp))
            GroupHeader("Geral")
            Group {
                SwitchRow("Vibracao ao interagir", null, checked = true) { soon("Vibração") }
                GroupDivider()
                TapRow("Limpar cache", "Remove arquivos temporarios de preview e render") { store.clearCache() }
            }
        }
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
            androidx.compose.foundation.layout.Column {
                Text("Crie a chave em console.groq.com e cole aqui. Ela fica só neste aparelho.", style = HomeType.Note)
                Spacer(Modifier.height(12.dp))
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
            androidx.compose.foundation.layout.Row {
                if (store.captions.hasGroqKey) {
                    androidx.compose.material3.TextButton(onClick = { store.captions.clearGroqKey(); onDismiss() }) { Text("Remover") }
                }
                androidx.compose.material3.TextButton(onClick = onDismiss) { Text("Cancelar") }
            }
        },
    )
}
