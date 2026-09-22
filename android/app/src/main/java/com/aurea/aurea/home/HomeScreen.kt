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
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon

/** Uma aba da barra: rótulo, ícone inativo e ativo (`home_shell.dart@762dbfe`). */
private class HomeTab(val label: String, val icon: Char, val active: Char)

private val Tabs = listOf(
    HomeTab("Inicio", CupertinoGlyph.House, CupertinoGlyph.HouseFill),
    HomeTab("Comunidade", CupertinoGlyph.Person2, CupertinoGlyph.Person2Fill),
    HomeTab("Ajustes", CupertinoGlyph.SliderHorizontal3, CupertinoGlyph.SliderHorizontal3),
    HomeTab("Perfil", CupertinoGlyph.Person, CupertinoGlyph.PersonFill),
    HomeTab("Sobre", CupertinoGlyph.InfoCircle, CupertinoGlyph.InfoCircleFill),
)

/**
 * A Home da Beta A.01 (versão 762dbfe dos prints aprovados): a casca com a
 * barra de 5 abas translúcida e o conteúdo rolando por baixo dela.
 *
 * - Só a aba atual fica composta; a rolagem de cada uma mora no
 *   [HomeViewModel] e volta igual ao trocar de aba ou voltar do editor
 *   (o IndexedStack + PageStorageKey da A.01).
 * - O recuo da status bar entra UMA vez, aqui (a A.01 somava dois com a
 *   faixa de aviso aberta, §8.1). A status bar leva o véu #0B0F13 dos prints.
 * - A faixa de aviso do topo ("+100 usuários…") NÃO entra: o texto vinha do
 *   servidor de avisos (`AvisosService`), não era conteúdo do app.
 */
@Composable
fun HomeScreen(store: EditorStore) {
    val vm: HomeViewModel = viewModel()
    // Uma chamada por aba (nada de laço): cada estado tem o seu lugar fixo na composição.
    val home = rememberTabListState(vm, 0)
    val community = rememberTabListState(vm, 1)
    val settings = rememberTabListState(vm, 2)
    val profile = rememberTabListState(vm, 3)
    val about = rememberTabListState(vm, 4)
    val shellBackdrop = rememberBackdrop()
    val view = LocalView.current
    val navBottom = with(LocalDensity.current) { WindowInsets.navigationBars.getBottom(this).toDp() }
    // Altura total da barra de abas: hairline + 54 + a barra de gestos.
    val bottomBar = AureaDims.Hairline + HomeDims.TabBarHeight + navBottom

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
        Box(Modifier.fillMaxWidth().windowInsetsTopHeight(WindowInsets.statusBars).background(HomeColors.StatusBarVeil))
        Box(Modifier.fillMaxWidth().weight(1f)) {
            Box(Modifier.fillMaxSize().backdropSource(shellBackdrop)) {
                when (vm.tab) {
                    0 -> ProjectsTab(store, vm, home, bottomBar, selectTab)
                    1 -> CommunityTab(store, community, bottomBar)
                    2 -> SettingsTab(store, vm, settings, bottomBar)
                    3 -> ProfileTab(store, profile, bottomBar)
                    else -> AboutTab(store, vm, about, bottomBar)
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
 * 54 de altura; embaixo, a barra de gestos preta dos prints.
 */
@Composable
private fun HomeTabBar(selected: Int, onSelect: (Int) -> Unit, backdrop: Backdrop, modifier: Modifier) {
    Column(modifier.fillMaxWidth()) {
        Column(
            Modifier
                .fillMaxWidth()
                .blockTouches()
                .glass(backdrop, HomeDims.TabBarBlur, AureaColors.Background.copy(alpha = 0.72f), AureaColors.Background.copy(alpha = 0.97f)),
        ) {
            Box(Modifier.fillMaxWidth().height(AureaDims.Hairline).background(AureaColors.Hairline))
            Row(Modifier.fillMaxWidth().height(HomeDims.TabBarHeight)) {
                Tabs.forEachIndexed { i, tab ->
                    TabItem(tab, i == selected, Modifier.weight(1f)) { onSelect(i) }
                }
            }
        }
        Spacer(Modifier.fillMaxWidth().windowInsetsBottomHeight(WindowInsets.navigationBars).background(HomeColors.NavigationBar))
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
        CupertinoIcon(if (selected) tab.active else tab.icon, 24.dp, color)
        Spacer(Modifier.height(3.dp))
        Text(tab.label, style = HomeType.TabLabel, color = color, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
