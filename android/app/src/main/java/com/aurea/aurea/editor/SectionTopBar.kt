package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ChevronLeft
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.panelSectionTitle
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/** Fundo da barra da seção aberta (Efeitos.dc.html). */
private val SectionBarFill = Color(0xFF1E2636)

/**
 * A BARRA DA SEÇÃO (redesenho 2026-09-29, Efeitos.dc.html): com uma seção da
 * camada aberta (Efeitos, Transformar…), o topo é só `‹` + o título da seção
 * centrado (17 sp, negrito). O `‹` fecha a seção e volta para as ferramentas da
 * camada (a doca), onde moram de novo nome, vincular, lixeira e `⋯`.
 */
@Composable
internal fun SectionTopBar(store: EditorStore, panel: EditorPanel, onBack: () -> Unit) {
    val title = panelSectionTitle(store, panel)
    val back = stringResource(R.string.pn_back_to_layer_tools)
    Box(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .background(SectionBarFill)
            .testTag("editor.sectionBar"),
    ) {
        Text(
            title,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.align(Alignment.Center).padding(horizontal = 56.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700, color = AureaColors.Text, textAlign = TextAlign.Center)),
        )
        Box(
            Modifier
                .align(Alignment.CenterStart)
                .padding(start = 6.dp)
                .size(44.dp)
                .semantics { contentDescription = back }
                .tocavel(onClick = onBack),
            contentAlignment = Alignment.Center,
        ) {
            Icon(Icons.Rounded.ChevronLeft, contentDescription = null, tint = AureaColors.Text, modifier = Modifier.size(26.dp))
        }
    }
}
