package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

// Velocidade e Som: o motor novo ainda não tem tempo de clipe nem áudio. A casca
// visual da A.01 fica pronta e TODO toque diz "em breve" — nada finge editar.

/** Duração da camada em "m:ss" (a única coisa real que o painel de velocidade tem). */
private fun clock(frames: Int, fps: Float): String {
    val s = if (fps > 0f) (frames / fps).roundToInt() else 0
    return "${s / 60}:${(s % 60).toString().padStart(2, '0')}"
}

/**
 * TEMPO E VELOCIDADE [A] (`showSpeedSheet`): "1,00x · duração", o que acontece com
 * a barra (Estender início · Cortar início · Cortar fim · Estender fim), a régua
 * entre a tartaruga e a lebre, os atalhos 0,5x / 1x / 2x e os interruptores.
 * O motor toca a 1x — é o único valor verdadeiro, e é o que aparece.
 */
@Composable
internal fun SpeedPanel(env: PanelEnv) {
    val store = env.store
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val frames by remember(store) { derivedStateOf { store.detail?.let { it.endFrame - it.startFrame } ?: 0 } }
    val soon = { store.comingSoon("Velocidade") }
    val media = kind == LayerType.Video.kind || kind == LayerType.Audio.kind
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 14.dp, end = 18.dp, bottom = 24.dp)) {
        if (!media) {
            Text(
                "A velocidade vale para vídeo e áudio. Nas outras camadas, aproxime ou afaste os keyframes para animar mais rápido ou mais devagar.",
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 18.2.sp, color = AureaColors.Muted)),
            )
            return@Column
        }
        Text("1,00x · ${clock(frames, store.project.fps)}", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)))
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip).padding(3.dp)) {
            listOf(
                Triple(CupertinoGlyph.ArrowLeftToLine, "Estender início", false),
                Triple(CupertinoGlyph.Scissors, "Cortar início", true),
                Triple(CupertinoGlyph.Scissors, "Cortar fim", false),
                Triple(CupertinoGlyph.ArrowRightToLine, "Estender fim", false),
            ).forEachIndexed { i, (glyph, label, flip) ->
                val on = i == 3 // padrão da A.01: estender o fim
                Column(
                    Modifier
                        .weight(1f)
                        .height(52.dp)
                        .clip(RoundedCornerShape(9.dp))
                        .background(if (on) AureaColors.AccentDim else androidx.compose.ui.graphics.Color.Transparent)
                        .tocavel(onClick = soon),
                    verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    CupertinoIcon(glyph, 17.dp, if (on) AureaColors.Accent else AureaColors.Text, Modifier.graphicsLayer { scaleX = if (flip) -1f else 1f })
                    Spacer(Modifier.height(4.dp))
                    Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 10.5.sp, color = if (on) AureaColors.Accent else AureaColors.Muted)))
                }
            }
        }
        Spacer(Modifier.height(12.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(CupertinoGlyph.Tortoise, 20.dp, AureaColors.Muted)
            Spacer(Modifier.width(6.dp))
            Box(Modifier.weight(1f).height(40.dp).tocavel(shrink = 1f, onClick = soon)) {
                TickRuler(value = { 1f }, unitsPerDp = 0.01f, active = true, modifier = Modifier.fillMaxSize())
            }
            Spacer(Modifier.width(6.dp))
            CupertinoIcon(CupertinoGlyph.Hare, 20.dp, AureaColors.Muted)
            Spacer(Modifier.width(8.dp))
            ValueBox("1,00x", width = 64.dp, onTap = soon)
        }
        Spacer(Modifier.height(12.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf("0,5x" to false, "1x" to true, "2x" to false).forEach { (label, on) ->
                Box(
                    Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                        .tocavel(onClick = soon)
                        .padding(horizontal = 12.dp, vertical = 6.dp),
                ) {
                    Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        ShellToggle("Manter tom do áudio", soon)
        if (kind == LayerType.Video.kind) {
            ShellToggle("Reverso", soon)
            ShellToggle("Blur proporcional à velocidade", soon)
        }
    }
}

@Composable
private fun ShellToggle(label: String, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        AureaToggle(checked = false, onCheckedChange = { onClick() })
    }
}

/**
 * SOM [A] (`audio_sheet.dart`): "Som" com o nível em dB, Mudo, Volume, Ganho,
 * fades e "Abaixar pela voz". Os números ficam "—" (o motor não publica áudio).
 */
@Composable
internal fun AudioPanel(env: PanelEnv) {
    val store = env.store
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val soon = { store.comingSoon("Som") }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 14.dp, end = 18.dp, bottom = 24.dp)) {
        if (kind != LayerType.Video.kind && kind != LayerType.Audio.kind) {
            Text("Esta camada não tem áudio.", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            return@Column
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(CupertinoGlyph.Speaker2, 18.dp, AureaColors.Accent)
            Spacer(Modifier.width(8.dp))
            Text("Som", style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700)))
            Spacer(Modifier.weight(1f))
            Text("— dB", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)))
        }
        Spacer(Modifier.height(10.dp))
        ShellToggle("Mudo", soon)
        listOf("Volume", "Ganho", "Fade de entrada", "Fade de saída").forEach { label ->
            PropertyCustomRow(label, selected = false, onSelect = soon) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.weight(1f).height(40.dp).tocavel(shrink = 1f, onClick = soon)) {
                        TickRuler(value = { 0f }, unitsPerDp = 1f, active = false, modifier = Modifier.fillMaxSize())
                    }
                    Spacer(Modifier.width(8.dp))
                    ValueBox("—", onTap = soon)
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        Text(
            "O fade é de igual potência: fade reto de volume soa como um buraco no meio.",
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 14.85.sp, color = AureaColors.Muted)),
        )
        Spacer(Modifier.height(14.dp))
        Text("Abaixar pela voz", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700)))
        Spacer(Modifier.height(4.dp))
        Text(
            "A trilha desce quando a voz entra e volta quando ela para — sem desenhar envelope na mão.",
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 14.85.sp, color = AureaColors.Muted)),
        )
        Spacer(Modifier.height(8.dp))
        Text("Não há outra faixa com som no projeto.", style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
    }
}
