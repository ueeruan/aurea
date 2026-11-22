@file:Suppress("DEPRECATION") // LocalClipboardManager: o mesmo no Android e no Compose do PC

package com.aurea.aurea.mcp

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.editor.galleryFull
import com.aurea.aurea.editor.galleryPartial
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.text.DateFormat
import java.util.Date

// A paleta do mascote (roxo da arte do AureaMCP).
private val Ink = Color(0xFF0E0B16)
private val Card = Color(0xFF1A1428)
private val Line = Color(0x1FFFFFFF)
private val Violet = Color(0xFFA855F7)
private val Glow = Brush.linearGradient(listOf(Color(0xFF6D28D9), Color(0xFFA855F7)))
private val Online = Color(0xFF4CD964)

private fun style(size: Int, weight: FontWeight = FontWeight.W400, color: Color = Color.White) =
    AureaType.Base.merge(TextStyle(fontSize = size.sp, fontWeight = weight, color = color))

/**
 * AureaMCP em tela cheia (substitui o chat de IA 0.4.6, que volta com
 * [AureaMcpFlags.legacyChat]). Mascote, ligar, status, conectar agente
 * (código), token do Claude Code/Codex, agentes com acesso, atividade,
 * parar/retomar e permissões. Par do iOS: AureaMcpView.swift.
 */
@Composable
fun AureaMcpScreen(mcp: AureaMcp, onClose: () -> Unit) {
    LaunchedEffect(mcp.enabled) { if (mcp.enabled) { mcp.start(); mcp.refreshGrants(); mcp.refreshAureaos() } }
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Column(Modifier.fillMaxSize().background(Ink).statusBarsPadding().navigationBarsPadding().imePadding().testTag("mcp.screen")) {
            Row(Modifier.fillMaxWidth().height(56.dp).padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Mascot(30)
                Text(stringResource(R.string.mcp_title), Modifier.padding(start = 10.dp).weight(1f).semantics { heading() }, style = style(17, FontWeight.W700))
                val close = stringResource(R.string.mcp_close)
                Box(Modifier.size(40.dp).clip(CircleShape).tocavel(role = Role.Button, onClick = onClose).semantics { contentDescription = close }.testTag("mcp.close"),
                    contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.Xmark, 18.dp, AureaColors.Muted) }
            }
            Box(Modifier.fillMaxWidth().height(1.dp).background(Line))
            Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                Hero(mcp)
                if (mcp.enabled && mcp.status != AureaMcp.Status.NoAccount) {
                    ConnectCard(mcp)
                    BackgroundCard(mcp)
                    AureaOsCard(mcp)
                    TokenCard(mcp)
                    AgentsCard(mcp)
                    ActivityCard(mcp)
                    PermissionsCard(mcp)
                    Text(stringResource(R.string.mcp_turn_off), Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).tocavel(role = Role.Button) { mcp.changeEnabled(false) }
                        .padding(vertical = 12.dp).testTag("mcp.turnOff"), style = style(14, FontWeight.W600, AureaColors.Danger).merge(TextStyle(textAlign = androidx.compose.ui.text.style.TextAlign.Center)))
                }
            }
        }
    }
}

@Composable
private fun Mascot(size: Int) {
    Image(painterResource(R.drawable.aureamcp_mascot), contentDescription = null, modifier = Modifier.size(size.dp).clip(RoundedCornerShape((size * 0.23f).dp)))
}

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(Card).border(1.dp, Line, RoundedCornerShape(16.dp)).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(title, Modifier.semantics { heading() }, style = style(15, FontWeight.W700))
        content()
    }
}

@Composable
private fun PrimaryButton(label: String, tag: String, enabled: Boolean = true, onClick: () -> Unit) {
    Box(Modifier.fillMaxWidth().height(46.dp).clip(RoundedCornerShape(12.dp)).background(if (enabled) Glow else SolidColor(Color(0xFF2A2338)))
        .tocavel(role = Role.Button, enabled = enabled, onClick = onClick).testTag(tag), contentAlignment = Alignment.Center) {
        Text(label, style = style(15, FontWeight.W700, if (enabled) Color.White else AureaColors.Muted))
    }
}

@Composable
private fun SecondaryButton(label: String, tag: String, color: Color = Violet, onClick: () -> Unit) {
    Text(label, Modifier.clip(RoundedCornerShape(10.dp)).border(1.dp, Line, RoundedCornerShape(10.dp)).tocavel(role = Role.Button, onClick = onClick)
        .padding(horizontal = 12.dp, vertical = 8.dp).testTag(tag), style = style(13, FontWeight.W600, color))
}

@Composable
private fun Hero(mcp: AureaMcp) {
    Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
        Mascot(104)
        Spacer(Modifier.height(12.dp))
        Text(stringResource(R.string.mcp_tagline), Modifier.widthIn(max = 420.dp), style = style(14, color = AureaColors.Muted).merge(TextStyle(textAlign = androidx.compose.ui.text.style.TextAlign.Center)))
        Spacer(Modifier.height(12.dp))
        if (!mcp.enabled) {
            PrimaryButton(stringResource(R.string.mcp_enable), "mcp.enable") { mcp.changeEnabled(true) }
            return@Column
        }
        val (label, dot) = when (mcp.status) {
            AureaMcp.Status.Online -> stringResource(R.string.mcp_status_online) to Online
            AureaMcp.Status.Connecting -> stringResource(R.string.mcp_status_connecting) to Violet
            AureaMcp.Status.Offline -> stringResource(R.string.mcp_status_offline) to Color(0xFFFFB020)
            AureaMcp.Status.NoAccount -> stringResource(R.string.mcp_status_no_account) to AureaColors.Danger
            AureaMcp.Status.Off -> stringResource(R.string.mcp_status_off) to AureaColors.Muted
        }
        Row(Modifier.clip(RoundedCornerShape(20.dp)).background(Card).padding(horizontal = 12.dp, vertical = 7.dp).testTag("mcp.status"),
            verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(8.dp).clip(CircleShape).background(dot))
            Text(label, Modifier.padding(start = 8.dp), style = style(13))
        }
        if (mcp.status == AureaMcp.Status.Online || mcp.paused) {
            Spacer(Modifier.height(10.dp))
            if (mcp.paused) {
                Text(stringResource(R.string.mcp_paused), Modifier.padding(bottom = 8.dp), style = style(13, FontWeight.W600, Color(0xFFFFB020)))
                PrimaryButton(stringResource(R.string.mcp_resume), "mcp.resume") { mcp.resume() }
            } else {
                SecondaryButton(stringResource(R.string.mcp_stop), "mcp.stop", AureaColors.Danger) { mcp.pause() }
            }
        }
    }
}

@Composable
private fun ConnectCard(mcp: AureaMcp) {
    val clipboard = LocalClipboardManager.current
    var code by remember { mutableStateOf("") }
    var error by remember { mutableStateOf<String?>(null) }
    var request by remember { mutableStateOf<AureaMcp.PairRequest?>(null) }
    // Link aurea://mcp/pair?code=…: o código entra sozinho e já é conferido.
    val linked = AureaMcp.pendingPairCode
    val linkInvalid = stringResource(R.string.mcp_code_invalid)
    LaunchedEffect(linked) {
        if (linked != null) {
            AureaMcp.pendingPairCode = null
            code = linked; error = null; request = null
            mcp.lookupPair(linked) { r, _ -> if (r == null) error = linkInvalid else request = r }
        }
    }
    var done by remember { mutableStateOf<String?>(null) }
    var copied by remember { mutableStateOf(false) }
    val invalid = stringResource(R.string.mcp_code_invalid)
    Section(stringResource(R.string.mcp_connect_title)) {
        Text(stringResource(R.string.mcp_connect_body), style = style(13, color = AureaColors.Muted))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(AureaMcp.SERVER_URL, Modifier.weight(1f), style = style(12, color = Color.White).merge(TextStyle(fontFamily = FontFamily.Monospace)))
            SecondaryButton(stringResource(if (copied) R.string.mcp_copied else R.string.mcp_copy_url), "mcp.copyUrl") {
                clipboard.setText(AnnotatedString(AureaMcp.SERVER_URL)); copied = true
            }
        }
        val pending = request
        if (pending == null) {
            Box(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(Ink).border(1.dp, Line, RoundedCornerShape(12.dp)).padding(12.dp)) {
                BasicTextField(code, { code = it.uppercase().take(12); error = null }, singleLine = true, cursorBrush = SolidColor(Violet),
                    keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
                    textStyle = style(18, FontWeight.W700).merge(TextStyle(fontFamily = FontFamily.Monospace, letterSpacing = 2.sp)),
                    modifier = Modifier.fillMaxWidth().testTag("mcp.code"))
                if (code.isEmpty()) Text(stringResource(R.string.mcp_code_hint), style = style(16, color = AureaColors.Muted))
            }
            error?.let { Text(it, style = style(13, color = AureaColors.Danger)) }
            done?.let { Text(it, style = style(13, FontWeight.W600, Online)) }
            PrimaryButton(stringResource(R.string.mcp_code_check), "mcp.codeCheck", enabled = code.count { it.isLetterOrDigit() } == 8 && !mcp.busy) {
                done = null
                mcp.lookupPair(code) { r, _ -> if (r == null) error = invalid else request = r }
            }
        } else {
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(Ink).padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(stringResource(R.string.mcp_approve_title), style = style(15, FontWeight.W700))
                Text(stringResource(R.string.mcp_approve_body, pending.client, pending.redirectHost), style = style(13, color = AureaColors.Muted))
                val approved = stringResource(R.string.mcp_approved, pending.client)
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Box(Modifier.weight(1f)) {
                        PrimaryButton(stringResource(R.string.mcp_approve), "mcp.approve", enabled = !mcp.busy) {
                            mcp.approvePair(pending.code, true) { ok -> request = null; code = ""; if (ok) done = approved else error = invalid }
                        }
                    }
                    SecondaryButton(stringResource(R.string.mcp_deny), "mcp.deny", AureaColors.Danger) {
                        mcp.approvePair(pending.code, false) { request = null; code = "" }
                    }
                }
            }
        }
        if (mcp.busy) LinearProgressIndicator(Modifier.fillMaxWidth(), color = Violet)
    }
}

@Composable
private fun TokenCard(mcp: AureaMcp) {
    val clipboard = LocalClipboardManager.current
    var created by remember { mutableStateOf<AureaMcp.NewToken?>(null) }
    Section(stringResource(R.string.mcp_token_title)) {
        Text(stringResource(R.string.mcp_token_body), style = style(13, color = AureaColors.Muted))
        val t = created
        if (t == null) {
            val name = stringResource(R.string.mcp_token_title)
            SecondaryButton(stringResource(R.string.mcp_token_create), "mcp.tokenCreate") { mcp.createToken(name) { created = it } }
        } else {
            Text(stringResource(R.string.mcp_token_once), style = style(13, FontWeight.W600, Color(0xFFFFB020)))
            Text(t.token, Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(Ink).padding(10.dp).testTag("mcp.token"),
                style = style(12).merge(TextStyle(fontFamily = FontFamily.Monospace)))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                SecondaryButton(stringResource(R.string.mcp_token_copy_claude), "mcp.copyClaude") { clipboard.setText(AnnotatedString(AureaMcp.claudeCodeCommand(t.token))) }
                SecondaryButton(stringResource(R.string.mcp_token_copy_codex), "mcp.copyCodex") { clipboard.setText(AnnotatedString(AureaMcp.codexConfig(t.token))) }
            }
        }
    }
}

@Composable
private fun AgentsCard(mcp: AureaMcp) {
    var confirmAll by remember { mutableStateOf(false) }
    val fmt = remember { DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT) }
    Section(stringResource(R.string.mcp_agents_title)) {
        if (mcp.grants.isEmpty()) Text(stringResource(R.string.mcp_agents_empty), style = style(13, color = AureaColors.Muted))
        for (g in mcp.grants) {
            val live = mcp.agents.any { it.tokenId == g.id && System.currentTimeMillis() - it.seen < 5 * 60_000 }
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(8.dp).clip(CircleShape).background(if (live) Online else Color(0xFF3A3348)))
                Column(Modifier.padding(start = 10.dp).weight(1f)) {
                    Text(g.name, style = style(14, FontWeight.W600))
                    Text(when {
                        live -> stringResource(R.string.mcp_agent_online)
                        g.lastUsedAt != null -> stringResource(R.string.mcp_agent_last, fmt.format(Date(g.lastUsedAt)))
                        else -> stringResource(R.string.mcp_agent_never)
                    }, style = style(12, color = AureaColors.Muted))
                }
                SecondaryButton(stringResource(R.string.mcp_revoke), "mcp.revoke.${g.id.take(8)}", AureaColors.Danger) { mcp.revoke(g.id) }
            }
        }
        if (mcp.grants.size > 1) SecondaryButton(stringResource(R.string.mcp_revoke_all), "mcp.revokeAll", AureaColors.Danger) { confirmAll = true }
    }
    if (confirmAll) AureaAlert(
        title = stringResource(R.string.mcp_revoke_all), message = stringResource(R.string.mcp_revoke_all_confirm),
        confirmLabel = stringResource(R.string.mcp_revoke_all), destructive = true,
        onConfirm = { confirmAll = false; mcp.revoke(null) }, onDismiss = { confirmAll = false },
    )
}

@Composable
private fun ActivityCard(mcp: AureaMcp) {
    val fmt = remember { DateFormat.getTimeInstance(DateFormat.MEDIUM) }
    Section(stringResource(R.string.mcp_activity_title)) {
        if (mcp.log.isEmpty()) Text(stringResource(R.string.mcp_activity_empty), style = style(13, color = AureaColors.Muted))
        for (line in mcp.log.takeLast(40).asReversed()) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CupertinoIcon(if (line.ok) CupertinoGlyph.CheckmarkCircle else CupertinoGlyph.ExclamationmarkBubble, 13.dp, if (line.ok) Violet else AureaColors.Danger)
                Text(fmt.format(Date(line.at)), Modifier.padding(start = 8.dp).width(72.dp), style = style(11, color = AureaColors.Muted))
                Text(if (line.tool == null) stringResource(R.string.mcp_activity_connected, line.agent) else "${line.agent} · ${line.tool}",
                    style = style(12), maxLines = 1)
            }
        }
    }
}

@Composable
private fun PermissionsCard(mcp: AureaMcp) {
    Section(stringResource(R.string.mcp_perm_title)) {
        val labels = mapOf(
            "read" to R.string.mcp_perm_read, "edit" to R.string.mcp_perm_edit, "projects" to R.string.mcp_perm_projects,
            "delete" to R.string.mcp_perm_delete, "media" to R.string.mcp_perm_media, "export" to R.string.mcp_perm_export,
        )
        for ((cap, res) in labels) ToggleRow(stringResource(res), mcp.permissions[cap] == true, "mcp.perm.$cap") { mcp.setPermission(cap, it) }
        DeviceMediaToggle(mcp)
        ToggleRow(stringResource(R.string.mcp_ask_every), mcp.askEveryEdit, "mcp.askEvery") { mcp.changeAskEveryEdit(it) }
    }
}

/**
 * "Fora do app" (18/11/2026): o agente continua com a pessoa em outro app ou com a
 * tela apagada (McpAgentService) e, com o app fechado, o Worker põe os pedidos na
 * fila e acorda o Aurea por push. Ligar pede a permissão de notificação (Android
 * 13+): as aprovações fora da tela chegam por ela. No PC não aparece (a janela é o app).
 */
private var askedNotify = false

@Composable
private fun BackgroundCard(mcp: AureaMcp) {
    if (com.aurea.aurea.ui.platform.PlatformFeatures.desktopAe) return
    val platform = McpAgentHub.platform ?: return
    var notifyOk by remember { mutableStateOf(platform.notificationsAllowed()) }
    val ask = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { notifyOk = platform.notificationsAllowed() }
    // Ligado por padrão: na primeira vez que a pessoa vê o cartão, o sistema pergunta (uma vez por abertura do app).
    LaunchedEffect(Unit) {
        val perm = platform.notificationPermission()
        if (mcp.keepWorking && !notifyOk && perm != null && !askedNotify) { askedNotify = true; ask.launch(arrayOf(perm)) }
    }
    Section(stringResource(R.string.mcp_bg_title)) {
        Text(stringResource(R.string.mcp_bg_body), style = style(13, color = AureaColors.Muted))
        ToggleRow(stringResource(R.string.mcp_bg_toggle), mcp.keepWorking, "mcp.keepWorking") { on ->
            mcp.changeKeepWorking(on)
            val perm = platform.notificationPermission()
            if (on && perm != null && !platform.notificationsAllowed()) ask.launch(arrayOf(perm))
        }
        if (mcp.keepWorking) {
            val running = McpAgentHub.serviceRunning
            Row(Modifier.testTag("mcp.keepWorking.status"), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(8.dp).clip(CircleShape).background(if (running) Online else Color(0xFF3A3348)))
                Text(stringResource(if (running) R.string.mcp_bg_running else R.string.mcp_bg_ready), Modifier.padding(start = 8.dp),
                    style = style(13, FontWeight.W600, if (running) Online else AureaColors.Muted))
            }
            if (!notifyOk) Text(stringResource(R.string.mcp_bg_notif_denied), Modifier.testTag("mcp.keepWorking.notify"), style = style(12, color = Color(0xFFFFB020)))
            Text(stringResource(R.string.mcp_bg_limits), style = style(12, color = AureaColors.Muted))
        }
    }
}

@Composable
private fun ToggleRow(label: String, checked: Boolean, tag: String, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().semantics { contentDescription = label }, verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f).padding(end = 10.dp), style = style(14))
        AureaToggle(checked, onChange, Modifier.testTag(tag))
    }
}

/**
 * O pedido de confirmação de um agente (apagar projeto, ou toda edição com
 * "Perguntar antes"). Fica na raiz do app: aparece na Home e no editor.
 */
@Composable
fun AureaMcpConfirmHost(mcp: AureaMcp) {
    val c = mcp.confirm ?: return
    val message = if (c.tool == "delete_project") stringResource(R.string.mcp_confirm_delete, c.detail)
        else stringResource(R.string.mcp_confirm_edit, c.tool) + if (c.detail.isNotBlank()) "\n\n" + c.detail.take(300) else ""
    AureaAlert(
        title = stringResource(R.string.mcp_confirm_title, c.agent),
        message = message,
        confirmLabel = stringResource(R.string.mcp_approve),
        cancelLabel = stringResource(R.string.mcp_deny),
        destructive = c.tool == "delete_project",
        // 1.0.6 (par do McpConfirmAlert do iOS): cada botão responde só ao pedido que
        // mostrou; um toque atrasado num diálogo trocado não aprova/recusa o seguinte.
        onConfirm = { mcp.answerConfirm(true, c.id) },
        onDismiss = { mcp.answerConfirm(false, c.id) },
    )
}

/**
 * Raiz do AureaMCP (AureaApp): com ele ligado, conecta na abertura, avisa o
 * relay quando o app volta ao primeiro plano (o aparelho ativo) e quando o
 * projeto aberto muda, e mostra as confirmações em qualquer tela.
 */
@Composable
fun AureaMcpRoot(store: com.aurea.aurea.state.EditorStore) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val wanted = AureaMcp.active || remember { AureaMcp.enabledIn(context) }
    if (!wanted) return
    val mcp = store.mcp
    val owner = androidx.lifecycle.compose.LocalLifecycleOwner.current
    androidx.compose.runtime.DisposableEffect(owner, mcp) {
        val observer = androidx.lifecycle.LifecycleEventObserver { _, event ->
            if (event == androidx.lifecycle.Lifecycle.Event.ON_RESUME) mcp.start()
        }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }
    // start + o estado de primeiro plano/serviço (o editor pode ter nascido depois do onStart).
    LaunchedEffect(mcp) { mcp.start(); mcp.onLifecycleChanged() }
    val title = store.project.title
    val screen = store.screen
    LaunchedEffect(title, screen, mcp.status) { if (mcp.status == AureaMcp.Status.Online) mcp.projectChanged() }
    AureaMcpConfirmHost(mcp)
    McpPermissionHost()
}

/**
 * O link `aurea://mcp/pair?code=…` (MainActivity → [AureaMcp.openPairLink]) abre a
 * tela do AureaMCP em qualquer lugar do app, com o código já preenchido. Fica na
 * raiz (AureaApp), fora do [AureaMcpRoot]: o link funciona com o AureaMCP desligado.
 */
@Composable
fun AureaMcpDeepLinkHost(store: com.aurea.aurea.state.EditorStore) {
    if (!AureaMcp.deepLinkOpen) return
    AureaMcpScreen(store.mcp) { AureaMcp.deepLinkOpen = false }
}

/** O diálogo do sistema para as permissões pedidas por ferramentas ([McpPermissions]). */
@Composable
fun McpPermissionHost() {
    val pending = McpPermissions.pending
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { McpPermissions.answer(it) }
    LaunchedEffect(pending) { if (pending != null) launcher.launch(pending.permissions) }
}

private tailrec fun Context.activity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.activity()
    else -> null
}

/**
 * "Galeria do aparelho": ligar pede a permissão do sistema (Android 13+: fotos e
 * vídeos; 14+: também "só as selecionadas"; até 12: armazenamento). Negada, o
 * interruptor volta a desligado. Acesso parcial: mostra que o agente só vê o que
 * a pessoa escolheu.
 */
@Composable
private fun DeviceMediaToggle(mcp: AureaMcp) {
    val context = LocalContext.current
    var denied by remember { mutableStateOf(false) }
    var limited by remember { mutableStateOf(context.galleryLimitedOnly()) }
    val ask = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        val granted = context.galleryFullAny() || context.galleryPartialAccess()
        denied = !granted
        limited = context.galleryLimitedOnly()
        mcp.setPermission("device_media", granted)
    }
    ToggleRow(stringResource(R.string.mcp_perm_device_media), mcp.permissions["device_media"] == true, "mcp.perm.device_media") { on ->
        if (!on) { mcp.setPermission("device_media", false); denied = false }
        else if (context.galleryFullAny()) { mcp.setPermission("device_media", true); limited = false }
        else ask.launch(com.aurea.aurea.editor.galleryPermissions())
    }
    if (denied) Text(stringResource(R.string.mcp_device_media_denied), style = style(12, color = AureaColors.Danger))
    else if (limited && mcp.permissions["device_media"] == true) Text(stringResource(R.string.mcp_device_media_limited), style = style(12, color = AureaColors.Muted))
}

private fun Context.galleryFullAny() = galleryFull(true) || galleryFull(false)
private fun Context.galleryPartialAccess() = galleryPartial()
private fun Context.galleryLimitedOnly() = !galleryFullAny() && galleryPartialAccess()

/** AureaOS: o método medido do dono, pago (aurea_aureaos). O editor continua livre. */
@Composable
private fun AureaOsCard(mcp: AureaMcp) {
    val activity = LocalContext.current.activity()
    val pc = com.aurea.aurea.ui.platform.PlatformFeatures.desktopAe
    Section(stringResource(R.string.mcp_aureaos_title)) {
        Text(stringResource(R.string.mcp_aureaos_body), style = style(13, color = AureaColors.Muted))
        val active = mcp.aureaosActive
        Row(Modifier.testTag("mcp.aureaos.status"), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(8.dp).clip(CircleShape).background(if (active == true) Online else Color(0xFF3A3348)))
            Text(stringResource(if (active == true) R.string.mcp_aureaos_active else R.string.mcp_aureaos_inactive), Modifier.padding(start = 8.dp),
                style = style(13, FontWeight.W600, if (active == true) Online else AureaColors.Muted))
        }
        if (active == false) {
            when {
                pc -> Text(stringResource(R.string.mcp_aureaos_pc), style = style(12, color = AureaColors.Muted))
                !mcp.aureaosPurchasable -> Text(stringResource(R.string.mcp_aureaos_unavailable), style = style(12, color = AureaColors.Muted))
                else -> {
                    val price = mcp.aureaosPrice
                    PrimaryButton(if (price != null) stringResource(R.string.mcp_aureaos_activate_price, price) else stringResource(R.string.mcp_aureaos_activate),
                        "mcp.aureaos.activate", enabled = !mcp.aureaosBusy && activity != null) { mcp.buyAureaos(activity, restore = false) }
                    SecondaryButton(stringResource(R.string.mcp_aureaos_restore), "mcp.aureaos.restore") { mcp.buyAureaos(activity, restore = true) }
                }
            }
        }
        mcp.aureaosNotice?.let { key ->
            val res = when (key) {
                "mcp_aureaos_thanks" -> R.string.mcp_aureaos_thanks
                "mcp_aureaos_pending" -> R.string.mcp_aureaos_pending
                "mcp_aureaos_nothing_to_restore" -> R.string.mcp_aureaos_nothing_to_restore
                "mcp_aureaos_other_account" -> R.string.mcp_aureaos_other_account
                "mcp_aureaos_invalid" -> R.string.mcp_aureaos_invalid
                else -> R.string.mcp_aureaos_unavailable
            }
            Text(stringResource(res), Modifier.testTag("mcp.aureaos.notice"), style = style(12, FontWeight.W600, if (key == "mcp_aureaos_thanks") Online else Color(0xFFFFB020)))
        }
        if (mcp.aureaosBusy) LinearProgressIndicator(Modifier.fillMaxWidth(), color = Violet)
    }
}
