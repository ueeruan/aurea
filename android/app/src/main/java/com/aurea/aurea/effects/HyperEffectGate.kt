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
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.hyper.HyperAccess
import com.aurea.aurea.hyper.HyperEntitlements
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon

// =============================================================================
//  Os efeitos do pacote AureaHyper são classificados pelo motor compartilhado
//  e aparecem no navegador de efeitos para todo mundo; sem o
//  acesso (HyperEntitlements) eles levam o cadeado e o toque abre a tela
//  bloqueada em vez de pôr o efeito. O par do iOS: EffectsBrowser.swift.
// =============================================================================

internal object HyperEffectGate {
    fun isHyper(typeId: Int): Boolean = AureaEngine.nativeEffectRequiresHyper(typeId)

    /**
     * Lê o estado do portão (Compose recompõe quando muda). 16/11/2026: cada efeito
     * pago tem a sua assinatura (hyper::effect_product_id) e o AureaHyper libera
     * todos; a regra é do motor (HyperEntitlements.accessFor → hyper::has_access).
     */
    fun locked(typeId: Int): Boolean {
        if (!isHyper(typeId)) return false
        return PaidGate.locked(true, HyperEntitlements.accessFor(AureaEngine.nativeEffectProductId(typeId)))
    }

    /**
     * 1.0.2: a opção [value] do parâmetro [paramIndex] é paga (Flow Retime › Neural (IA),
     * hyper::effect_option_requires_access) e a conta não tem o AureaHyper?
     */
    fun optionLocked(typeId: Int, paramIndex: Int, value: Float): Boolean {
        if (!AureaEngine.nativeEffectOptionRequiresHyper(typeId, paramIndex, value)) return false
        return PaidGate.locked(true, HyperEntitlements.accessFor(null))
    }
}

/**
 * A regra do portão, sem ponte nem estado (HyperGateTest): um recurso pago só
 * passa com o acesso confirmado; qualquer outro estado (sem conta, conferindo,
 * expirado, sem config, desligado) bloqueia. O par do iOS: HyperModel.swift.
 */
internal object PaidGate {
    // Um recurso pago que JÁ está no projeto (efeito aplicado, texto 3D criado)
    // nunca é tirado: abre, edita, aparece na prévia e exporta igual, sem marca
    // d'água (o motor não conhece o acesso); sem o acesso ele só leva o cadeado
    // (EffectsPanel.kt: o selo da linha do efeito). Só CRIAR um novo pede o acesso.
    fun locked(paid: Boolean, access: HyperAccess): Boolean = paid && access != HyperAccess.Unlocked
}

/**
 * Consulta a política compartilhada do Texto 3D. As entradas de criação e os
 * presets usam a mesma regra de [EditorStore.addText3D].
 */
internal object Text3DGate {
    fun isPaid(): Boolean = AureaEngine.nativeText3dRequiresHyper()

    /** Só consulta a assinatura quando o motor exige acesso pago. */
    fun locked(): Boolean {
        if (!isPaid()) return false
        return PaidGate.locked(true, HyperEntitlements.accessFor(AureaEngine.nativeText3dProductId()))
    }
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
