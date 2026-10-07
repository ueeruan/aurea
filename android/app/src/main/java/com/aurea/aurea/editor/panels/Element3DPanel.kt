package com.aurea.aurea.editor.panels

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Text3DInfo
import com.aurea.aurea.state.Text3DPreset
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/**
 * PAINEL 3D (texto 3D, forma 3D, modelo importado e câmera), reorganizado em
 * poucas ABAS em vez de uma parede de controles:
 *
 *  - Material: materiais prontos (amostras), cor, metálico e rugosidade; a
 *    superfície e o resto (especular, emissão, material por região) ficam em
 *    "Avançado", fechado. Forma 3D: cor e imagem por parte. Modelo: os
 *    materiais do arquivo.
 *  - Forma: fonte, alinhamento, profundidade (extrusão) e chanfro do texto;
 *    as partes (posição, giro, escala) e o efeito de partes da forma 3D.
 *  - Luz e cena: sombras do objeto, estúdio, ambiente (HDRI) e, em
 *    "Avançado", qualidade, tom, exposição, brilho e o ambiente do objeto.
 *  - Animação: animação por letra (texto) ou por parte (forma).
 *
 * Toda linha de valor é a mesma [HumanRow] (rótulo, régua, valor, ↺ ao
 * padrão); interruptores são [ToggleLine]; cores, [ColorLine]. Nada mudou no
 * motor: cada controle chama o mesmo comando de antes.
 */
@Composable
internal fun Element3DPanel(env: PanelEnv) {
    val store = env.store
    val t3 by remember(store) { derivedStateOf { store.text3d } }
    val camera = store.detail?.kind == 8
    val shape = if (camera) null else store.shape3d
    val text = if (camera) null else t3
    val tabs = when {
        camera -> listOf(Tab3D.Light)
        text != null || shape != null -> listOf(Tab3D.Material, Tab3D.Shape, Tab3D.Light, Tab3D.Anim)
        else -> listOf(Tab3D.Material, Tab3D.Light)
    }
    var chosen by rememberSaveable { mutableIntStateOf(Tab3D.Material.ordinal) }
    val tab = tabs.firstOrNull { it.ordinal == chosen } ?: tabs.first()
    Column(Modifier.fillMaxSize()) {
        if (tabs.size > 1) {
            Panel3DTabs(tabs, tab, Modifier.padding(start = 18.dp, top = 10.dp, end = 18.dp, bottom = 4.dp)) { chosen = it.ordinal }
        }
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState())
                .padding(start = 18.dp, top = 6.dp, end = 18.dp, bottom = 24.dp),
        ) {
            when (tab) {
                Tab3D.Material -> {
                    when {
                        text != null -> Text3DMaterialTab(env, text)
                        shape != null -> Shape3DSection(env, shape, Shape3DPage.Material)
                        else -> {
                            KitTitle(stringResource(R.string.panel_material))
                            ImportedMaterialSection(store)
                        }
                    }
                    InteriorToggle(store)
                }
                Tab3D.Shape -> when {
                    text != null -> Text3DShapeTab(env, text)
                    shape != null -> {
                        Shape3DSection(env, shape, Shape3DPage.Shape)
                        ActionCard(stringResource(R.string.shape3d_layout_effect), stringResource(R.string.shape3d_layout_hint)) {
                            val type = effectTypeId("aurea.shape3d.layout")
                            if (store.effects.none { e -> e.typeId == type }) store.addEffect(type)
                            env.onOpenPanel(EditorPanel.Effects)
                        }
                    }
                }
                Tab3D.Light -> LightSceneTab(env, objectSettings = !camera)
                Tab3D.Anim -> when {
                    text != null -> TextAnimSection(env, showAnimatorEffect = false)
                    shape != null -> Text3DAnimSection(env, parts = true)
                }
            }
        }
    }
}

/** As abas do painel 3D (a ordem é a da tela). */
internal enum class Tab3D(val label: Int) {
    Material(R.string.ui3d_tab_material),
    Shape(R.string.ui3d_tab_shape),
    Light(R.string.ui3d_tab_light),
    Anim(R.string.ui3d_tab_anim),
}

/** Controle segmentado: cada aba divide a largura; a acesa usa o destaque. */
@Composable
private fun Panel3DTabs(tabs: List<Tab3D>, selected: Tab3D, modifier: Modifier, onSelect: (Tab3D) -> Unit) {
    val group = stringResource(R.string.ui3d_tabs)
    Row(
        modifier.fillMaxWidth().height(40.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
            .semantics { contentDescription = group }.padding(3.dp),
        horizontalArrangement = Arrangement.spacedBy(3.dp),
    ) {
        tabs.forEach { t ->
            val on = t == selected
            Box(
                Modifier.weight(1f).fillMaxHeight().clip(RoundedCornerShape(8.dp))
                    .background(if (on) AureaColors.AccentDim else Color.Transparent)
                    .semantics { role = Role.Tab; this.selected = on }
                    .testTag("panel3d.tab.${t.name.lowercase()}")
                    .tocavel(shrink = 1f) { onSelect(t) },
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    stringResource(t.label), maxLines = 1, overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp,
                        fontWeight = if (on) FontWeight.W700 else FontWeight.W500,
                        color = if (on) AureaColors.Accent else AureaColors.Text)),
                )
            }
        }
    }
}

// --- Texto 3D -----------------------------------------------------------------

/** Cor da amostra de cada material pronto (a mesma ordem do motor). */
private val PresetSwatch = mapOf(
    Text3DPreset.Chrome to Color(0xFFF2F4F9),
    Text3DPreset.Gold to Color(0xFFFFC457),
    Text3DPreset.Brushed to Color(0xFFC7C9CC),
    Text3DPreset.Glossy to Color(0xFFE61A1F),
    Text3DPreset.Matte to Color(0xFFD9D9DB),
    Text3DPreset.Neon to Color(0xFF1AFFD9),
    Text3DPreset.CinematicMetal to Color(0xFFA8ADB8),
)

private val FinishLabels = listOf(
    R.string.ui3d_finish_smooth, R.string.ui3d_finish_brushed, R.string.ui3d_finish_scratched,
    R.string.ui3d_finish_hammered, R.string.ui3d_finish_weathered,
)

@Composable
private fun Text3DMaterialTab(env: PanelEnv, info: Text3DInfo) {
    val store = env.store
    var advanced by rememberSaveable { mutableStateOf(false) }
    KitTitle(stringResource(R.string.ui3d_ready_materials))
    ChipRow {
        (listOf(Text3DPreset.CinematicMetal) + Text3DPreset.values().filter { it != Text3DPreset.CinematicMetal }).forEach { preset ->
            // A bola de estúdio do material (o motor calcula; até chegar, a cor chapada).
            val thumb = rememberPresetThumb(store, preset.ordinal)
            SwatchChip(stringResource(preset.labelRes), PresetSwatch[preset] ?: Color.White, thumb,
                Modifier.testTag("text3d.materialPreset.${preset.ordinal}")) {
                store.applyText3DPreset(preset)
            }
        }
    }
    ColorLine(stringResource(R.string.panel_cor), Color(info.color[0], info.color[1], info.color[2])) {
        store.beginGesture("cor do texto 3D")
        env.openColor(ColorRequest(info.color.copyOf(), onChange = { r, g, b, _ ->
            store.text3d?.let { store.setText3D(it.copy(color = floatArrayOf(r, g, b, 1f)), lazy = true) }
        }, onDone = { store.endGesture() }))
    }
    T3DRow(env, stringResource(R.string.pn_t3d_metallic), info.metallic, 1f, 0f, "metalico") { t, v -> t.copy(metallic = v) }
    T3DRow(env, stringResource(R.string.pn_t3d_roughness), info.roughness, 1f, 0.35f, "rugosidade") { t, v -> t.copy(roughness = v) }
    AdvancedSection(advanced, { advanced = !advanced }) {
        KitTitle(stringResource(R.string.ui3d_finish))
        ChipRow {
            FinishLabels.forEachIndexed { index, res ->
                KitChip(stringResource(res), on = info.surfaceFinish == index) {
                    store.text3d?.let { store.setText3D(it.copy(surfaceFinish = index)) }
                }
            }
        }
        T3DRow(env, stringResource(R.string.pn_t3d_specular), info.specular, 1f, 1f, "especular") { t, v -> t.copy(specular = v) }
        ColorLine(stringResource(R.string.pn_t3d_emissive), Color(info.emissive[0], info.emissive[1], info.emissive[2])) {
            store.beginGesture("emissao do texto 3D")
            env.openColor(ColorRequest(info.emissive.copyOf(), onChange = { r, g, b, _ ->
                store.text3d?.let { store.setText3D(it.copy(emissive = floatArrayOf(r, g, b)), lazy = true) }
            }, onDone = { store.endGesture() }))
        }
        T3DRow(env, stringResource(R.string.pn_t3d_emissive_strength), info.emissiveStrength, 8f, 1f, "forca da emissao", step = 2f) { t, v ->
            t.copy(emissiveStrength = v)
        }
        ToggleLine(stringResource(R.string.pn_t3d_regions), info.regionMaterials) { on ->
            store.text3d?.let { store.setText3D(it.copy(regionMaterials = on)) }
        }
        if (info.regionMaterials) {
            // Frente = a cor acima. Aqui vão a lateral e o chanfro.
            T3DRow(env, stringResource(R.string.pn_t3d_region_side), info.sideRoughness, 1f, 0.35f, "rugosidade da lateral") { t, v ->
                t.copy(sideRoughness = v)
            }
            ColorLine(stringResource(R.string.pn_t3d_region_bevel), Color(info.bevelColor[0], info.bevelColor[1], info.bevelColor[2])) {
                store.beginGesture("cor do chanfro")
                env.openColor(ColorRequest(info.bevelColor.copyOf(), onChange = { r, g, b, _ ->
                    store.text3d?.let { store.setText3D(it.copy(bevelColor = floatArrayOf(r, g, b, 1f)), lazy = true) }
                }, onDone = { store.endGesture() }))
            }
            T3DRow(env, stringResource(R.string.pn_t3d_metallic) + " · " + stringResource(R.string.pn_t3d_region_bevel), info.bevelMetallic, 1f, 0f,
                "metalico do chanfro") { t, v -> t.copy(bevelMetallic = v) }
        }
    }
}

@Composable
private fun Text3DShapeTab(env: PanelEnv, info: Text3DInfo) {
    val store = env.store
    var fontsOpen by remember { mutableStateOf(false) }
    val importFont = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) store.importFont(uri, forText3d = true)
    }
    KitTitle(stringResource(R.string.ui3d_text))
    ChipRow {
        KitChip(stringResource(R.string.t3d_font), on = false) { store.loadFonts(); fontsOpen = true }
        KitChip(stringResource(R.string.t3d_import_font), on = false) { importFont.launch(arrayOf("font/*", "application/octet-stream", "*/*")) }
    }
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.panel_alinhamento), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp)))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(0 to R.string.panel_esquerda, 1 to R.string.panel_centro, 2 to R.string.panel_direita).forEach { (a, label) ->
                KitChip(stringResource(label), on = info.alignment == a) { store.text3d?.let { store.setText3D(it.copy(alignment = a)) } }
            }
        }
    }
    if (fontsOpen) AlertDialog(
        onDismissRequest = { fontsOpen = false },
        title = { Text(stringResource(R.string.t3d_font)) },
        text = {
            LazyColumn(Modifier.heightIn(max = 400.dp)) {
                item { TextButton(onClick = { store.text3d?.let { store.setText3D(it.copy(fontPath = "")) }; fontsOpen = false }) { Text(stringResource(R.string.t3d_default_font)) } }
                items(store.fonts, key = { it.path }) { font ->
                    TextButton(onClick = { store.text3d?.let { store.setText3D(it.copy(fontPath = font.path)) }; fontsOpen = false }) {
                        // A importada volta do motor como "docs:…" (caminho portátil), como no iOS.
                        val chosen = info.fontPath == font.path ||
                            (info.fontPath.startsWith("docs:") && font.path.replace('\\', '/').endsWith("/" + info.fontPath.removePrefix("docs:")))
                        Text("${font.family} ${font.style}", color = if (chosen) AureaColors.Accent else AureaColors.Text)
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = { fontsOpen = false }) { Text(stringResource(R.string.t3d_close)) } },
    )
    KitTitle(stringResource(R.string.ui3d_extrusion))
    // 100 % = a altura da letra (a malha é gerada de novo a cada passo).
    T3DRow(env, stringResource(R.string.pn_depth), info.depth, 3f, 0.25f, "profundidade do texto 3D", step = 0.5f) { t, v -> t.copy(depth = v) }
    KitHint(stringResource(R.string.ui3d_depth_hint))
    Spacer(Modifier.height(6.dp))
    // O chanfro é GEOMETRIA: frente/fundo recuados e o anel do chanfro.
    ToggleLine(stringResource(R.string.pn_t3d_bevel), info.bevel) { on -> store.text3d?.let { store.setText3D(it.copy(bevel = on)) } }
    if (info.bevel) {
        T3DRow(env, stringResource(R.string.pn_t3d_bevel_width), info.bevelWidth, 0.2f, 0.02f, "chanfro", step = 0.05f, decimals = 1) { t, v ->
            t.copy(bevelWidth = v)
        }
        T3DRow(env, stringResource(R.string.pn_t3d_bevel_depth), info.bevelDepth, 0.2f, 0.02f, "chanfro", step = 0.05f, decimals = 1) { t, v ->
            t.copy(bevelDepth = v)
        }
        T3DRow(env, stringResource(R.string.pn_t3d_bevel_roundness), info.bevelRoundness, 1f, 1f, "arredondamento") { t, v -> t.copy(bevelRoundness = v) }
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.pn_t3d_bevel_segments), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp)))
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                listOf(1, 2, 3, 5, 8).forEach { n ->
                    KitChip("$n", on = info.bevelSegments == n) { store.text3d?.let { store.setText3D(it.copy(bevelSegments = n)) } }
                }
            }
        }
    }
    Spacer(Modifier.height(10.dp))
    ActionCard(stringResource(R.string.ui3d_deform_letters), null) {
        val type = effectTypeId("aurea.text3d.layout")
        if (store.effects.none { it.typeId == type }) store.addEffect(type)
        env.onOpenPanel(EditorPanel.Effects)
    }
}

/**
 * Uma linha de valor do texto 3D em % (1 = 100 %): arrasto em passos leves
 * (`lazy`, a malha acompanha) e um passo de desfazer por gesto; teclado e ↺
 * gravam o valor inteiro.
 */
@Composable
private fun T3DRow(
    env: PanelEnv,
    label: String,
    value: Float,
    max: Float,
    default: Float,
    gesture: String,
    step: Float = 0.5f,
    decimals: Int = 0,
    apply: (Text3DInfo, Float) -> Text3DInfo,
) {
    val store = env.store
    HumanRow(
        env, label, value * 100f, step, 0f, max * 100f, "%", decimals, default * 100f,
        onStart = { store.beginGesture(gesture) },
        onValue = { v -> store.text3d?.let { store.setText3D(apply(it, v / 100f), lazy = true) } },
        onEnd = { store.endGesture() },
        onCommit = { v ->
            store.beginGesture(gesture)
            store.text3d?.let { store.setText3D(apply(it, v / 100f)) }
            store.endGesture()
        },
    )
}

/** Chip com a bola do material (miniatura do motor; a cor chapada enquanto ela não chega). */
@Composable
private fun SwatchChip(label: String, swatch: Color, thumb: ImageBitmap?, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Row(
        modifier.height(40.dp).clip(RoundedCornerShape(9.dp)).background(AureaColors.Chip)
            .tocavel(shrink = 1f, onClick = onClick).padding(start = 5.dp, end = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        MaterialBall(thumb, swatch, 30.dp)
        Spacer(Modifier.width(7.dp))
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, color = AureaColors.Text)))
    }
}

/**
 * MOSTRAR INTERIOR (dupla face): as faces de dentro do objeto aparecem com a
 * mesma textura — a câmera que entra no cubo vê o lado de dentro. Ligado por
 * padrão nas formas 3D prontas; modelo importado e texto 3D começam desligados.
 */
@Composable
private fun InteriorToggle(store: EditorStore) {
    val on by remember(store) { derivedStateOf { store.modelInterior } }
    val current = on ?: return
    Spacer(Modifier.height(6.dp))
    Box(Modifier.testTag("panel3d.interior")) {
        ToggleLine(stringResource(R.string.ui3d_show_interior), current) { store.setModelInterior(it) }
    }
    KitHint(stringResource(R.string.ui3d_show_interior_hint))
    Spacer(Modifier.height(10.dp))
}

// --- Modelo importado -----------------------------------------------------------

@Composable
private fun ImportedMaterialSection(store: EditorStore) {
    // Reading detail/curve revision subscribes to playback, edits and undo.
    val detail = store.detail
    val revision = store.curveRevision
    val materials = remember(detail, revision) { store.queryMaterials() }
    var selected by remember(store.primary) { mutableStateOf(0) }
    var dragging by remember(store.primary) { mutableStateOf(false) }
    // FBX/OBJ com textura/.mtl que não veio junto: o mesmo "Importar texturas" do import.
    val missingTextures = remember(detail, revision) { store.selectedModelMissingTextures() }
    if (missingTextures > 0) {
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.model_textures_missing, missingTextures), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            KitChip(stringResource(R.string.model_textures_title), on = false) { store.askModelTextures() }
        }
    }
    if (materials.isEmpty()) return
    val current = materials.firstOrNull { it[0].toInt() == selected } ?: materials.first()
    val index = current[0].toInt()
    val layer = store.primary
    ChipRow {
        materials.forEach { material ->
            val id = material[0].toInt()
            // Bola do material de verdade (cor, metal, rugosidade e a textura do
            // arquivo), não só o nome; refeita quando a revisão ou os valores mudam.
            val thumb = if (layer != null) rememberMaterialThumb(store, layer, id, revision to material.contentHashCode()) else null
            val fallback = Color(material[2].coerceIn(0f, 1f), material[3].coerceIn(0f, 1f), material[4].coerceIn(0f, 1f))
            MaterialChip(stringResource(R.string.i18n_material_n, id + 1), on = id == index, thumb = thumb, fallback = fallback,
                modifier = Modifier.testTag("panel3d.material.$id")) { selected = id }
        }
    }
    val labels = listOf("R", "G", "B", "Alpha", stringResource(R.string.pn_t3d_metallic), stringResource(R.string.pn_t3d_roughness))
    labels.forEachIndexed { param, label ->
        val value = current[param + 2].coerceIn(0f, 1f)
        val here = (store.keyframes[store.primary] ?: emptyList()).any {
            it.property == 37 && it.effectIndex == index && it.paramIndex == param && it.time == detail?.localPlayhead
        }
        val keyLabel = stringResource(if (here) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui) + " · " + label
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(label, modifier = Modifier.width(78.dp), style = AureaType.BodySmall)
            Slider(value = value, onValueChange = {
                if (!dragging) { dragging = true; store.beginGesture("material") }
                store.setMaterialParameter(index, param, it)
            }, onValueChangeFinished = { if (dragging) { dragging = false; store.endGesture() } }, modifier = Modifier.weight(1f))
            TextButton(onClick = { store.toggleMaterialKeyframe(index, param, value) }, modifier = Modifier.semantics { contentDescription = keyLabel }) { Text(if (here) "◆" else "◇") }
        }
    }
    DisposableEffect(store.primary) {
        onDispose { if (dragging) { dragging = false; store.endGesture() } }
    }
}

// --- Luz e cena -----------------------------------------------------------------

/**
 * LUZ E CENA: sombras do objeto, estúdio e ambiente na frente; o acabamento
 * da imagem (qualidade, tom, exposição, brilho) e o ambiente próprio do objeto
 * em "Avançado". [objectSettings] = falso na câmera (só a cena).
 */
@Composable
private fun LightSceneTab(env: PanelEnv, objectSettings: Boolean) {
    val store = env.store
    val e by remember(store) { derivedStateOf { store.environment } }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) { store.importHdri(uri); if (store.detail?.kind == 8) store.setEnvironmentBackground(true) }
    }
    val revision = store.sceneSettingsRevision
    var settings by remember(revision) { mutableStateOf(store.sceneSettings()) }
    fun change(index: Int, value: Float) { store.setSceneSetting(index, value); settings = store.sceneSettings() }
    var advanced by rememberSaveable { mutableStateOf(false) }

    if (objectSettings) {
        val sh by remember(store) { derivedStateOf { store.modelShadows } }
        sh?.let { cur ->
            KitTitle(stringResource(R.string.ui3d_shadows))
            ToggleLine(stringResource(R.string.pn_t3d_cast_shadow), cur.first) { store.setModelShadows(it, cur.second) }
            ToggleLine(stringResource(R.string.pn_t3d_receive_shadow), cur.second) { store.setModelShadows(cur.first, it) }
        }
    }
    if (settings.size >= 8) {
        KitTitle(stringResource(R.string.scene_studio))
        ChipRow {
            listOf(R.string.scene_none, R.string.scene_dark, R.string.scene_product, R.string.scene_sky).forEachIndexed { i, label ->
                KitChip(stringResource(label), on = settings[0].toInt() == i) { change(0, i.toFloat()) }
            }
        }
        ToggleLine(stringResource(R.string.scene_floor), settings[1] > 0) { change(1, if (it) 1f else 0f) }
    }
    KitTitle(stringResource(R.string.environment_texture))
    ChipRow {
        KitChip(stringResource(R.string.panel_estudio_neutro), on = e[0] < 0.5f) { store.clearHdri() }
        KitChip(if (e[0] >= 0.5f) stringResource(R.string.panel_imagem_ambiente) else stringResource(R.string.panel_usar_imagem_ambiente_hdr), on = e[0] >= 0.5f) {
            pick.launch(arrayOf("image/vnd.radiance", "image/x-exr", "application/zip", "application/octet-stream", "*/*"))
        }
    }
    SceneRow(env, stringResource(R.string.panel_intensidade), e[1] * 100f, 1f, 0f, 2000f, "%", 100f, "ambiente") {
        store.setEnvironment(it / 100f, store.environment[2])
    }
    SceneRow(env, stringResource(R.string.pn_env_rotate_light), e[2], 1f, -360f, 360f, "°", 0f, "ambiente") {
        store.setEnvironment(store.environment[1], it)
    }
    ToggleLine(stringResource(R.string.environment_background), (e.getOrNull(3) ?: 0f) > .5f, store::setEnvironmentBackground)
    if ((e.getOrNull(3) ?: 0f) > .5f && e.size >= 6) {
        val duration = store.project.durationFrames.coerceAtLeast(1)
        val start = e[4].toLong().coerceIn(0, (duration - 1).toLong())
        val end = (if (e[5] < 0) duration.toLong() else e[5].toLong()).coerceIn(start + 1, duration.toLong())
        SceneRow(env, stringResource(R.string.environment_start), start.toFloat(), 1f, 0f, (end - 1).toFloat(), "f", 0f, "ambiente") {
            store.setEnvironmentBackgroundRange(it.toLong(), end)
        }
        SceneRow(env, stringResource(R.string.environment_end), end.toFloat(), 1f, (start + 1).toFloat(), duration.toFloat(), "f", duration.toFloat(), "ambiente") {
            store.setEnvironmentBackgroundRange(start, it.toLong())
        }
        ChipRow {
            KitChip(stringResource(R.string.environment_full_duration), on = start == 0L && e[5] < 0) {
                store.setEnvironmentBackgroundRange(0, -1)
            }
            store.detail?.let { layer ->
                KitChip(stringResource(R.string.environment_layer_duration), on = false) {
                    store.setEnvironmentBackgroundRange(layer.startFrame.toLong().coerceAtLeast(0), layer.endFrame.toLong().coerceAtMost(duration.toLong()))
                }
            }
        }
    }
    KitHint(stringResource(R.string.environment_hint))
    Spacer(Modifier.height(4.dp))
    AdvancedSection(advanced, { advanced = !advanced }) {
        if (settings.size >= 8) {
            KitTitle(stringResource(R.string.scene_quality))
            ChipRow {
                listOf(R.string.scene_auto, R.string.scene_low, R.string.scene_medium, R.string.scene_high, R.string.scene_ultra).forEachIndexed { i, label ->
                    KitChip(stringResource(label), on = settings[2].toInt() == i) { change(2, i.toFloat()) }
                }
            }
            KitTitle(stringResource(R.string.scene_tonemap))
            ChipRow {
                KitChip("PBR Neutral", on = settings[3] == 0f) { change(3, 0f) }
                KitChip("AgX", on = settings[3] == 1f) { change(3, 1f) }
            }
            SceneRow(env, stringResource(R.string.scene_exposure), settings[4].coerceIn(.01f, 4f) * 100f, 1f, 1f, 400f, "%", 100f, null) {
                change(4, it / 100f)
            }
            ToggleLine(stringResource(R.string.scene_bloom), settings[5] > 0) { change(5, if (it) 1f else 0f) }
            if (settings[5] > 0) {
                SceneRow(env, stringResource(R.string.ui3d_bloom_strength), settings[6].coerceIn(0f, 4f) * 100f, 1f, 0f, 400f, "%", 100f, null) {
                    change(6, it / 100f)
                }
            }
        }
        if (objectSettings) ObjectEnvironmentSection(env)
    }
}

/** Linha de valor da cena/ambiente: um passo de desfazer por arrasto (quando [gesture] existe). */
@Composable
private fun SceneRow(
    env: PanelEnv,
    label: String,
    value: Float,
    step: Float,
    min: Float,
    max: Float,
    unit: String,
    default: Float,
    gesture: String?,
    onValue: (Float) -> Unit,
) {
    val store = env.store
    HumanRow(
        env, label, value, step, min, max, unit, 0, default,
        onStart = { if (gesture != null) store.beginGesture(gesture) },
        onValue = onValue,
        onEnd = { if (gesture != null) store.endGesture() },
        onCommit = { v -> if (gesture != null) store.beginGesture(gesture); onValue(v); if (gesture != null) store.endGesture() },
    )
}

/**
 * AMBIENTE DO OBJETO (v22): este modelo pode ter a luz dele, sem mexer na dos
 * outros. Sem ambiente próprio, vale o do projeto — o de cima.
 */
@Composable
private fun ObjectEnvironmentSection(env: PanelEnv) {
    val store = env.store
    val oe by remember(store) { derivedStateOf { store.objectEnvironment } }
    val escolher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.importObjectHdri(uri)
    }
    val objectEnv = oe?.takeIf { it.size >= 5 } ?: return
    val proprio = objectEnv[0] >= 0.5f
    KitTitle(stringResource(R.string.panel_ambiente_do_objeto))
    ChipRow {
        KitChip(stringResource(R.string.panel_do_projeto), on = !proprio) { store.setObjectEnvironment(0) }
        KitChip(stringResource(R.string.panel_proprio), on = proprio) {
            store.setObjectEnvironment(1)
            if (objectEnv[1] <= 0f) escolher.launch(arrayOf("image/vnd.radiance", "image/x-exr", "application/zip", "application/octet-stream", "*/*"))
        }
        if (proprio) {
            KitChip(
                if (objectEnv[1] > 0f) stringResource(R.string.panel_trocar_imagem) else stringResource(R.string.panel_usar_imagem_ambiente_hdr),
                on = objectEnv[1] > 0f,
            ) { escolher.launch(arrayOf("image/vnd.radiance", "image/x-exr", "application/zip", "application/octet-stream", "*/*")) }
        }
    }
    if (proprio) {
        SceneRow(env, stringResource(R.string.panel_intensidade), objectEnv[2] * 100f, 1f, 0f, 2000f, "%", 100f, "ambiente") {
            store.setObjectEnvironment(1, intensity = it / 100f)
        }
        SceneRow(env, stringResource(R.string.pn_env_rotate_light), objectEnv[3], 1f, -360f, 360f, "°", 0f, "ambiente") {
            store.setObjectEnvironment(1, rotation = it)
        }
        SceneRow(env, stringResource(R.string.panel_exposicao), objectEnv[4] * 100f, 1f, 5f, 2000f, "%", 100f, "ambiente") {
            store.setObjectEnvironment(1, exposure = it / 100f)
        }
    }
}
