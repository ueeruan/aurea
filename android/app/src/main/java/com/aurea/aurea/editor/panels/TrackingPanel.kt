package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
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
        Action("Rastrear um ponto", "Cria um Nulo que segue o ponto — ligue textos e formas a ele.") {
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
