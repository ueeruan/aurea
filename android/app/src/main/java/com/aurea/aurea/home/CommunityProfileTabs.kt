package com.aurea.aurea.home

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
// Comunidade
// =============================================================================

/**
 * Aba Comunidade (`community_tab.dart@762dbfe`) SEM o mural: o serviço da
 * comunidade não existe no Aurea novo. Fica o visual de quem ainda não tem
 * conta — título, publicar, a fila de criadores com "Criar conta" e o mural
 * vazio — e todo toque avisa "em breve".
 */
@Composable
internal fun CommunityTab(store: EditorStore, listState: LazyListState, bottomBar: Dp) {
    val soon = { store.comingSoon("Comunidade") }
    LazyColumn(
        state = listState,
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(top = 6.dp, bottom = HomeDims.ListEndSpace + bottomBar - HomeDims.TabBarHeight),
    ) {
        item(key = "titulo") {
            Row(
                Modifier.fillMaxWidth().padding(start = 20.dp, top = 8.dp, end = 12.dp, bottom = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("Comunidade", style = HomeType.CommunityTitle, modifier = Modifier.weight(1f))
                Box(
                    Modifier.size(44.dp).semantics { contentDescription = "Publicar" }.clickable(interactionSource = null, indication = null, onClick = soon),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.PlusApp, 25.dp, AureaColors.Text)
                }
                Box(
                    Modifier.size(44.dp).clickable(interactionSource = null, indication = null, onClick = soon),
                    contentAlignment = Alignment.Center,
                ) {
                    InitialAvatar(32.dp, HomeType.CommunityAvatar)
                }
            }
        }
        item(key = "criadores") {
            Row(Modifier.fillMaxWidth().height(102.dp).padding(horizontal = 14.dp)) {
                CreateAccountBubble(soon)
            }
        }
        item(key = "linha") {
            Box(Modifier.fillMaxWidth().height(0.5.dp).background(AureaColors.Hairline))
            Spacer(Modifier.height(10.dp))
        }
        item(key = "vazio") {
            Column(
                Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 40.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                CupertinoIcon(CupertinoGlyph.Person2, 42.dp, AureaColors.Muted.copy(alpha = 0.6f))
                Spacer(Modifier.height(14.dp))
                Text("O mural ainda está vazio", style = HomeType.EmptyTitle)
                Spacer(Modifier.height(6.dp))
                Text(
                    "Seja quem começa. Toque em Publicar e mostre o que você fez no Aurea.",
                    style = HomeType.EmptyBody,
                    textAlign = TextAlign.Center,
                )
            }
        }
    }
}

/** Avatar sem conta: círculo #A9D3EC com "?" (inicial escura, contraste — §8.9). */
@Composable
private fun InitialAvatar(size: Dp, style: androidx.compose.ui.text.TextStyle) {
    Box(Modifier.size(size).clip(CircleShape).background(AureaColors.Keyframe), contentAlignment = Alignment.Center) {
        Text("?", style = style)
    }
}

/** `_Criador` "mais": aro #1B2530, avatar 52 e o selo "+" no destaque. */
@Composable
private fun CreateAccountBubble(onClick: () -> Unit) {
    Column(
        Modifier.width(76.dp).clickable(interactionSource = null, indication = null, onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.height(6.dp))
        Box(Modifier.size(64.dp)) {
            Box(
                Modifier.size(64.dp).clip(CircleShape).background(AureaColors.SurfaceHigh).padding(2.5.dp)
                    .clip(CircleShape).background(AureaColors.Background).padding(2.5.dp),
                contentAlignment = Alignment.Center,
            ) {
                InitialAvatar(52.dp, HomeType.CreatorAvatar)
            }
            Box(
                Modifier
                    .align(Alignment.BottomEnd)
                    .offset(x = 1.dp, y = 1.dp)
                    .size(22.dp)
                    .clip(CircleShape)
                    .background(AureaColors.Background)
                    .padding(2.dp)
                    .clip(CircleShape)
                    .background(AureaColors.Accent),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Plus, 12.dp, AureaColors.OnAccent)
            }
        }
        Spacer(Modifier.height(6.dp))
        Text("Criar conta", style = HomeType.CreatorLabel, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

// =============================================================================
// Perfil
// =============================================================================

/**
 * Aba Perfil (`user_tab.dart@762dbfe`) no estado "sem conta": o cartão de
 * criar conta / entrar com código. A conta da comunidade não existe no
 * Aurea novo — os campos e botões avisam "em breve" (não aceitam texto que
 * não iria a lugar nenhum). A pílula "Cloudflare KV" saiu: anunciava uma
 * conexão que não há. Alternar Criar/Entrar é só apresentação e funciona.
 */
@Composable
internal fun ProfileTab(store: EditorStore, listState: LazyListState, bottomBar: Dp) {
    val soon = { store.comingSoon("Conta da comunidade") }
    var enter by rememberSaveable { mutableStateOf(false) }
    LazyColumn(
        state = listState,
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(start = 20.dp, top = 12.dp, end = 20.dp, bottom = HomeDims.ListEndSpace + bottomBar - HomeDims.TabBarHeight),
    ) {
        item(key = "titulo") {
            Text("Perfil", style = AureaType.HeadlineLarge)
            Spacer(Modifier.height(20.dp))
        }
        item(key = "conta") {
            Column(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(20.dp))
                    .background(AureaColors.Surface)
                    .border(1.dp, AureaColors.Hairline, RoundedCornerShape(20.dp))
                    .padding(20.dp),
            ) {
                Row(
                    Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(14.dp))
                        .background(BannerBrush)
                        .border(1.dp, AureaColors.Accent.copy(alpha = 0.25f), RoundedCornerShape(14.dp))
                        .padding(14.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(
                        Modifier.size(40.dp).clip(CircleShape).background(AureaColors.Accent.copy(alpha = 0.2f)),
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(CupertinoGlyph.CloudFill, 22.dp, AureaColors.Accent)
                    }
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Text("Conta Oficial do Criador", style = HomeType.BannerTitle)
                        Spacer(Modifier.height(2.dp))
                        Text("Publique projetos no mural e sincronize com a nuvem.", style = HomeType.BannerBody)
                    }
                }
                Spacer(Modifier.height(20.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    ModeToggle("Criar Conta", selected = !enter) { enter = false }
                    ModeToggle("Entrar com Código", selected = enter) { enter = true }
                }
                Spacer(Modifier.height(20.dp))
                if (!enter) {
                    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                        Box(Modifier.size(76.dp).clickable(interactionSource = null, indication = null, onClick = soon)) {
                            Box(
                                Modifier
                                    .size(76.dp)
                                    .clip(CircleShape)
                                    .background(AureaColors.SurfaceHigh)
                                    .border(2.dp, AureaColors.Accent.copy(alpha = 0.5f), CircleShape),
                                contentAlignment = Alignment.Center,
                            ) {
                                Text("?", style = HomeType.ProfileInitial)
                            }
                            Box(
                                Modifier.align(Alignment.BottomEnd).clip(CircleShape).background(AureaColors.Accent).padding(5.dp),
                            ) {
                                CupertinoIcon(CupertinoGlyph.CameraFill, 13.dp, AureaColors.OnAccent)
                            }
                        }
                    }
                    Spacer(Modifier.height(16.dp))
                    StaticField(CupertinoGlyph.At, "Seu apelido criativo (ex: Pedro Motion)", soon)
                    Spacer(Modifier.height(8.dp))
                    Text("Este apelido é único e assinará seus vídeos e templates no mural.", style = HomeType.FieldNote)
                    Spacer(Modifier.height(20.dp))
                    ProfileButton("Criar Minha Conta no Cloudflare", soon)
                } else {
                    StaticField(CupertinoGlyph.LockShield, "Código de acesso (48 dígitos)", soon)
                    Spacer(Modifier.height(8.dp))
                    Text("Cole o código de 48 caracteres gerado na criação da sua conta.", style = HomeType.FieldNote)
                    Spacer(Modifier.height(20.dp))
                    ProfileButton("Restaurar Minha Conta", soon)
                }
            }
        }
    }
}

private val BannerBrush = Brush.horizontalGradient(
    listOf(AureaColors.Accent.copy(alpha = 0.15f), AureaColors.Keyframe.copy(alpha = 0.15f)),
)

@Composable
private fun RowScope.ModeToggle(label: String, selected: Boolean, onClick: () -> Unit) {
    val shape = RoundedCornerShape(10.dp)
    Box(
        Modifier
            .weight(1f)
            .clip(shape)
            .background(if (selected) AureaColors.SurfaceHigh else Color.Transparent)
            .then(if (selected) Modifier.border(1.dp, AureaColors.Accent.copy(alpha = 0.4f), shape) else Modifier)
            .clickable(interactionSource = null, indication = null, onClick = onClick)
            .padding(vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = HomeType.ToggleLabel, color = if (selected) AureaColors.Accent else AureaColors.Muted)
    }
}

/** O campo preenchido do Material (fundo #1B2530, raio 12, ícone à esquerda) — só o visual. */
@Composable
private fun StaticField(glyph: Char, placeholder: String, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(56.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.SurfaceHigh)
            .clickable(interactionSource = null, indication = null, onClick = onClick)
            .padding(start = 15.dp, end = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(glyph, 18.dp, AureaColors.Accent)
        Spacer(Modifier.width(15.dp))
        Text(placeholder, style = HomeType.FieldPlaceholder, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
private fun ProfileButton(label: String, onClick: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Accent)
            .tocavel(shrink = 1f, onClick = onClick)
            .padding(vertical = 14.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = HomeType.ProfileButton)
    }
}
