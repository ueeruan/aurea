package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/**
 * 0.4.1 (mockup "interface 03"): as pílulas flutuantes no canto sup-dir do palco —
 * grade, o "olho" (esconde contorno e alças) e o zoom "NN%" (tocar volta a 100 %). Vidro
 * escuro de 32 com fio de 8 %; aceso = destaque. Somem com a coluna de modos aberta
 * (ela ocupa o mesmo canto).
 */
@Composable
internal fun StageFloatPills(store: EditorStore, modifier: Modifier) {
    if (ViewModes.columnOpen) return
    val on = stringResource(R.string.common_on)
    val off = stringResource(R.string.common_off)
    Column(modifier.padding(top = 12.dp, end = 10.dp), verticalArrangement = Arrangement.spacedBy(6.dp), horizontalAlignment = Alignment.End) {
        FloatPill("stage.pill.grid", stringResource(R.string.om_vm_grid), if (ViewModes.grid) on else off, ViewModes.grid, {
            ViewModes.grid = !ViewModes.grid
        }) { tint -> CupertinoIcon(CupertinoGlyph.Grid, 16.dp, tint) }
        // O "olho": esconde o contorno e as alças da seleção (aceso = escondido).
        FloatPill("stage.pill.overlay", stringResource(R.string.om_vm_overlay), if (ViewModes.hideOverlay) on else off, ViewModes.hideOverlay, {
            ViewModes.hideOverlay = !ViewModes.hideOverlay
        }) { tint -> CupertinoIcon(if (ViewModes.hideOverlay) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye, 16.dp, tint) }
        val percent = stringResource(R.string.common_percent, StageZoomMath.percent(StageView.zoom))
        FloatPill("stage.pill.zoom", percent, null, StageView.zoom != 1f, { StageView.zoomTo(store, 1f) }, wide = true) { tint ->
            Text(percent, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, fontWeight = FontWeight.W600, color = tint)).merge(AureaType.Tabular))
        }
    }
}

@Composable
private fun FloatPill(
    tag: String,
    label: String,
    state: String?,
    active: Boolean,
    onTap: () -> Unit,
    wide: Boolean = false,
    content: @Composable (Color) -> Unit,
) {
    val shape = RoundedCornerShape(16.dp)
    val tint = if (active) AureaColors.Accent else AureaColors.Muted
    Box(
        Modifier
            .then(if (wide) Modifier.size(width = 48.dp, height = 32.dp) else Modifier.size(32.dp))
            .clip(shape)
            .background(Color(0xD9141416))
            .border(1.dp, if (active) AureaColors.Accent else Color.White.copy(alpha = 0.08f), shape)
            .testTag(tag)
            .semantics {
                contentDescription = label
                role = Role.Button
                if (state != null) stateDescription = state
            }
            .tocavel(haptic = true, onClick = onTap),
        contentAlignment = Alignment.Center,
    ) { content(tint) }
}
