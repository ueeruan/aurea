package com.aurea.aurea.effects

import android.content.res.Configuration
import androidx.annotation.StringRes
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.launch
import java.util.Locale

/** Prévia pequena (cartões, linhas, ladrilhos): a metade "depois" tem 159 px. */
internal const val FX_PREVIEW_W = 320
internal const val FX_PREVIEW_H = 200
/** Prévia grande (recomendados e detalhe): a metade "depois" tem 255 px. */
internal const val FX_PREVIEW_LARGE_W = 512
internal const val FX_PREVIEW_LARGE_H = 320
/** Abrir e fechar o detalhe (referência: 200 ms subindo; véu + descida 200 ms ao fechar). */
private const val DETAIL_ANIM_MS = 200

/**
 * A cartela de recuo: degradê vertical, disco quente e a barra clara — a
 * "cena" do catálogo — com o ícone da categoria por cima. É o que aparece
 * antes da prévia real e para efeito que não se pré-visualiza.
 */
@Composable
fun GenericPlate(glyph: Char, modifier: Modifier = Modifier) {
    androidx.compose.foundation.Canvas(modifier.fillMaxSize()) {
        drawRect(Brush.verticalGradient(listOf(AureaColors.EffectPreviewTop, AureaColors.EffectPreviewBottom)))
        val w = size.width
        val h = size.height
        drawCircle(AureaColors.EffectPreviewDisc.copy(alpha = 0.85f), radius = w * 0.19f, center = Offset(w * 0.34f, h * 0.60f))
        drawRect(AureaColors.OnImage.copy(alpha = 0.6f), topLeft = Offset(w * 0.20f, h * 0.18f), size = Size(w * 0.60f, h * 0.012f))
        drawRect(AureaColors.OnImage.copy(alpha = 0.25f), topLeft = Offset(w * 0.20f, h * 0.26f), size = Size(w * 0.40f, h * 0.012f))
    }
    Box(Modifier.fillMaxSize().padding(AureaDims.S2), contentAlignment = Alignment.BottomEnd) {
        CupertinoIcon(glyph, AureaDims.IconSm, AureaColors.OnImage.copy(alpha = 0.85f))
    }
}

/** Cartela das ferramentas (e dos grupos sem prévia): degradê do Aurea e o glifo no meio. */
@Composable
internal fun EffectToolPlate(glyph: Char, glyphSize: Dp = 26.dp, alpha: Float = 1f) {
    Box(
        Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(AureaColors.ActionDim, AureaColors.SurfaceHigh))),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, glyphSize, AureaColors.Accent.copy(alpha = alpha))
    }
}

/**
 * Só a metade "depois" da prévia de comparação do motor (antes | depois), cortada
 * para preencher o quadro sem esticar ([effectOnlyCrop]). Sem rótulos.
 */
@Composable
internal fun AfterOnlyImage(image: ImageBitmap, modifier: Modifier = Modifier) {
    Canvas(modifier.fillMaxSize()) {
        if (size.width < 1f || size.height < 1f) return@Canvas
        val crop = effectOnlyCrop(image.width, image.height, size.width / size.height)
        drawImage(
            image,
            srcOffset = IntOffset(crop[0], crop[1]), srcSize = IntSize(crop[2], crop[3]),
            dstSize = IntSize(size.width.toInt(), size.height.toInt()), filterQuality = FilterQuality.Medium,
        )
    }
}

/**
 * A miniatura de um efeito no navegador (Omega): a prévia real do motor, só o
 * "depois". Ferramenta mostra a cartela dela; sem prévia (desligada em Ajustes,
 * ainda gerando ou efeito que não se pré-visualiza), a cartela da categoria.
 */
@Composable
internal fun EffectAfterPreview(
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    previewsOn: Boolean,
    width: Int = FX_PREVIEW_W,
    height: Int = FX_PREVIEW_H,
) {
    val tool = effectToolOf(entry.typeId)
    if (tool != null) { EffectToolPlate(toolGlyph(tool)); return }
    val image = rememberEffectPreviewState(previews?.takeIf { previewsOn }, entry.typeId, width, height).image
    if (image == null) GenericPlate(categoryGlyph(entry.category)) else AfterOnlyImage(image)
}

/**
 * O nome em INGLÊS de uma entrada: o dos efeitos já é o nome mostrado (o AUREA
 * mostra efeitos em inglês em todo idioma); o das ferramentas é o rótulo em
 * inglês. O detalhe e a busca mostram a segunda linha só quando difere.
 */
@Composable
internal fun pickerEntryEnglishName(entry: EffectCatalogEntry): String {
    val tool = effectToolOf(entry.typeId) ?: return com.aurea.aurea.editor.panels.effectDisplayName(entry.typeId, entry.name)
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val english = remember(context, configuration) {
        context.createConfigurationContext(Configuration(configuration).apply { setLocale(Locale.ENGLISH) }).resources
    }
    return english.getString(toolLabelRes(tool))
}

/** O nome de uma variação (preset) pelo id estável; sem tradução, o nome do motor. */
@Composable
internal fun effectPresetName(typeId: Int, presetId: String, engineName: String): String =
    effectPresetLabelRes(typeId, presetId)?.let { stringResource(it) } ?: engineName

@StringRes
private fun effectPresetLabelRes(typeId: Int, id: String): Int? {
    if (typeId == effectTypeId("aurea.color.grade")) return when (id) {
        "cinema" -> R.string.look_cinema
        "warm" -> R.string.look_warm
        "cool" -> R.string.look_cool
        "vintage" -> R.string.look_vintage
        "mono" -> R.string.look_mono
        "noir" -> R.string.look_noir
        "vivid" -> R.string.look_vivid
        "faded" -> R.string.look_faded
        "sunset" -> R.string.look_sunset
        "teal" -> R.string.look_teal
        "film" -> R.string.look_film
        "anime" -> R.string.look_anime
        "matte" -> R.string.look_matte
        else -> null
    }
    if (typeId == effectTypeId("aurea.enhance.video")) return when (id) {
        "auto" -> R.string.fxp_enhance_auto
        "social" -> R.string.fxp_enhance_social
        "restore" -> R.string.fxp_enhance_restore
        "anime" -> R.string.fxp_enhance_anime
        "upscaled" -> R.string.fxp_enhance_upscaled
        "film" -> R.string.fxp_enhance_film
        else -> null
    }
    return when (id) {
        "impact" -> R.string.fxp_impact
        "handheld" -> R.string.fxp_handheld
        "glitch" -> R.string.fxp_glitch
        "saber_default" -> R.string.fxp_saber_default
        "saber_lightsaber" -> R.string.fxp_saber_lightsaber
        "saber_neon" -> R.string.fxp_saber_neon
        "saber_electric" -> R.string.fxp_saber_electric
        "saber_fire" -> R.string.fxp_saber_fire
        "saber_plasma" -> R.string.fxp_saber_plasma
        "saber_energy" -> R.string.fxp_saber_energy
        "saber_soft" -> R.string.fxp_saber_soft
        "saber_magic" -> R.string.fxp_saber_magic
        "classic" -> R.string.fxp_constellation_classic
        "sphere" -> R.string.fx030c_src_sphere
        "dust" -> R.string.fxp_constellation_dust
        "living_grid" -> R.string.fxp_constellation_living_grid
        else -> null
    }
}

/**
 * A FICHA DE UM EFEITO pelo ⋮ "Detalhes do efeito" do painel (Omega efeitos,
 * P0-6): a mesma folha do navegador — prévia 1:1 com nome, descrição e
 * etiquetas na base — mas SEM a lista de variações; no lugar dela, a lista de
 * parâmetros do AUREA. A estrela do efeito fica no alto. Com [onApply] (nenhum
 * chamador hoje), mostra as variações: a base chama [onApply]; uma variação
 * adiciona com o preset ([EditorStore.addEffectWithPreset]) e fecha.
 */
@Composable
fun EffectDetailSheet(
    store: EditorStore,
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    favorite: Boolean,
    onFavorite: () -> Unit,
    onApply: (() -> Unit)?,
    onDismiss: () -> Unit,
) {
    var closeSignal by remember { mutableIntStateOf(0) }
    Dialog(
        onDismissRequest = { closeSignal++ },
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        // O véu é o da folha (30 %): sem o escurecimento padrão da janela de diálogo.
        val window = (LocalView.current.parent as? DialogWindowProvider)?.window
        SideEffect { window?.setDimAmount(0f) }
        EffectDetailOverlay(
            store = store,
            entries = listOf(entry),
            startIndex = 0,
            closeSignal = closeSignal,
            onClosed = onDismiss,
            variants = onApply != null,
            onAdd = { e, preset ->
                if (preset < 0) onApply?.invoke() else { store.addEffectWithPreset(e.typeId, preset); onDismiss() }
            },
            previews = previews,
            headerFavorite = favorite,
            onHeaderFavorite = onFavorite,
        )
    }
}

/**
 * A FOLHA DE DETALHE da referência (Omega efeitos, P0-6): véu de 30 %, caixa
 * colada embaixo (até 80 % da altura, cantos de 8) que sobe em 200 ms, e as
 * páginas deslizáveis com os efeitos da lista de onde ela abriu ([entries]).
 *
 * [variants]: a lista de variações (1ª linha = efeito base, depois os presets);
 * tocar adiciona ([onAdd] com o preset, −1 = base) — quem chama fecha. Sem
 * ela, a lista de parâmetros. [onTag] (nulo = etiqueta só informa) abre a busca
 * com a ficha. Fechar: ✕, toque no véu ou [closeSignal] mudando (o voltar do
 * sistema); [onClosed] vem depois da descida.
 *
 * Ids: `effects.detail`, `effects.detail.close`,
 * `effects.detail.variant.<id>[.<preset>]`, `effects.detail.favorite.<id>[.<preset>]`,
 * `effects.detail.tag.<tag>` (iguais no iOS).
 */
@Composable
internal fun EffectDetailOverlay(
    store: EditorStore,
    entries: List<EffectCatalogEntry>,
    startIndex: Int,
    closeSignal: Int,
    onClosed: () -> Unit,
    variants: Boolean,
    onAdd: (EffectCatalogEntry, Int) -> Unit,
    onTag: ((String) -> Unit)? = null,
    previews: EffectPreviewStore? = store.effectPreviews,
    headerFavorite: Boolean? = null,
    onHeaderFavorite: (() -> Unit)? = null,
) {
    val latestClosed by rememberUpdatedState(onClosed)
    if (entries.isEmpty()) {
        LaunchedEffect(Unit) { latestClosed() }
        return
    }
    val reduceMotion = rememberReduceMotion()
    val progress = remember { Animatable(if (reduceMotion) 1f else 0f) }
    val scope = rememberCoroutineScope()
    var closing by remember { mutableStateOf(false) }
    val close: () -> Unit = {
        if (!closing) {
            closing = true
            scope.launch {
                if (!reduceMotion) progress.animateTo(0f, tween(DETAIL_ANIM_MS))
                latestClosed()
            }
        }
    }
    LaunchedEffect(Unit) { if (!reduceMotion) progress.animateTo(1f, tween(DETAIL_ANIM_MS)) }
    val firstSignal = remember { closeSignal }
    LaunchedEffect(closeSignal) { if (closeSignal != firstSignal) close() }
    val pager = rememberPagerState(initialPage = startIndex.coerceIn(0, entries.size - 1)) { entries.size }
    val closeLabel = stringResource(R.string.om_fx_detail_close)
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val fullPx = constraints.maxHeight.toFloat()
        Box(
            Modifier
                .fillMaxSize()
                .graphicsLayer { alpha = progress.value }
                .background(AureaColors.FxScrim)
                .pointerInput(Unit) { detectTapGestures { close() } }
                .semantics { contentDescription = closeLabel; role = Role.Button; onClick { close(); true } },
        )
        Column(
            Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .heightIn(max = maxHeight * 0.8f)
                .graphicsLayer { translationY = (1f - progress.value) * fullPx }
                .clip(RoundedCornerShape(topStart = 8.dp, topEnd = 8.dp))
                .background(AureaColors.FxDarkest)
                // A caixa segura o toque: tocar nela não chega ao véu (que fecharia).
                .pointerInput(Unit) { detectTapGestures { } }
                .testTag("effects.detail"),
        ) {
            Box(Modifier.fillMaxWidth()) {
                HorizontalPager(
                    pager,
                    Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.Top,
                    // Bugs 0.3.4 (bug 3): a lista do detalhe é fixa enquanto ele está
                    // aberto; o índice na chave impede repetição mesmo com typeId repetido.
                    key = { effectItemKey(entries[it].typeId) + "@" + it },
                ) { page ->
                    DetailPage(
                        store, entries[page], page, entries.size, active = pager.currentPage == page,
                        previews = previews, variants = variants, onAdd = onAdd, onTag = onTag,
                    )
                }
                Box(
                    Modifier
                        .align(Alignment.TopStart)
                        .size(AureaDims.MinTap)
                        .testTag("effects.detail.close")
                        .semantics { contentDescription = closeLabel; role = Role.Button }
                        .tocavel(shrink = 1f, role = null, onClick = close),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.Xmark, 20.dp, AureaColors.Text)
                }
                if (headerFavorite != null && onHeaderFavorite != null) {
                    val starLabel = stringResource(if (headerFavorite) R.string.om_fx_favorite_remove else R.string.om_fx_favorite_add)
                    val id = effectCardId(entries[pager.currentPage].typeId)
                    Box(
                        Modifier
                            .align(Alignment.TopEnd)
                            .size(AureaDims.MinTap)
                            .testTag("effects.detail.favorite.$id")
                            .semantics { contentDescription = starLabel; role = Role.Button }
                            .tocavel(shrink = 0.9f, role = null, onClick = onHeaderFavorite),
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(
                            if (headerFavorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star, 22.dp,
                            if (headerFavorite) AureaColors.Accent else AureaColors.Text,
                        )
                    }
                }
            }
        }
    }
}

/** Uma página do detalhe: prévia 1:1 com o texto na base e, a ¾ dela, as variações (ou os parâmetros). */
@Composable
private fun DetailPage(
    store: EditorStore,
    entry: EffectCatalogEntry,
    index: Int,
    count: Int,
    active: Boolean,
    previews: EffectPreviewStore?,
    variants: Boolean,
    onAdd: (EffectCatalogEntry, Int) -> Unit,
    onTag: ((String) -> Unit)?,
) {
    val previewsOn = store.effectPrefs.previewsEnabled
    val name = pickerEntryName(entry)
    val english = pickerEntryEnglishName(entry)
    val description = effectDescription(entry.typeId, entry.category)
    val tags = remember(entry.typeId) { effectTags(entry) }
    val tool = effectToolOf(entry.typeId)
    // A prévia ANIMADA (extra do AUREA): só a página à vista gera; desligada por
    // "Prévias de efeitos" ou por "Remover animações" do sistema.
    val reduceMotion = rememberReduceMotion()
    val animatedStore = previews?.animated?.takeIf { previewsOn && !reduceMotion && active && tool == null }
    var scene by rememberSaveable(entry.typeId) { mutableIntStateOf(EffectPreviewScene.PHOTO.id) }
    val animated = rememberAnimatedEffectPreview(animatedStore, entry.typeId, scene, generate = true)
    val sceneName = stringResource(EffectPreviewScene.entries.firstOrNull { it.id == scene }?.label ?: R.string.fx_preview_scene_photo)
    val previewDesc = if (animated.frames.isNotEmpty()) stringResource(R.string.fx_preview_animated_desc, name, sceneName)
        else stringResource(R.string.fx_preview_still_desc, name)
    val pageLabel = stringResource(R.string.om_fx_detail_page, index + 1, count)
    val veil = AureaColors.FxSearch
    Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).semantics { stateDescription = pageLabel }) {
        BoxWithConstraints(Modifier.fillMaxWidth()) {
            val side = maxWidth
            Box(Modifier.fillMaxWidth().height(side).semantics { contentDescription = previewDesc }) {
                when {
                    tool != null -> EffectToolPlate(toolGlyph(tool), glyphSize = 56.dp)
                    animated.frames.isNotEmpty() -> AnimatedPreviewFrames(animated.frames, playing = true, afterOnly = true)
                    else -> EffectAfterPreview(entry, previews, previewsOn, FX_PREVIEW_LARGE_W, FX_PREVIEW_LARGE_H)
                }
                // Escurece a base para o texto (referência: 70 % de «busca»).
                Box(
                    Modifier.fillMaxSize().background(
                        Brush.verticalGradient(
                            0f to veil.copy(alpha = 0f), 0.3f to veil.copy(alpha = 0f),
                            0.7f to veil.copy(alpha = 0.7f), 1f to veil.copy(alpha = 0.7f),
                        ),
                    ),
                )
                if (animated.loading && animatedStore != null) {
                    Text(
                        stringResource(R.string.fx_preview_animating),
                        style = AureaType.of(11f, FontWeight.W600, color = AureaColors.Text),
                        modifier = Modifier.align(Alignment.TopCenter).padding(top = 12.dp).clip(RoundedCornerShape(6.dp))
                            .background(AureaColors.FxScrim).padding(horizontal = 6.dp, vertical = 3.dp),
                    )
                }
            }
            Box(Modifier.fillMaxWidth().height(side * 0.75f)) {
                Column(
                    Modifier
                        .align(Alignment.BottomStart)
                        .fillMaxWidth()
                        .wrapContentHeight(Alignment.Bottom, unbounded = true)
                        .padding(start = 16.dp, end = 16.dp, bottom = 6.dp),
                ) {
                    Text(name, style = AureaType.of(20f, FontWeight.Bold, color = AureaColors.Text), maxLines = 2,
                        overflow = TextOverflow.Ellipsis, modifier = Modifier.semantics { heading() })
                    if (english != name) {
                        Text(english, style = AureaType.of(18f, color = AureaColors.FxTextSecondary), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                    Spacer(Modifier.height(6.dp))
                    Text(description, style = AureaType.of(16f, color = AureaColors.Text, lineHeight = 1.3f), maxLines = 10,
                        overflow = TextOverflow.Ellipsis)
                    if (tags.isNotEmpty()) {
                        Row(Modifier.horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
                            tags.forEach { DetailTagChip(it, onTag) }
                        }
                    }
                }
            }
            Column(Modifier.fillMaxWidth().padding(top = side * 0.75f)) {
                if (variants) VariantList(store, entry, onAdd) else ParamList(store, entry)
            }
        }
        // Extras do AUREA: o seletor de cena da prévia animada e onde o efeito funciona.
        if (animatedStore != null) {
            Spacer(Modifier.height(AureaDims.S3))
            EffectPreviewScenePicker(scene, onSelect = { scene = it }, modifier = Modifier.padding(horizontal = 16.dp))
            if (animated.failed) {
                Text(stringResource(R.string.fx_preview_animation_unavailable),
                    style = AureaType.of(11f, color = AureaColors.FxTextSecondary), modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp))
            }
        }
        Text(
            effectCostLine(entry.category, effectCost(entry.effectClass)) + " · " + effectCompatibilityLine(entry.typeId),
            style = AureaType.of(12f, color = AureaColors.FxTextSecondary),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
        )
        Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars))
    }
}

/** A lista de VARIAÇÕES: a 1ª linha é o efeito base, depois um preset do motor por linha. */
@Composable
private fun VariantList(store: EditorStore, entry: EffectCatalogEntry, onAdd: (EffectCatalogEntry, Int) -> Unit) {
    val prefs = store.effectPrefs
    val presets = remember(entry.typeId) { if (effectToolOf(entry.typeId) != null) emptyList() else store.effectPresets(entry.typeId) }
    val id = effectCardId(entry.typeId)
    val variationsLabel = stringResource(R.string.om_fx_detail_variations)
    Spacer(Modifier.fillMaxWidth().height(0.dp).semantics { contentDescription = variationsLabel; heading() })
    val base = EffectRef(entry.typeId)
    VariantRow(
        code = stringResource(R.string.om_fx_detail_base), name = pickerEntryName(entry), favorite = prefs.isFavorite(base),
        rowTag = "effects.detail.variant.$id", starTag = "effects.detail.favorite.$id",
        onAdd = { onAdd(entry, -1) }, onFavorite = { prefs.toggleFavorite(base) },
    )
    presets.forEachIndexed { i, (pid, engineName) ->
        val ref = EffectRef(entry.typeId, i)
        VariantRow(
            code = presetCode(i), name = effectPresetName(entry.typeId, pid, engineName), favorite = prefs.isFavorite(ref),
            rowTag = "effects.detail.variant.$id.$i", starTag = "effects.detail.favorite.$id.$i",
            onAdd = { onAdd(entry, i) }, onFavorite = { prefs.toggleFavorite(ref) },
        )
    }
}

/** Linha de 70,5: "+" · código sobre o nome · divisória · estrela 56×56. */
@Composable
private fun VariantRow(
    code: String,
    name: String,
    favorite: Boolean,
    rowTag: String,
    starTag: String,
    onAdd: () -> Unit,
    onFavorite: () -> Unit,
) {
    val addLabel = stringResource(R.string.om_fx_detail_add_variant, name)
    val starLabel = stringResource(if (favorite) R.string.om_fx_favorite_remove else R.string.om_fx_favorite_add)
    val line = AureaColors.FxDarkest
    Row(
        Modifier
            .fillMaxWidth()
            .height(70.5.dp)
            .background(AureaColors.FxField)
            .drawBehind { drawRect(line, size = Size(size.width, 0.5.dp.toPx())) },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            Modifier
                .weight(1f)
                .fillMaxHeight()
                .testTag(rowTag)
                .semantics { contentDescription = addLabel; role = Role.Button }
                .tocavel(shrink = 1f, role = null, onClick = onAdd)
                .padding(start = 16.dp, end = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Plus, 18.dp, AureaColors.Text)
            Spacer(Modifier.width(14.dp))
            Column(Modifier.weight(1f)) {
                Text(code, style = AureaType.of(12f, color = AureaColors.FxTextSecondary), maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(name, style = AureaType.of(17f, FontWeight.Bold, color = AureaColors.Text), maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        Box(Modifier.padding(vertical = 12.dp).width(1.dp).fillMaxHeight().background(AureaColors.FxLowSurface))
        Box(
            Modifier
                .size(56.dp)
                .testTag(starTag)
                .semantics { contentDescription = starLabel; role = Role.Button }
                .tocavel(shrink = 0.9f, role = null, onClick = onFavorite),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(
                if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star, 22.dp,
                if (favorite) AureaColors.Accent else AureaColors.FxTextSecondary,
            )
        }
    }
}

/** ⋮ "Detalhes do efeito": no lugar das variações, os parâmetros do AUREA (nome · o que o controle faz). */
@Composable
private fun ParamList(store: EditorStore, entry: EffectCatalogEntry) {
    val specs = remember(entry.typeId) { store.effectSpecs(entry.typeId) }
    if (specs.isEmpty()) return
    val line = AureaColors.FxDarkest
    Text(
        stringResource(R.string.effect_parameters),
        style = AureaType.of(12f, FontWeight.W600, color = AureaColors.FxTextSecondary),
        modifier = Modifier.fillMaxWidth().background(AureaColors.FxField).padding(horizontal = 16.dp, vertical = 8.dp).semantics { heading() },
    )
    specs.forEach { p ->
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = AureaDims.MinTap)
                .background(AureaColors.FxField)
                .drawBehind { drawRect(line, size = Size(size.width, 0.5.dp.toPx())) }
                .padding(horizontal = 16.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(p.label, style = AureaType.of(15f, color = AureaColors.Text), modifier = Modifier.weight(1f))
            Text(paramSummary(p), style = AureaType.of(13f, color = AureaColors.FxTextSecondary), textAlign = TextAlign.End)
        }
    }
}

/** A etiqueta do detalhe (31 de altura num alvo de 44): tocar abre a busca com ela como ficha. */
@Composable
private fun DetailTagChip(tag: String, onTag: ((String) -> Unit)?) {
    val res = effectTagLabelRes(tag) ?: return
    val label = stringResource(res)
    val shape = RoundedCornerShape(10.dp)
    val base = Modifier.heightIn(min = AureaDims.MinTap).padding(end = 6.dp).testTag("effects.detail.tag.$tag")
    Box(
        if (onTag != null) base.semantics { contentDescription = label; role = Role.Button }.tocavel(shrink = 0.96f, role = null) { onTag(tag) }
        else base.semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.height(31.dp).clip(shape).background(AureaColors.FxListSurface).border(0.8.dp, AureaColors.FxDarkest, shape)
                .padding(horizontal = 12.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(label, style = AureaType.of(13f, color = AureaColors.Text), maxLines = 1)
        }
    }
}

/**
 * O que o controle faz, em linguagem de edição: "Empurrar, Puxar, Torcer…",
 * "0 a 100 %", "Cor", "Liga/desliga", "Um ponto na tela".
 */
@Composable
private fun paramSummary(p: com.aurea.aurea.engine.EffectParam): String {
    if (p.enumLabels.isNotEmpty()) {
        val shown = p.enumLabels.take(3).joinToString(", ")
        return if (p.enumLabels.size > 3) "$shown…" else shown
    }
    return when (p.type) {
        com.aurea.aurea.engine.ParamType.COLOR -> stringResource(R.string.ds_cor)
        com.aurea.aurea.engine.ParamType.BOOL -> stringResource(R.string.effect_param_bool)
        com.aurea.aurea.engine.ParamType.POINT2D -> stringResource(R.string.effect_param_point)
        else -> {
            val unit = if (p.unit.isNotEmpty()) " ${p.unit}" else ""
            if (p.min.isFinite() && p.max.isFinite() && p.max > p.min) stringResource(R.string.fx_param_range, trimNumber(p.min), trimNumber(p.max)) + unit else unit.trim()
        }
    }
}

private fun trimNumber(v: Float): String =
    if (v == v.toInt().toFloat()) v.toInt().toString() else String.format(java.util.Locale.ROOT, "%.2f", v).trimEnd('0').trimEnd('.')
