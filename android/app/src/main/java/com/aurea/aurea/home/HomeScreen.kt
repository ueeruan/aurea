package com.aurea.aurea.home

import android.view.HapticFeedbackConstants
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.windowInsetsTopHeight
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaElevation
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon

/** Uma aba da barra: rótulo (do catálogo), ícone inativo e ativo. */
private class HomeTab(val label: Int, val icon: Char, val active: Char)

/**
 * As três abas da Home. Comunidade e Perfil saíram (§2); a aba Efeitos saiu
 * depois — ela mostrava o catálogo sem NADA para aplicar, um espelho só de
 * leitura do navegador que já existe dentro do editor, onde o toque aplica.
 * Fora do editor não há camada selecionada, então não há o que fazer com o
 * efeito: o navegador vive onde ele serve para alguma coisa.
 */
private val Tabs = listOf(
    HomeTab(R.string.home_tab_start, CupertinoGlyph.House, CupertinoGlyph.HouseFill),
    HomeTab(R.string.home_tab_projects, CupertinoGlyph.RectangleStack, CupertinoGlyph.RectangleStack),
    HomeTab(R.string.home_tab_settings, CupertinoGlyph.SliderHorizontal3, CupertinoGlyph.SliderHorizontal3),
)

/**
 * A Home: a casca com a barra de 3 abas translúcida e o conteúdo rolando por
 * baixo dela.
 *
 * - Só a aba atual fica composta; a rolagem de cada uma mora no
 *   [HomeViewModel] e volta igual ao trocar de aba ou voltar do editor.
 * - O recuo da status bar entra UMA vez, aqui. A status bar leva o véu
 *   #0B0F13 do desenho aprovado.
 */
@Composable
fun HomeScreen(store: EditorStore) {
    val vm: HomeViewModel = viewModel()
    // Uma chamada por aba (nada de laço): cada estado tem o seu lugar fixo na composição.
    val home = rememberTabListState(vm, HomeViewModel.HOME_TAB)
    val projects = rememberTabListState(vm, HomeViewModel.PROJECTS_TAB)
    val settings = rememberTabListState(vm, HomeViewModel.SETTINGS_TAB)
    val shellBackdrop = rememberBackdrop()
    val view = LocalView.current
    val navBottom = with(LocalDensity.current) { WindowInsets.navigationBars.getBottom(this).toDp() }
    // Altura total da barra de abas: hairline + 54 + a barra de gestos.
    val bottomBar = AureaDims.Hairline + AureaDims.TabBarHeight + navBottom

    val selectTab: (Int) -> Unit = { i ->
        if (i != vm.tab) {
            view.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)   // = selectionClick
            vm.selectTab(i)
        }
    }

    Column(
        Modifier
            .fillMaxSize()
            .background(AureaColors.Background)
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal)),
    ) {
        Box(Modifier.fillMaxWidth().windowInsetsTopHeight(WindowInsets.statusBars).background(AureaColors.SystemBarVeil))
        LiveNoticeBanners(vm.notices)
        ReleaseNotesEntry()
        Box(Modifier.fillMaxWidth().weight(1f)) {
            Box(Modifier.fillMaxSize().backdropSource(shellBackdrop)) {
                when (vm.tab) {
                    HomeViewModel.HOME_TAB -> StartTab(store, vm, home, bottomBar, selectTab)
                    HomeViewModel.PROJECTS_TAB -> ProjectsTab(store, vm, projects, bottomBar)
                    else -> SettingsTab(store, vm, settings, bottomBar)
                }
            }
            HomeTabBar(vm.tab, selectTab, shellBackdrop, Modifier.align(Alignment.BottomCenter))
        }
    }
}

/** Estado de rolagem de uma aba, com a posição guardada no ViewModel ao sair. */
@Composable
private fun rememberTabListState(vm: HomeViewModel, tab: Int): LazyListState {
    val state = remember { LazyListState(vm.scrollIndex(tab), vm.scrollOffset(tab)) }
    DisposableEffect(state) {
        onDispose { vm.saveScroll(tab, state.firstVisibleItemIndex, state.firstVisibleItemScrollOffset) }
    }
    return state
}

/**
 * A barra de abas: vidro #0F141A @ 0,72 com blur σ 24, hairline no topo,
 * 54 de altura; embaixo, a barra de gestos preta.
 */
@Composable
private fun HomeTabBar(selected: Int, onSelect: (Int) -> Unit, backdrop: Backdrop, modifier: Modifier) {
    Column(modifier.fillMaxWidth()) {
        Column(
            Modifier
                .fillMaxWidth()
                .blockTouches()
                .glass(backdrop, AureaElevation.TabBarBlur, AureaElevation.tabBarTint(), AureaElevation.tabBarFallback()),
        ) {
            Box(Modifier.fillMaxWidth().height(AureaDims.Hairline).background(AureaColors.Hairline))
            Row(Modifier.fillMaxWidth().height(AureaDims.TabBarHeight)) {
                Tabs.forEachIndexed { i, tab ->
                    TabItem(tab, i == selected, Modifier.weight(1f)) { onSelect(i) }
                }
            }
        }
        Spacer(Modifier.fillMaxWidth().windowInsetsBottomHeight(WindowInsets.navigationBars).background(AureaColors.NavigationBar))
    }
}

/** `_TabItem`: ícone 24 · 3 · rótulo 10,5 w500; ativo no destaque com o ícone cheio. */
@Composable
private fun TabItem(tab: HomeTab, selected: Boolean, modifier: Modifier, onClick: () -> Unit) {
    val color = if (selected) AureaColors.Accent else AureaColors.Muted
    Column(
        modifier
            .fillMaxHeight()
            .clickable(interactionSource = null, indication = null, role = Role.Tab, onClick = onClick),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        CupertinoIcon(if (selected) tab.active else tab.icon, AureaDims.IconLg, color)
        Spacer(Modifier.height(3.dp))
        Text(stringResource(tab.label), style = AureaType.TabLabel, color = color, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
