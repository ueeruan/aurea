package com.aurea.aurea.home

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/**
 * Omega home · P0-1: a barra superior da Home (57 dp). ☰ (50 × 50) abre a
 * gaveta de ajustes; a marca do AUREA fica centrada na largura toda; à direita
 * as Novidades (o lugar do botão extra da referência) e o avatar da conta.
 *
 * [accountMenu]: com login, o avatar abre um menu ancorado (conectado como,
 * sair, excluir); sem login, [onAccount] abre a tela de entrar.
 */
@Composable
internal fun HomeTopBar(
    onMenu: () -> Unit,
    signedInEmail: String?,
    onAccount: () -> Unit,
    accountMenuOpen: Boolean,
    onDismissAccountMenu: () -> Unit,
    accountMenu: List<MenuEntry>,
) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(OmegaHomeDims.TopBar)
            .background(AureaColors.LightBackground)
            .testTag("home.topBar"),
    ) {
        val menuLabel = stringResource(R.string.home_tab_settings)
        Box(
            Modifier
                .align(Alignment.CenterStart)
                .padding(start = 4.dp)
                .size(50.dp)
                .testTag("home.menu")
                .semantics { contentDescription = menuLabel }
                .tocavel(onClick = onMenu),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.LineHorizontal3, 24.dp, AureaColors.LightText)
        }
        Row(
            Modifier.align(Alignment.Center).testTag("home.logo").semantics(mergeDescendants = true) { heading() },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            AureaLogo(28.dp)
            Spacer(Modifier.width(8.dp))
            Text("AUREA", style = AureaType.of(20f, FontWeight.W700, 1f, AureaColors.LightText), maxLines = 1)
        }
        Row(Modifier.align(Alignment.CenterEnd).padding(end = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            ReleaseNotesEntry()
            Box {
                val accountLabel = stringResource(R.string.om_home_account)
                Box(
                    Modifier
                        .size(44.dp)
                        .testTag("home.account")
                        .semantics { contentDescription = signedInEmail?.let { "$accountLabel · $it" } ?: accountLabel }
                        .tocavel(onClick = onAccount),
                    contentAlignment = Alignment.Center,
                ) {
                    HomeAvatar(signedInEmail)
                }
                AnchoredMenu(
                    expanded = accountMenuOpen,
                    onDismiss = onDismissAccountMenu,
                    entries = accountMenu,
                    alignEnd = true,
                    tag = "home.account.menu",
                )
            }
        }
    }
}

/** Avatar de 40 dp: sem login, a pessoa sobre o cinza; com login, a inicial. */
@Composable
private fun HomeAvatar(email: String?) {
    val initial = email?.trim()?.firstOrNull()?.uppercaseChar()
    if (initial == null) {
        Box(Modifier.size(40.dp).padding(5.dp).clip(CircleShape).background(AureaColors.HomeAvatar), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.PersonCropCircle, 22.dp, AureaColors.HomeOnAvatar)
        }
    } else {
        Box(
            Modifier.size(40.dp).padding(5.dp).clip(CircleShape).background(AureaColors.LightInk),
            contentAlignment = Alignment.Center,
        ) {
            Text(initial.toString(), style = AureaType.of(15f, FontWeight.W700, 0f, AureaColors.LightOnInk), maxLines = 1)
        }
    }
}

/**
 * Omega home · P0-6: o cabeçalho do modo de seleção, no lugar da barra
 * superior (57 dp): "{n} escolhidos" · "Selecionar todos" (extra D8) · "Cancelar".
 */
@Composable
internal fun HomeSelectionHeader(count: Int, onSelectAll: () -> Unit, onCancel: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(OmegaHomeDims.TopBar)
            .background(AureaColors.LightBackground)
            .padding(horizontal = 16.dp, vertical = 4.dp)
            .testTag("home.selection.header"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            stringResource(R.string.home_selected_chosen, count),
            style = OmegaHomeType.ListTitle,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f).testTag("home.selection.count").semantics { heading() },
        )
        HeaderTextButton(
            stringResource(R.string.home_select_all),
            OmegaHomeType.Small.copy(color = AureaColors.LightAccentInk, fontWeight = FontWeight.W500),
            "home.selection.all",
            onSelectAll,
        )
        HeaderTextButton(stringResource(R.string.common_cancel), OmegaHomeType.Action, "home.selection.cancel", onCancel)
    }
}

/** Botão de texto do cabeçalho: alvo de 48 dp de altura, texto centrado. */
@Composable
private fun HeaderTextButton(label: String, style: androidx.compose.ui.text.TextStyle, tag: String, onClick: () -> Unit) {
    Box(
        Modifier
            .heightIn(min = 48.dp)
            .testTag(tag)
            .tocavel(shrink = 1f, onClick = onClick)
            .padding(horizontal = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = style, maxLines = 1)
    }
}
