package com.aurea.aurea.editor.panels

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
// TEXTURA DO MODELO 3D (13/11/2026, conceito do dono: "um modelo 3D é UMA camada").
// A aba Textura do painel do modelo (Model3DPanel.kt) tem "Aplicar em":
//
//   Modelo inteiro  — uma imagem cobre todas as partes (Trocar textura 3D com
//                     "todas as partes" ligado), Repetir e "Voltar à textura original".
//   Uma parte ›     — lista de partes → parte aberta: a imagem DELA (vale por cima da
//                     do modelo inteiro), Repetir e Esconder esta parte.
//
// A aba Mover por parte (posição/giro/escala) SAIU: o dono recusou — o palco e a
// timeline do editor já movem o modelo. Projetos antigos com "Animar parte 3D"
// continuam iguais e o efeito segue na lista de Efeitos (nada se perde).
//
// Por baixo, os mesmos efeitos do motor (`aurea.3d.part_texture`, `part_hide`,
// parâmetros fixos em Object3DEffects.cpp): o primeiro ajuste cria o efeito.
// Par do iOS: `Parts3DEditor` / `WholeModelTexture` em Panel3DView.swift.
// =============================================================================

private const val PART_TEXTURE = "aurea.3d.part_texture"
private const val PART_HIDE = "aurea.3d.part_hide"

/** "Todas as partes" (parâmetro 2) ligado: é a textura do modelo inteiro. */
private fun EditorStore.coversAll(e: LayerEffect): Boolean =
    (effectParams[e.effectId]?.getOrNull(2)?.value?.getOrNull(0) ?: 0f) >= 0.5f

/** O efeito [key] desta parte (parâmetro 0 = parte 1..N; no Esconder, primeira = última = a parte). */
private fun EditorStore.partEffect(key: String, part: Int): LayerEffect? {
    val type = effectTypeId(key)
    return effects.firstOrNull { e ->
        if (e.typeId != type) return@firstOrNull false
        val ps = effectParams[e.effectId] ?: return@firstOrNull false
        val first = ps.getOrNull(0)?.value?.getOrNull(0)?.toInt()
        when (key) {
            PART_HIDE -> first == part + 1 && ps.getOrNull(1)?.value?.getOrNull(0)?.toInt() == part + 1
            PART_TEXTURE -> first == part + 1 && !coversAll(e)
            else -> first == part + 1
        }
    }
}

/** Garante o efeito da parte (cria no 1º ajuste) e devolve o id; nulo se o motor recusou. */
private fun EditorStore.ensurePartEffect(key: String, part: Int): Int? {
    partEffect(key, part)?.let { return it.effectId }
    if (!addPartEffect(key, part)) return null
    return partEffect(key, part)?.effectId
}

/** A textura do modelo inteiro (Trocar textura 3D com "todas as partes"). */
private fun EditorStore.wholeTexture(): LayerEffect? {
    val type = effectTypeId(PART_TEXTURE)
    return effects.firstOrNull { it.typeId == type && coversAll(it) }
}

/**
 * Cria a textura do modelo inteiro e a põe ANTES das texturas de parte (no motor, a
 * de baixo na lista vence): a imagem de uma parte continua valendo por cima.
 */
private fun EditorStore.ensureWholeTexture(): Int? {
    wholeTexture()?.let { return it.effectId }
    val type = effectTypeId(PART_TEXTURE)
    val before = effects.map { it.effectId }.toSet()
    if (!addPartEffect(PART_TEXTURE, 0)) return null
    val fx = effects.firstOrNull { it.typeId == type && it.effectId !in before } ?: return null
    effectParams[fx.effectId]?.getOrNull(2)?.let { setEffectParam(fx.effectId, it, 1f) }
    val firstTexture = effects.indexOfFirst { it.typeId == type }
    if (firstTexture >= 0 && effects.indexOfFirst { it.effectId == fx.effectId } > firstTexture) reorderEffect(fx.effectId, firstTexture)
    return fx.effectId
}

/** Modelo inteiro: a imagem, Repetir e Voltar à textura original. */
@Composable
internal fun WholeModelTexture(env: PanelEnv) {
    val store = env.store
    val fx = store.wholeTexture()
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.ensureWholeTexture()?.let { store.setEffectImage(uri, it) }
    }
    val imageName = fx?.let { store.effectImageName(it.effectId) }.orEmpty()
    KitHint(stringResource(R.string.m3d_texture_hint))
    Spacer(Modifier.height(6.dp))
    TextureRows(env, fx, imageName, "panel3d.texture.whole", onPick = { picker.launch(arrayOf("image/*")) })
}

/** As linhas comuns: escolher a imagem, Repetir e voltar à textura do arquivo. */
@Composable
private fun TextureRows(env: PanelEnv, fx: LayerEffect?, imageName: String, tag: String, onPick: () -> Unit) {
    val store = env.store
    PanelItemRow(
        CupertinoGlyph.Photo,
        stringResource(if (imageName.isBlank()) R.string.m3d_texture_pick else R.string.shape3d_image_change),
        "$tag.pick",
        detail = imageName.ifBlank { stringResource(R.string.fn_part_texture_none) },
        chevron = true,
        onClick = onPick,
    )
    if (fx == null) return
    if (store.effectParams[fx.effectId] == null) return
    // 14/11/2026 (v76): Repetir, Deslocar X/Y e Girar com ◆ (toca na linha, marca no ◆ da
    // esquerda); ROLAR X/Y faz a textura andar sozinha com o tempo (esteira, água, tela).
    TextureParamRow(env, fx, store.paramOf(fx.effectId, 3), stringResource(R.string.fxo_repeat), "%", 0.5f, 0, 100f, "$tag.repeat", true, "repetir textura 3D")
    if (store.paramOf(fx.effectId, PT_SCROLL_Y) != null) {
        KitTitle(stringResource(R.string.m3d_tex_transform))
        TextureParamRow(env, fx, store.paramOf(fx.effectId, PT_OFFSET_X), stringResource(R.string.m3d_tex_offset_x), "%", 0.5f, 1, 0f, "$tag.offsetX", true, "deslocar textura 3D")
        TextureParamRow(env, fx, store.paramOf(fx.effectId, PT_OFFSET_Y), stringResource(R.string.m3d_tex_offset_y), "%", 0.5f, 1, 0f, "$tag.offsetY", true, "deslocar textura 3D")
        TextureParamRow(env, fx, store.paramOf(fx.effectId, PT_ROTATION), stringResource(R.string.m3d_tex_rotation), "°", 1f, 0, 0f, "$tag.rotation", true, "girar textura 3D")
        KitTitle(stringResource(R.string.m3d_tex_scroll))
        TextureParamRow(env, fx, store.paramOf(fx.effectId, PT_SCROLL_X), stringResource(R.string.m3d_tex_scroll_x), "%/s", 1f, 0, 0f, "$tag.scrollX", false, "rolar textura 3D")
        TextureParamRow(env, fx, store.paramOf(fx.effectId, PT_SCROLL_Y), stringResource(R.string.m3d_tex_scroll_y), "%/s", 1f, 0, 0f, "$tag.scrollY", false, "rolar textura 3D")
        KitHint(stringResource(R.string.m3d_tex_scroll_hint))
    }
    PanelItemRow(CupertinoGlyph.ArrowCounterclockwise, stringResource(R.string.m3d_texture_original), "$tag.remove") {
        store.removeEffect(fx.effectId)
    }
}

/** Parâmetros novos do "Trocar textura 3D" (v76; Object3DEffects.cpp, acrescentados no fim). */
private const val PT_OFFSET_X = 4
private const val PT_OFFSET_Y = 5
private const val PT_ROTATION = 6
private const val PT_SCROLL_X = 7
private const val PT_SCROLL_Y = 8

/**
 * Uma régua da textura (parâmetro [param] do efeito). [keyable]: tocar/arrastar escolhe a
 * linha e o ◆ da coluna da esquerda marca o keyframe dela (Model3DKeys, como no Material).
 */
@Composable
private fun TextureParamRow(
    env: PanelEnv, fx: LayerEffect, param: com.aurea.aurea.engine.EffectParam?, label: String, unit: String, step: Float,
    decimals: Int, default: Float, tag: String, keyable: Boolean, gesture: String,
) {
    val store = env.store
    val p = param ?: return
    val look = if (keyable) effectLook(store, fx.effectId, p.index) else com.aurea.aurea.ui.ds.KeyframeLook.None
    val toggle = { store.paramOf(fx.effectId, p.index)?.let { store.toggleEffectKeyframe(fx.effectId, it) }; Unit }
    if (keyable) KeyRowSync(tag, look, toggle)
    // O parâmetro do instante (o `animated` muda quando o ◆ entra): sempre o mais novo.
    fun fresh() = store.paramOf(fx.effectId, p.index) ?: p
    Box(Modifier.testTag(tag)) {
        HumanRow(
            env, label, p.value.getOrElse(0) { default }, step, p.min, p.max, unit, decimals, default,
            onStart = { if (keyable) Model3DKeys.claim(tag, look, toggle); store.beginGesture(gesture) },
            onValue = { store.setEffectParam(fx.effectId, fresh(), it) },
            onEnd = { store.endGesture() },
            onCommit = { v -> store.beginGesture(gesture); store.setEffectParam(fx.effectId, fresh(), v); store.endGesture() },
            selected = keyable && Model3DKeys.target == tag,
            onSelect = if (keyable) ({ Model3DKeys.claim(tag, look, toggle) }) else null,
            keyframe = look,
        )
    }
}


@Composable
internal fun Parts3DEditor(env: PanelEnv, names: List<String>) {
    val store = env.store
    var open by rememberSaveable(store.primary) { mutableStateOf<Int?>(null) }
    val part = open
    if (part == null || part !in names.indices) {
        PartsList(store, names) { open = it }
        return
    }
    PartOpen(env, part, names[part].ifBlank { stringResource(R.string.fx3d_part_n, part + 1) }) { open = null }
}

/** A lista: uma linha de 48 dp por parte, com o que a parte tem (textura própria, escondida). */
@Composable
private fun PartsList(store: EditorStore, names: List<String>, onOpen: (Int) -> Unit) {
    KitHint(stringResource(R.string.fn_part_list_hint))
    Spacer(Modifier.height(4.dp))
    val ownTexture = stringResource(R.string.m3d_part_own_texture)
    val hiddenLabel = stringResource(R.string.m3d_part_hidden)
    names.forEachIndexed { i, n ->
        val name = n.ifBlank { stringResource(R.string.fx3d_part_n, i + 1) }
        val textured = store.partEffect(PART_TEXTURE, i) != null
        val hidden = store.partEffect(PART_HIDE, i)?.enabled == true
        val state = listOfNotNull(ownTexture.takeIf { textured }, hiddenLabel.takeIf { hidden }).joinToString(" · ")
        Row(
            Modifier.fillMaxWidth().heightIn(min = 48.dp).padding(vertical = 2.dp).clip(RoundedCornerShape(6.dp))
                .background(AureaColors.FxLowSurface).testTag("panel3d.part.$i")
                .semantics(mergeDescendants = true) { contentDescription = name; if (state.isNotEmpty()) stateDescription = state }
                .tocavel(shrink = 1f) { onOpen(i) }.padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.CubeBox, 18.dp, if (hidden) AureaColors.Muted else AureaColors.Text)
            Spacer(Modifier.width(10.dp))
            Text(name, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
            if (state.isNotEmpty()) {
                Text(state, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)))
                Spacer(Modifier.width(8.dp))
            }
            CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Muted)
        }
    }
}

/** A parte aberta: ‹ nome; a textura dela e Esconder esta parte. */
@Composable
private fun PartOpen(env: PanelEnv, part: Int, name: String, onBack: () -> Unit) {
    val store = env.store
    val backLabel = stringResource(R.string.fn_part_back)
    Row(Modifier.fillMaxWidth().height(48.dp).testTag("panel3d.part.open"), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(48.dp).testTag("panel3d.part.back").semantics { contentDescription = backLabel }.tocavel(onClick = onBack),
            contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.ChevronLeft, 18.dp, AureaColors.Text) }
        Text(name, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W700, color = AureaColors.Text)))
    }
    val fx = store.partEffect(PART_TEXTURE, part)
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.ensurePartEffect(PART_TEXTURE, part)?.let { store.setEffectImage(uri, it) }
    }
    TextureRows(env, fx, fx?.let { store.effectImageName(it.effectId) }.orEmpty(), "panel3d.part.texture",
        onPick = { picker.launch(arrayOf("image/*")) })
    // Esconder a parte: o efeito Esconder com primeira = última = a parte.
    Spacer(Modifier.height(6.dp))
    val hide = store.partEffect(PART_HIDE, part)
    val hidden = hide != null && hide.enabled
    Box(Modifier.testTag("panel3d.part.hide")) {
        ToggleLine(stringResource(R.string.fn_part_hide), hidden) { on ->
            when {
                on && hide == null -> store.ensurePartEffect(PART_HIDE, part)?.let { id ->
                    store.effectParams[id]?.getOrNull(1)?.let { last -> store.setEffectParam(id, last, (part + 1).toFloat()) }
                }
                hide != null -> store.setEffectEnabled(hide.effectId, on)
            }
        }
    }
}
