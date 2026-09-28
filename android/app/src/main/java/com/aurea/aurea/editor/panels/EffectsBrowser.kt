package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.effects.EffectPicker
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.text.Normalizer

/** Busca sem acento e sem caixa ("saturacao" acha "Saturação"). */
internal fun normalizeSearch(s: String): String =
    Normalizer.normalize(s, Normalizer.Form.NFD).replace(Regex("\\p{Mn}+"), "").lowercase().trim()

/**
 * O NAVEGADOR DE EFEITOS EM TELA CHEIA.
 *
 * O caminho normal é a aba "Adicionar" do próprio painel Efeitos; esta tela é
 * o mesmo escolhedor ([EffectPicker]) para quem a pede de fora do painel. Um
 * toque no cartão adiciona às camadas escolhidas e fecha; segurar favorita.
 */
@Composable
internal fun EffectsBrowser(store: EditorStore, onDismiss: () -> Unit) {
    val hasAudio by remember(store) { derivedStateOf { store.detail?.hasAudio == true } }
    val back = stringResource(R.string.editor_voltar_editor)
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(
            Modifier
                .fillMaxSize()
                .background(AureaColors.EditorPanel)
                .windowInsetsPadding(WindowInsets.safeDrawing),
        ) {
            Row(
                Modifier.fillMaxWidth().height(AureaDims.EditorTopBar).padding(start = AureaDims.S2, end = AureaDims.S2),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(
                    Modifier.semantics { contentDescription = back }.tocavel(shrink = 1f, onClick = onDismiss).padding(AureaDims.S2),
                    Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.ChevronBack, AureaDims.IconLg, AureaColors.Text)
                }
                Spacer(Modifier.padding(horizontal = 2.dp))
                Text(
                    stringResource(R.string.panel_efeitos),
                    style = AureaType.Base.merge(TextStyle(fontSize = 18.sp, fontWeight = FontWeight.W700)),
                    modifier = Modifier.weight(1f),
                )
                Text(
                    stringResource(R.string.effect_count, store.catalog.size),
                    style = AureaType.CardSpec,
                    modifier = Modifier.padding(end = AureaDims.S3),
                )
            }
            EffectPicker(
                store = store,
                layerHasAudio = hasAudio,
                onPick = { e ->
                    store.addEffect(e.typeId)
                    store.effectPrefs.addRecent(e.typeId)
                    onDismiss()
                },
                modifier = Modifier.weight(1f),
            )
        }
    }
}
