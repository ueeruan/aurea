package com.aurea.aurea.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.MODEL_QUALITY_BALANCED
import com.aurea.aurea.engine.MODEL_QUALITY_ORIGINAL
import com.aurea.aurea.engine.ModelPlan
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType

/**
 * "Modelo pesado": o motor mediu o arquivo e o Original não cabe (ou é denso
 * demais para o preview). Três escolhas, a recomendada já marcada; o Original
 * só aparece quando cabe. A conta (o que cabe, quantos triângulos ficam) é do
 * motor — aqui só se mostra.
 */
@Composable
fun ModelOptimizeDialog(store: EditorStore, req: EditorStore.ModelOptimizeRequest) {
    val plan = req.plan
    val thousand = stringResource(R.string.model3d_thousand)
    val million = stringResource(R.string.model3d_million)
    val tris = ModelPlan.shortCount(plan.triangles, thousand, million).let { if (plan.exact) it else "~$it" }
    val quality = store.modelOptimizeQuality
    AureaAlert(
        title = stringResource(R.string.model3d_heavy_title),
        message = stringResource(R.string.model3d_heavy_body, tris),
        confirmLabel = stringResource(if (quality == MODEL_QUALITY_ORIGINAL) R.string.model3d_import else R.string.model3d_optimize_import),
        onConfirm = { store.confirmModelOptimize(req) },
        onDismiss = { store.dismissModelOptimize() },
        extra = {
            Column(
                Modifier.fillMaxWidth().padding(top = 10.dp).selectableGroup(),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                for (q in plan.offered()) {
                    val kept = ModelPlan.shortCount(plan.keptTriangles(q), thousand, million)
                    val hint = when (q) {
                        MODEL_QUALITY_ORIGINAL -> stringResource(R.string.model3d_quality_original_hint)
                        MODEL_QUALITY_BALANCED -> stringResource(R.string.model3d_quality_balanced_hint, kept, plan.textureCap(q))
                        else -> stringResource(R.string.model3d_quality_light_hint, kept, plan.textureCap(q))
                    }
                    QualityRow(
                        label = stringResource(
                            when (q) {
                                MODEL_QUALITY_ORIGINAL -> R.string.model3d_quality_original
                                MODEL_QUALITY_BALANCED -> R.string.model3d_quality_balanced
                                else -> R.string.model3d_quality_light
                            },
                        ),
                        hint = hint,
                        recommended = q == plan.recommended,
                        selected = q == quality,
                        onSelect = { store.modelOptimizeQuality = q },
                    )
                }
            }
        },
    )
}

@Composable
private fun QualityRow(label: String, hint: String, recommended: Boolean, selected: Boolean, onSelect: () -> Unit) {
    val badge = stringResource(R.string.model3d_recommended)
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 48.dp)
            .clip(RoundedCornerShape(9.dp))
            .background(if (selected) Color(0xFF3A3A3C) else Color(0xFF1C1C1E))
            // Leitor de tela: "Equilibrado, recomendado, …, selecionado" (papel de botão de opção).
            .selectable(selected = selected, role = Role.RadioButton, onClick = onSelect)
            .padding(horizontal = 10.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .size(18.dp)
                .clip(CircleShape)
                .border(1.5.dp, if (selected) AureaColors.Accent else Color(0xFF8E8E93), CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            if (selected) Box(Modifier.size(9.dp).clip(CircleShape).background(AureaColors.Accent))
        }
        Column(Modifier.padding(start = 10.dp).weight(1f)) {
            Text(
                if (recommended) "$label · $badge" else label,
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp)),
            )
            Text(hint, style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = Color(0xFF8E8E93))))
        }
    }
}
