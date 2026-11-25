package com.aurea.aurea.hyper

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import com.aurea.aurea.R
import com.aurea.aurea.conta.ContaScreen
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
//  O PAYWALL das assinaturas (16/11/2026). Abre ao tocar num efeito pago sem o
//  acesso e ao criar Texto 3D sem o acesso. 1.0.9 (decisão do dono, 25/11/2026):
//  três planos do AureaHyper, todos liberam tudo (preço da loja quando carrega):
//    · Semanal — R$ 7,00/semana
//    · Mensal  — R$ 30,00/mês
//    · Anual   — R$ 150,00/ano, destacado ("Melhor valor" e, com os preços da
//      loja carregados, "Economize N%" contra um ano do plano curto mais barato)
//  e "Restaurar compras". Sem conta: entrar primeiro (a assinatura fica ligada à
//  conta Aurea). As mesmas tags no iOS (AureaSubscriptions.swift, PaywallSheet):
//    assinatura · assinatura.fechar · assinatura.semanal · assinatura.mensal ·
//    assinatura.hyper (o anual) · assinatura.economia · assinatura.restaurar ·
//    assinatura.entrar · assinatura.aviso
// =============================================================================

/** A folha do paywall para [item]. [itemName] = o nome do efeito (null = o nome do tipo). */
@Composable
fun PaywallDialog(item: PaidItem, itemName: String?, onDismiss: () -> Unit) {
    val conta: ContaViewModel = viewModel()
    val context = LocalContext.current
    var signingIn by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        AureaSubscriptions.clearNotice()
        conta.atualizarAcessos()
        AureaSubscriptions.loadPrices(context)
    }
    LaunchedEffect(conta.logado) { if (conta.logado) { signingIn = false; conta.atualizarAcessos() } }
    val access = HyperEntitlements.accessFor(item.product, subscriptionOnly = item.subscriptionOnly)
    LaunchedEffect(access) { if (access == HyperAccess.Unlocked) onDismiss() }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding().testTag("assinatura")) {
            TextButton(onClick = onDismiss, modifier = Modifier.align(Alignment.End).testTag("assinatura.fechar")) {
                Text(stringResource(R.string.editor_fechar))
            }
            if (signingIn) Box(Modifier.weight(1f)) { ContaScreen(conta) }
            else Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 18.dp)) {
                PaywallContent(item, itemName, access, onSignIn = { signingIn = true },
                    onRetry = { conta.atualizarAcessos(retryPurchases = true) })
                Spacer(Modifier.height(24.dp))
            }
        }
    }
}

@Composable
private fun PaywallContent(item: PaidItem, itemName: String?, access: HyperAccess, onSignIn: () -> Unit, onRetry: () -> Unit) {
    val title = itemName ?: when (item.kind) {
        PaidItem.Kind.Text3D -> stringResource(R.string.sh_add_text_3d)
        else -> stringResource(R.string.hyper_title)
    }
    Spacer(Modifier.height(8.dp))
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(44.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Accent.copy(alpha = 0.18f)),
            contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.LockFill, 22.dp, AureaColors.Accent) }
        Spacer(Modifier.width(12.dp))
        Column {
            Text(title, style = AureaType.TitleLarge, modifier = Modifier.semantics { heading() })
            Text(stringResource(R.string.assinatura_badge), style = AureaType.of(13f, FontWeight.W600, color = AureaColors.Accent))
        }
    }
    Spacer(Modifier.height(14.dp))
    Text(stringResource(when {
        item.product == null -> R.string.assinatura_intro_hyper
        item.kind == PaidItem.Kind.Text3D -> R.string.assinatura_intro_texto3d
        else -> R.string.assinatura_intro_efeito
    }), style = AureaType.of(15f))
    Spacer(Modifier.height(18.dp))
    when (access) {
        HyperAccess.SignedOut -> {
            StatusBox(stringResource(R.string.assinatura_entrar))
            Spacer(Modifier.height(14.dp))
            PaywallButton(stringResource(R.string.conta_entrar), "assinatura.entrar", onClick = onSignIn)
        }
        // A config remota manda (sem config em dia / desligado), como no painel do AureaHyper.
        HyperAccess.NeedsConfig -> {
            StatusBox(stringResource(R.string.hyper_needs_internet))
            Spacer(Modifier.height(14.dp))
            PaywallButton(stringResource(R.string.home_retry), "assinatura.verificar", primary = false) {
                com.aurea.aurea.config.AppConfigStore.refresh(force = true)
                onRetry()
            }
        }
        HyperAccess.Off -> {
            val language = androidx.compose.ui.platform.LocalConfiguration.current.locales[0].language
            StatusBox(com.aurea.aurea.config.AppConfigStore.config?.messageFor(language) ?: stringResource(R.string.hyper_off_default))
        }
        HyperAccess.Checking -> {
            StatusBox(stringResource(R.string.hyper_locked_checking))
            Spacer(Modifier.height(12.dp))
            LinearProgressIndicator(Modifier.fillMaxWidth(), color = AureaColors.Accent, trackColor = AureaColors.Surface)
        }
        else -> SubscriptionOptions(item)
    }
}

/** Como a tela mostra um plano do AureaHyper (o id vem do motor). */
private data class HyperPlan(val id: String, val title: Int, val per: Int, val fallback: Int, val tag: String, val best: Boolean)

private fun hyperPlan(id: String, annual: String?): HyperPlan = when {
    id == annual -> HyperPlan(id, R.string.assinatura_plano_anual, R.string.assinatura_por_ano, R.string.assinatura_preco_hyper,
        "assinatura.hyper", best = true)
    id.endsWith("_semanal") -> HyperPlan(id, R.string.assinatura_plano_semanal, R.string.assinatura_por_semana,
        R.string.assinatura_preco_semanal, "assinatura.semanal", best = false)
    else -> HyperPlan(id, R.string.assinatura_plano_mensal, R.string.assinatura_por_mes, R.string.assinatura_preco_mensal,
        "assinatura.mensal", best = false)
}

/**
 * Os planos do AureaHyper à venda (semanal, mensal, anual; todos liberam tudo) e
 * "Restaurar compras". Também aparece no painel bloqueado do AureaHyper.
 */
@Composable
fun SubscriptionOptions(item: PaidItem) {
    val context = LocalContext.current
    val activity = context.findActivity()
    LaunchedEffect(Unit) { AureaSubscriptions.loadPrices(context) }
    val annual = AureaSubscriptions.hyperProduct()
    val plans = remember(annual) { AureaSubscriptions.plansOnSale().map { hyperPlan(it, annual) } }
    val purchasable = HyperEntitlements.purchasable
    val busy = AureaSubscriptions.busy
    val prices = AureaSubscriptions.prices
    // 1.0.9: semanal, mensal e anual; os avulsos não se vendem mais (quem já
    // tinha um continua com ele: hyper::has_access). O selo só com os preços da loja.
    val savings = AureaSubscriptions.annualSavingsPercent(
        plans.firstOrNull { it.tag == "assinatura.semanal" }?.id,
        plans.firstOrNull { it.tag == "assinatura.mensal" }?.id,
        plans.firstOrNull { it.best }?.id)
    if (plans.isNotEmpty()) {
        Text(stringResource(R.string.assinatura_escolha_plano), style = AureaType.of(13f, FontWeight.W600, color = AureaColors.Muted))
        Spacer(Modifier.height(10.dp))
    }
    plans.forEachIndexed { i, plan ->
        if (i > 0) Spacer(Modifier.height(10.dp))
        val price = stringResource(plan.per, prices[plan.id] ?: stringResource(plan.fallback))
        OptionCard(
            title = stringResource(plan.title),
            price = price,
            badge = if (plan.best) stringResource(R.string.assinatura_melhor_valor) else null,
            savings = if (plan.best && savings > 0) stringResource(R.string.assinatura_economize, savings) else null,
            tag = plan.tag, highlighted = plan.best, busy = busy == plan.id,
            enabled = busy == null && activity != null && plan.id in purchasable,
        ) { activity?.let { AureaSubscriptions.buy(it, plan.id) } }
    }
    if (purchasable.isEmpty()) {
        Spacer(Modifier.height(12.dp))
        Text(stringResource(R.string.assinatura_em_breve), style = AureaType.of(13f, color = AureaColors.Muted))
    }
    Spacer(Modifier.height(14.dp))
    PaywallButton(stringResource(R.string.assinatura_restaurar), "assinatura.restaurar", primary = false,
        enabled = busy == null, busy = busy == AureaSubscriptions.RESTORING) { AureaSubscriptions.restore(context, silent = false) }
    AureaSubscriptions.notice?.let { res ->
        Spacer(Modifier.height(12.dp))
        val text = stringResource(res)
        Text(text, style = AureaType.of(14f, FontWeight.W600),
            modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Surface).padding(14.dp)
                .testTag("assinatura.aviso").semantics { liveRegion = LiveRegionMode.Polite })
    }
    Spacer(Modifier.height(14.dp))
    Text(stringResource(R.string.assinatura_renovacao, "Google Play"), style = AureaType.of(12f, color = AureaColors.Muted))
}

@Composable
private fun OptionCard(title: String, price: String, badge: String?, savings: String?, tag: String, highlighted: Boolean,
                       busy: Boolean, enabled: Boolean, onClick: () -> Unit) {
    val processing = stringResource(R.string.assinatura_processando)
    val shape = RoundedCornerShape(14.dp)
    val spoken = listOfNotNull(title, badge, price, savings).joinToString(", ")
    Row(
        Modifier.fillMaxWidth().heightIn(min = 64.dp).clip(shape)
            .background(if (highlighted) AureaColors.Accent.copy(alpha = 0.14f) else AureaColors.Surface)
            .border(if (highlighted) 1.5.dp else 1.dp, if (highlighted) AureaColors.Accent else AureaColors.Border, shape)
            .testTag(tag)
            .semantics(mergeDescendants = true) {
                role = Role.Button
                contentDescription = spoken
                if (busy) stateDescription = processing
            }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, style = AureaType.of(16f, FontWeight.W700, color = if (enabled || busy) AureaColors.Text else AureaColors.Disabled))
                if (badge != null) {
                    Spacer(Modifier.width(8.dp))
                    Text(badge, style = AureaType.of(11f, FontWeight.W700, color = AureaColors.OnAccent),
                        modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(AureaColors.Accent)
                            .padding(horizontal = 6.dp, vertical = 2.dp))
                }
            }
            Spacer(Modifier.height(2.dp))
            Text(price, style = AureaType.of(14f, FontWeight.W600,
                color = if (!enabled && !busy) AureaColors.Disabled else if (highlighted) AureaColors.Accent else AureaColors.Muted))
            if (savings != null) {
                Spacer(Modifier.height(2.dp))
                Text(savings, style = AureaType.of(13f, FontWeight.W700, color = AureaColors.Accent),
                    modifier = Modifier.testTag("assinatura.economia"))
            }
        }
        if (busy) CircularProgressIndicator(Modifier.size(20.dp), color = AureaColors.Accent, strokeWidth = 2.dp)
        else CupertinoIcon(if (highlighted) CupertinoGlyph.StarFill else CupertinoGlyph.CheckmarkAlt, 18.dp,
            if (enabled) AureaColors.Accent else AureaColors.Disabled)
    }
}

@Composable
private fun StatusBox(text: String) {
    Text(text, style = AureaType.of(14f, FontWeight.W600),
        modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Surface).padding(14.dp))
}

@Composable
private fun PaywallButton(label: String, tag: String, primary: Boolean = true, enabled: Boolean = true, busy: Boolean = false,
                          onClick: () -> Unit) {
    val processing = stringResource(R.string.assinatura_processando)
    Box(
        Modifier.fillMaxWidth().heightIn(min = 48.dp).clip(RoundedCornerShape(12.dp))
            .background(when { !enabled && !busy -> AureaColors.Surface; primary -> AureaColors.Accent; else -> AureaColors.Chip })
            .testTag(tag).semantics {
                role = Role.Button
                contentDescription = label
                if (busy) stateDescription = processing
            }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        if (busy) CircularProgressIndicator(Modifier.size(20.dp), color = AureaColors.Accent, strokeWidth = 2.dp)
        else Text(label, style = AureaType.of(15f, FontWeight.W600,
            color = when { !enabled -> AureaColors.Disabled; primary -> AureaColors.OnAccent; else -> AureaColors.Text }))
    }
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}
