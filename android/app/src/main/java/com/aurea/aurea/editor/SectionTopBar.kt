package com.aurea.aurea.editor

import androidx.compose.foundation.layout.RowScope
import androidx.compose.runtime.Composable
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelBar
import com.aurea.aurea.editor.panels.panelSectionTitle
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.MenuEntry

/**
 * A BARRA DA SEÇÃO: com uma seção da camada aberta (Efeitos, Transformar…), o
 * topo é a barra do painel de propriedade da referência ([PanelBar], Omega
 * fundação F5): `‹` + o título À ESQUERDA, [trailing] à direita (ações do
 * painel, área 06) e um ⋮ opcional ([moreEntries]). O `‹` fecha a seção e volta
 * para as ferramentas da camada. Id da barra: `editor.sectionBar`.
 */
@Composable
internal fun SectionTopBar(
    store: EditorStore,
    panel: EditorPanel,
    onBack: () -> Unit,
    moreEntries: List<MenuEntry>? = null,
    trailing: @Composable RowScope.() -> Unit = {},
) {
    PanelBar(
        title = panelSectionTitle(store, panel),
        onBack = onBack,
        tag = "editor.sectionBar",
        moreEntries = moreEntries,
        trailing = trailing,
    )
}
