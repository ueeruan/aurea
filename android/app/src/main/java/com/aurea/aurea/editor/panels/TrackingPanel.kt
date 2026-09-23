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
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
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
        Action(stringResource(R.string.panel_rastrear_ponto), stringResource(R.string.panel_cria_ponto_guia_segue_objeto_ligue)) {
            env.onClose()
            store.beginPointPick(false)
        }
        Spacer(Modifier.height(10.dp))
        Action(stringResource(R.string.panel_estabilizar_pelo_ponto), stringResource(R.string.panel_move_video_ponto_ficar_parado_tela)) {
            env.onClose()
            store.beginPointPick(true)
        }
        Spacer(Modifier.height(12.dp))
        Text(
            stringResource(R.string.panel_toque_num_detalhe_contraste_canto_luz),
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
    Text(stringResource(R.string.panel_camera_3d), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(6.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(0 to stringResource(R.string.panel_rapido), 1 to stringResource(R.string.panel_equilibrado), 2 to stringResource(R.string.panel_alta_qualidade)).forEach { (m, label) ->
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
            Action(stringResource(R.string.panel_cancelar), stringResource(R.string.panel_analise_projeto_nao_muda)) { store.cancelCameraTrack() }
        }
        s != null && s.state == 2 -> {
            val kind = if (s.rotationOnly) stringResource(R.string.panel_camera_so_gira_lugar_sem_profundidade) else stringResource(R.string.panel_movimento_camera_encontrado)
            Text(kind + if (s.cached) stringResource(R.string.panel_analise_guardada) else "", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600)))
            Spacer(Modifier.height(4.dp))
            Text(
                "${s.solved} de ${s.frames} quadros · precisão ${(s.confidence * 100).toInt()}% · abertura da lente ${kotlin.math.round(s.fovDeg).toInt()}°",
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
            )
            Spacer(Modifier.height(8.dp))
            Action(stringResource(R.string.panel_criar_camera), stringResource(R.string.panel_cria_camera_3d_animada_ponto_guia)) { store.applyCameraTrack() }
            Spacer(Modifier.height(8.dp))
            Action(stringResource(R.string.panel_analisar_novo), stringResource(R.string.panel_modo_escolhido_acima)) { store.startCameraTrack(mode) }
        }
        else -> {
            if (s != null && (s.state == 3 || s.state == 4)) {
                Text(
                    if (s.state == 4) stringResource(R.string.panel_analise_cancelada) else "Não deu para resolver: ${s.message}",
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
                )
                Spacer(Modifier.height(8.dp))
            }
            Action(stringResource(R.string.panel_analisar_camera), stringResource(R.string.panel_acha_movimento_camera_video_roda_segundo)) { store.startCameraTrack(mode) }
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
