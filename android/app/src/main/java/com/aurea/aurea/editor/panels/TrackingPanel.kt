package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/**
 * RASTREIO do vídeo: escolher um ponto (o próximo toque no palco) e seguir o
 * detalhe pelo clipe — um Nulo que acompanha, ou o vídeo estabilizado.
 */
@Composable
internal fun TrackingPanel(env: PanelEnv) {
    val store = env.store
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        Action("Rastrear um ponto", "Cria um ponto guia que segue o objeto — ligue textos e formas a ele.") {
            env.onClose()
            store.beginPointPick(false)
        }
        Spacer(Modifier.height(10.dp))
        Action("Estabilizar pelo ponto", "Move o vídeo para o ponto ficar parado na tela.") {
            env.onClose()
            store.beginPointPick(true)
        }
        Spacer(Modifier.height(12.dp))
        Text(
            "Toque num detalhe com contraste (canto, luz, marca). Depois de escolher, o rastreio roda sozinho.",
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
        )
        Spacer(Modifier.height(18.dp))
        CameraTrackSection(env)
    }
}

/**
 * CÂMERA 3D: analisa o movimento da câmera do vídeo (pontos, rastreio,
 * solve) em segundo plano e cria uma câmera 3D animada — modelos 3D ligados
 * ao Nulo da cena ficam presos no vídeo.
 */
@Composable
private fun CameraTrackSection(env: PanelEnv) {
    val store = env.store
    val st by remember(store) { derivedStateOf { store.cameraTrack } }
    var mode by remember { mutableIntStateOf(1) }
    Text("Câmera 3D", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(6.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(0 to "Rápido", 1 to "Equilibrado", 2 to "Alta qualidade").forEach { (m, label) ->
            val on = mode == m
            Box(
                Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(onClick = { mode = m }).padding(horizontal = 10.dp, vertical = 6.dp),
            ) {
                Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
    Spacer(Modifier.height(8.dp))
    val s = st
    when {
        s != null && s.state == 1 -> {
            Text("Analisando o movimento… ${(s.progress * 100).toInt()}%", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            Spacer(Modifier.height(6.dp))
            Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(AureaColors.Chip)) {
                Box(Modifier.fillMaxWidth(s.progress.coerceIn(0f, 1f)).fillMaxHeight().background(AureaColors.Accent))
            }
            Spacer(Modifier.height(8.dp))
            Action("Cancelar", "Para a análise; o projeto não muda.") { store.cancelCameraTrack() }
        }
        s != null && s.state == 2 -> {
            val kind = if (s.rotationOnly) "A câmera só gira no lugar — sem profundidade" else "Movimento da câmera encontrado"
            Text(kind + if (s.cached) " (análise guardada)" else "", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600)))
            Spacer(Modifier.height(4.dp))
            Text(
                "${s.solved} de ${s.frames} quadros · precisão ${(s.confidence * 100).toInt()}% · abertura da lente ${kotlin.math.round(s.fovDeg).toInt()}°",
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
            )
            Spacer(Modifier.height(8.dp))
            Action("Criar câmera", "Cria a câmera 3D animada e um ponto guia no chão da cena.") { store.applyCameraTrack() }
            Spacer(Modifier.height(8.dp))
            Action("Analisar de novo", "Com o modo escolhido acima.") { store.startCameraTrack(mode) }
        }
        else -> {
            if (s != null && (s.state == 3 || s.state == 4)) {
                Text(
                    if (s.state == 4) "Análise cancelada." else "Não deu para resolver: ${s.message}",
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
                )
                Spacer(Modifier.height(8.dp))
            }
            Action("Analisar câmera", "Acha o movimento da câmera do vídeo (roda em segundo plano).") { store.startCameraTrack(mode) }
        }
    }
}

@Composable
private fun Action(title: String, detail: String, onClick: () -> Unit) {
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
        Spacer(Modifier.height(2.dp))
        Text(detail, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    }
}
