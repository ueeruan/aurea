package com.aurea.aurea.effects

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.hyper.HyperEntitlements
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon

// =============================================================================
//  Os efeitos de camada do AureaHyper (hyper::effect_keys: Hyper Upscale /
//  Motion / Clean) aparecem no navegador de efeitos para todo mundo; sem o
//  acesso (HyperEntitlements) eles levam o cadeado e o toque abre a tela
//  bloqueada em vez de pôr o efeito. O par do iOS: EffectsBrowser.swift.
// =============================================================================

internal object HyperEffectGate {
    val Keys = listOf("aurea.hyper.upscale", "aurea.hyper.motion", "aurea.hyper.clean")
    private val TypeIds: Set<Int> = Keys.map(::effectTypeId).toSet()

    fun isHyper(typeId: Int): Boolean = typeId in TypeIds

    /** Lê o estado do portão (Compose recompõe quando muda). */
    fun locked(typeId: Int): Boolean = isHyper(typeId) && !HyperEntitlements.unlocked
}

/** O cadeado no canto da miniatura de um efeito Hyper bloqueado. */
@Composable
internal fun HyperLockBadge(modifier: Modifier = Modifier) {
    val label = stringResource(R.string.hyper_effect_locked)
    Box(
        modifier.size(20.dp).clip(AureaShape.Circle).background(Color.Black.copy(alpha = 0.6f))
            .testTag("effects.hyper.cadeado").semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) { CupertinoIcon(CupertinoGlyph.LockFill, 12.dp, AureaColors.Accent) }
}
