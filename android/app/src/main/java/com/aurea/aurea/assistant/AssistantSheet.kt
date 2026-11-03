package com.aurea.aurea.assistant

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
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
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
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
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

private val Ink = Color(0xFF0E0F12)
private val Card = Color(0xFF1A1C21)
private val Line = Color(0x14FFFFFF)
private val Glow = Brush.linearGradient(listOf(Color(0xFF7C5CFF), Color(0xFF2F9BFF), Color(0xFF3DE0FF)))

/** Nome curto de cada ação para o chip no chat (o motor dá o nome da ferramenta). */
@Composable
private fun toolLabel(name: String): String = stringResource(when (name) {
    "get_project_state" -> R.string.ai_tool_state
    "run_ae_script" -> R.string.ai_tool_script
    "list_ae_effects" -> R.string.ai_tool_effects
    "select_layers" -> R.string.ai_tool_select
    "delete_layers" -> R.string.ai_tool_delete
    "undo" -> R.string.ai_tool_undo
    "redo" -> R.string.ai_tool_redo
    "seek" -> R.string.ai_tool_seek
    "playback" -> R.string.ai_tool_playback
    "set_view" -> R.string.ai_tool_view
    "open_panel" -> R.string.ai_tool_panel
    else -> R.string.ai_tool_other
})

/**
 * 0.4.6: o chat do assistente de IA em tela cheia. Sem chave: o cartão que
 * pede a chave do OpenRouter. Com chave: conversa, chips das ações feitas no
 * editor, troca de modelo (só os gratuitos com ferramentas) e sugestões.
 */
@Composable
fun AssistantSheet(chat: AssistantChat, onClose: () -> Unit) {
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Column(Modifier.fillMaxSize().background(Ink).statusBarsPadding().navigationBarsPadding().imePadding().testTag("assistant.sheet")) {
            Header(chat, onClose)
            if (!chat.hasKey) KeyGate(chat) else Conversation(chat, Modifier.weight(1f))
        }
    }
}

@Composable
private fun Header(chat: AssistantChat, onClose: () -> Unit) {
    var menu by remember { mutableStateOf(false) }
    var models by remember { mutableStateOf(false) }
    Row(Modifier.fillMaxWidth().height(56.dp).padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(34.dp).clip(CircleShape).background(Glow), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Sparkles, 17.dp, Color.White)
        }
        Column(Modifier.padding(start = 10.dp).weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.ai_title), style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, fontWeight = FontWeight.W700, color = Color.White)))
                Spacer(Modifier.size(6.dp))
                Text(stringResource(R.string.ai_free), Modifier.clip(RoundedCornerShape(6.dp)).background(Color(0x334CD964)).padding(horizontal = 6.dp, vertical = 1.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, fontWeight = FontWeight.W700, color = Color(0xFF4CD964))))
            }
            if (chat.hasKey) Box {
                val current = chat.models.firstOrNull { it.id == chat.model }?.name ?: chat.model.substringAfter('/').removeSuffix(":free")
                Text("$current  ▾", Modifier.tocavel(role = Role.Button, onClick = { chat.loadModels(); models = true }).semantics {
                    contentDescription = current }, maxLines = 1,
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
                AnchoredMenu(expanded = models, onDismiss = { models = false }, tag = "assistant.models",
                    entries = listOf<MenuEntry>(MenuEntry.Header(stringResource(R.string.ai_model))) + chat.models.map { m ->
                        MenuEntry.Item(m.name, sublabel = m.id, selected = m.id == chat.model, checked = m.id == chat.model) { chat.chooseModel(m.id) }
                    })
            }
        }
        if (chat.hasKey) Box {
            HeaderButton(CupertinoGlyph.Ellipsis, stringResource(R.string.ai_model)) { menu = true }
            AnchoredMenu(expanded = menu, onDismiss = { menu = false }, alignEnd = true, tag = "assistant.menu", entries = listOf(
                MenuEntry.Item(stringResource(R.string.ai_new_chat)) { chat.clear() },
                MenuEntry.Item(stringResource(R.string.ai_change_key), destructive = true) { chat.forgetKey() },
            ))
        }
        HeaderButton(CupertinoGlyph.Xmark, stringResource(R.string.ai_close), onClose)
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(Line))
}

@Composable
private fun HeaderButton(glyph: Char, label: String, onClick: () -> Unit) {
    Box(Modifier.size(40.dp).clip(CircleShape).tocavel(role = Role.Button, onClick = onClick).semantics { contentDescription = label },
        contentAlignment = Alignment.Center) { CupertinoIcon(glyph, 18.dp, AureaColors.Muted) }
}

@Composable
private fun KeyGate(chat: AssistantChat) {
    var key by remember { mutableStateOf("") }
    val uri = LocalUriHandler.current
    val invalid = stringResource(R.string.ai_key_invalid)
    val offline = stringResource(R.string.ai_offline)
    Column(Modifier.fillMaxWidth().padding(20.dp)) {
        Box(Modifier.size(56.dp).clip(RoundedCornerShape(16.dp)).background(Glow), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Sparkles, 28.dp, Color.White)
        }
        Spacer(Modifier.height(16.dp))
        Text(stringResource(R.string.ai_key_title), style = AureaType.Base.merge(TextStyle(fontSize = 20.sp, fontWeight = FontWeight.W700, color = Color.White)))
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.ai_key_body), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)))
        Spacer(Modifier.height(16.dp))
        for (step in listOf(R.string.ai_key_step1, R.string.ai_key_step2, R.string.ai_key_step3)) {
            Text(stringResource(step), Modifier.padding(vertical = 3.dp), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = Color.White)))
        }
        Spacer(Modifier.height(10.dp))
        Text(stringResource(R.string.ai_key_open), Modifier.clip(RoundedCornerShape(10.dp)).tocavel(role = Role.Button) {
            uri.openUri("https://openrouter.ai/keys") }.padding(vertical = 8.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
        Spacer(Modifier.height(12.dp))
        Box(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(Card).border(1.dp, Line, RoundedCornerShape(14.dp)).padding(14.dp)) {
            BasicTextField(key, { key = it }, singleLine = true, visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                cursorBrush = SolidColor(AureaColors.Accent), modifier = Modifier.fillMaxWidth().testTag("assistant.key"),
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = Color.White)))
            if (key.isEmpty()) Text(stringResource(R.string.ai_key_hint), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
        }
        chat.keyError?.let { Text(it, Modifier.padding(top = 8.dp), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Danger))) }
        Spacer(Modifier.height(14.dp))
        val enabled = key.isNotBlank() && !chat.busy
        Box(Modifier.fillMaxWidth().height(50.dp).clip(RoundedCornerShape(14.dp)).background(if (enabled) Glow else SolidColor(Card))
            .tocavel(role = Role.Button, onClick = { if (enabled) chat.submitKey(key, invalid, offline) }).testTag("assistant.unlock"),
            contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.ai_key_unlock), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W700,
                color = if (enabled) Color.White else AureaColors.Muted)))
        }
        if (chat.busy) LinearProgressIndicator(Modifier.fillMaxWidth().padding(top = 10.dp), color = AureaColors.Accent)
    }
}

@Composable
private fun Conversation(chat: AssistantChat, modifier: Modifier) {
    LaunchedEffect(Unit) { chat.loadModels() }
    var draft by remember { mutableStateOf("") }
    val list = rememberLazyListState()
    LaunchedEffect(chat.lines.size, chat.status) { if (chat.lines.isNotEmpty()) list.animateScrollToItem(chat.lines.size) }
    val labels = AssistantChat.Labels(
        thinking = stringResource(R.string.ai_thinking), done = stringResource(R.string.ai_done),
        offline = stringResource(R.string.ai_offline), keyRejected = stringResource(R.string.ai_key_rejected),
        tool = toolNames(), switched = switchedLabel(),
    )
    fun send(text: String) { chat.send(text, labels); draft = "" }
    Column(modifier) {
        LazyColumn(Modifier.weight(1f).fillMaxWidth(), state = list, contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            if (chat.lines.isEmpty()) item { Welcome(::send) }
            itemsIndexed(chat.lines) { _, line -> Bubble(line) }
            item {
                chat.status?.let { s ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(8.dp).clip(CircleShape).background(Glow))
                        Text(s, Modifier.padding(start = 8.dp), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
                    }
                }
            }
        }
        Row(Modifier.fillMaxWidth().padding(12.dp).clip(RoundedCornerShape(24.dp)).background(Card).border(1.dp, Line, RoundedCornerShape(24.dp))
            .padding(start = 16.dp, end = 6.dp, top = 6.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).padding(vertical = 8.dp)) {
                BasicTextField(draft, { draft = it }, maxLines = 5, cursorBrush = SolidColor(AureaColors.Accent),
                    modifier = Modifier.fillMaxWidth().testTag("assistant.input"),
                    textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = Color.White)))
                if (draft.isEmpty()) Text(stringResource(R.string.ai_placeholder), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
            }
            val label = stringResource(if (chat.busy) R.string.ai_stop else R.string.ai_send)
            val can = chat.busy || draft.isNotBlank()
            Box(Modifier.size(40.dp).clip(CircleShape).background(if (can) Glow else SolidColor(Color(0xFF2A2D33)))
                .tocavel(role = Role.Button, onClick = { if (chat.busy) chat.stop() else if (draft.isNotBlank()) send(draft) })
                .semantics { contentDescription = label }.testTag("assistant.send"), contentAlignment = Alignment.Center) {
                CupertinoIcon(if (chat.busy) CupertinoGlyph.Xmark else CupertinoGlyph.ArrowUp, 18.dp, Color.White)
            }
        }
    }
}

@Composable
private fun toolNames(): (String) -> String {
    val names = listOf("get_project_state", "run_ae_script", "list_ae_effects", "select_layers", "delete_layers", "undo", "redo",
        "seek", "playback", "set_view", "open_panel", "").associateWith { toolLabel(it) }
    return { names[it] ?: names.getValue("") }
}

@Composable
private fun switchedLabel(): (String) -> String {
    val format = stringResource(R.string.ai_switched)
    return { format.replace("%1\$s", it) }
}

@Composable
private fun Welcome(onPick: (String) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(top = 12.dp)) {
        Text(stringResource(R.string.ai_welcome), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = Color.White)))
        Spacer(Modifier.height(14.dp))
        val suggestions = listOf(R.string.ai_suggest_1, R.string.ai_suggest_2, R.string.ai_suggest_3, R.string.ai_suggest_4).map { stringResource(it) }
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            suggestions.forEach { s ->
                Text(s, Modifier.widthIn(max = 220.dp).clip(RoundedCornerShape(14.dp)).background(Card).border(1.dp, Line, RoundedCornerShape(14.dp))
                    .tocavel(role = Role.Button) { onPick(s) }.padding(horizontal = 12.dp, vertical = 10.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = Color.White)))
            }
        }
    }
}

@Composable
private fun Bubble(line: ChatLine) {
    when (line.role) {
        ChatLine.Role.User -> Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.CenterEnd) {
            Text(line.text, Modifier.widthIn(max = 300.dp).clip(RoundedCornerShape(18.dp, 18.dp, 4.dp, 18.dp)).background(Glow).padding(horizontal = 14.dp, vertical = 10.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = Color.White)))
        }
        ChatLine.Role.Assistant -> Row(verticalAlignment = Alignment.Top) {
            Box(Modifier.size(26.dp).clip(CircleShape).background(Glow), contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.Sparkles, 13.dp, Color.White) }
            Text(line.text, Modifier.padding(start = 8.dp).widthIn(max = 320.dp).clip(RoundedCornerShape(4.dp, 18.dp, 18.dp, 18.dp)).background(Card)
                .padding(horizontal = 14.dp, vertical = 10.dp), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = Color.White)))
        }
        ChatLine.Role.Tool -> Row(Modifier.padding(start = 34.dp).clip(RoundedCornerShape(10.dp)).background(Color(0xFF15171B)).padding(horizontal = 10.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(if (line.failed) CupertinoGlyph.ExclamationmarkBubble else CupertinoGlyph.WandStars, 13.dp,
                if (line.failed) AureaColors.Danger else AureaColors.Accent)
            Text(line.text, Modifier.padding(start = 6.dp), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        }
        ChatLine.Role.Error -> Text(line.text, Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(Color(0x22E5252A)).padding(12.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Danger)))
    }
}
