package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R

/**
 * Miniatura dos modos de máscara da mescla (Alight Motion): Máscara mostra a
 * IMAGEM só dentro do disco (fora, o xadrez da transparência); Excluir mostra
 * a imagem com o disco virado furo (o xadrez aparece dentro dele).
 * `exclude` = false → Máscara (BlendMode::Mask 22), true → Excluir (23).
 */
@Composable
internal fun BlendCutThumb(exclude: Boolean) {
    val desc = stringResource(if (exclude) R.string.pn_blend_exclude_desc else R.string.pn_blend_mask_desc)
    Canvas(Modifier.size(34.dp, 22.dp).semantics { contentDescription = desc }) {
        val r = size.height * 0.40f
        val disc = Path().apply {
            addOval(androidx.compose.ui.geometry.Rect(Offset(size.width / 2f, size.height / 2f), r))
        }
        if (exclude) {
            drawCutPicture()
            clipPath(disc) { drawCutChecker() }
        } else {
            drawCutChecker()
            clipPath(disc) { drawCutPicture() }
        }
    }
}

/** Xadrez cinza da transparência (casas de ~4,4 dp). */
private fun DrawScope.drawCutChecker() {
    val cell = size.height / 5f
    drawRect(Color(0xFFBDBDBD))
    var y = 0
    while (y * cell < size.height) {
        var x = 0
        while (x * cell < size.width) {
            if ((x + y) % 2 == 0) drawRect(Color(0xFF8A8A8A), Offset(x * cell, y * cell), Size(cell, cell))
            x++
        }
        y++
    }
}

/** "Foto" esquemática: céu em degradê, sol e um morro. */
private fun DrawScope.drawCutPicture() {
    drawRect(Brush.verticalGradient(listOf(Color(0xFF3D9BFF), Color(0xFFFFC27A))))
    drawCircle(Color(0xFFFFE066), size.height * 0.16f, Offset(size.width * 0.72f, size.height * 0.32f))
    val hill = Path().apply {
        moveTo(0f, size.height)
        lineTo(0f, size.height * 0.70f)
        quadraticTo(size.width * 0.35f, size.height * 0.38f, size.width * 0.70f, size.height * 0.72f)
        lineTo(size.width, size.height * 0.62f)
        lineTo(size.width, size.height)
        close()
    }
    drawPath(hill, Color(0xFF2E9E5B))
}
