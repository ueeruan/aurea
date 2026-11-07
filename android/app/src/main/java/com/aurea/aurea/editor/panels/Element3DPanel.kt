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
import androidx.compose.foundation.ScrollState
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
 * PAINEL 3D (texto 3D, forma 3D, modelo importado e câmera), em poucas ABAS:
 *
 *  - Ambiente (a 1ª nos objetos; 0.3.6 refeito em seções — Scene3DEnvironment.kt):
 *    Céu e fundo (estúdio ou HDRI, fundo 360°), Luz ambiente (intensidade,
 *    giro, exposição) e Chão e sombra. Só o que é de ambiente.
 *  - Render (0.3.6): qualidade, tone map e bloom da cena 3D inteira.
 *  - Material: materiais prontos (amostras), cor, metálico e rugosidade; a
 *    superfície e o resto ficam em "Avançado", fechado. Forma 3D: cor por
 *    parte. Modelo: os materiais do arquivo, cada um com a TEXTURA (arquivo ou
 *    camada da timeline). Seção Objeto: sombras e interior (eram do ambiente).
 *  - Câmera e luz: só Ambiente e Render (a cena, não um objeto).
 *  - Partes (forma 3D e modelo): escolher a parte e pôr um EFEITO nela —
 *    Animar parte 3D, Esconder partes 3D, Trocar textura 3D — como o Text 3D
 *    Layout faz no texto. Os ajustes e keyframes ficam no painel de Efeitos.
 *  - Forma e Animação (texto 3D): fonte, profundidade, chanfro; animação por letra.
 *
 * Toda linha de valor é a mesma [HumanRow] (rótulo, régua, valor, ↺ ao
 * padrão); interruptores são [ToggleLine]; cores, [ColorLine]. Nada mudou no
 * motor: cada controle chama o mesmo comando de antes.
 */
@Composable
internal fun Element3DPanel(env: PanelEnv) {
    val store = env.store
    val t3 by remember(store) { derivedStateOf { store.text3d } }
    // Câmera (8) e luz (9) não são objetos: só a cena (ambiente e render).
    val camera = store.detail?.kind == 8 || store.detail?.kind == 9
    val shape = if (camera) null else store.shape3d
    val text = if (camera) null else t3
    val tabs = when {
        camera -> listOf(Tab3D.Light, Tab3D.Render)
        text != null -> listOf(Tab3D.Material, Tab3D.Shape, Tab3D.Light, Tab3D.Anim, Tab3D.Render)
        // 0.3.6: modelo importado ganha a aba Esqueleto (AutoRig 3D, ossos, IK, Motion Capture).
        shape == null && store.detail?.kind == com.aurea.aurea.ui.theme.LayerType.Model3D.kind ->
            listOf(Tab3D.Light, Tab3D.Material, Tab3D.Parts, Tab3D.Rig, Tab3D.Render)
        else -> listOf(Tab3D.Light, Tab3D.Material, Tab3D.Parts, Tab3D.Render)
    }
    var chosen by rememberSaveable { mutableIntStateOf(-1) }
    // 0.4.6: a Cena 3D abre o painel direto numa aba (Motion Capture → Esqueleto).
    androidx.compose.runtime.LaunchedEffect(Element3DRequest.tab) {
        Element3DRequest.tab?.let { if (it in tabs) chosen = it.ordinal }
        Element3DRequest.tab = null
    }
    val tab = tabs.firstOrNull { it.ordinal == chosen } ?: tabs.first()
    // O desenho dos painéis de propriedade: a coluna fixa à esquerda, o conteúdo em
    // seções no centro e as abas em ícones à direita.
    Row(Modifier.fillMaxSize().testTag("panel3d")) {
      LeftRail(onBack = env.onClose, keyframeLook = com.aurea.aurea.ui.ds.KeyframeLook.None, onKeyframe = null, curveAnimated = false, onCurve = null)
      Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
        Column(Modifier.weight(1f).fillMaxHeight().verticalScroll(remember(tab) { ScrollState(0) }).padding(bottom = 24.dp)) {
            when (tab) {
                Tab3D.Material -> {
                    when {
                        text != null -> Text3DMaterialTab(env, text)
                        shape != null -> EnvSection(stringResource(R.string.panel_material), null) {
                            Shape3DSection(env, shape, Shape3DPage.Material)
                            // 0.3.6: a parte escolhida pode vir de uma camada da timeline
                            // (a imagem do arquivo continua na linha "Imagem" acima).
                            val part = store.shapePartOf(store.primary)
                            if (part >= 0) {
                                val c = shape.colors.getOrNull(part) ?: floatArrayOf(1f, 1f, 1f, 1f)
                                MaterialTextureRow(env, part, Color(c[0], c[1], c[2]), allowFile = false)
                            }
                        }
                        else -> EnvSection(stringResource(R.string.panel_material), null) { ImportedMaterialSection(env) }
                    }
                    // 0.3.6: Objeto — projeta/recebe sombra (saíram do Ambiente) e o interior.
                    ObjectShadowSection(store)
                    Column(Modifier.padding(horizontal = 10.dp)) { InteriorToggle(store) }
                }
                Tab3D.Shape -> if (text != null) Text3DShapeTab(env, text)
                Tab3D.Light -> Environment3DTab(env)
                Tab3D.Render -> Render3DTab(env)
                Tab3D.Anim -> if (text != null) Box(Modifier.padding(10.dp)) { AnimChip(stringResource(R.string.la_animators), false) { env.onOpenAnimators() } }
                Tab3D.Parts -> Column(Modifier.padding(horizontal = 10.dp)) { PartsTab(env, shape) }
                Tab3D.Rig -> Column(Modifier.padding(horizontal = 10.dp)) { Rig3DTab(env) }
            }
        }
      if (tabs.size > 1) {
          Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
          PanelIconTabs(tabs.map { PanelIconTab(it.glyph, stringResource(it.label), "panel3d.tab.${it.name.lowercase()}") }, tabs.indexOf(tab),
              stringResource(R.string.ui3d_tabs)) { chosen = tabs[it].ordinal }
      }
    }
}

/** Uma aba da coluna da direita. */
internal class PanelIconTab(val glyph: Char, val label: String, val tag: String)

/** As abas em ícones, na coluna da direita (60 dp), como nos painéis de propriedade do Alight. */
@Composable
internal fun PanelIconTabs(tabs: List<PanelIconTab>, selected: Int, group: String, onSelect: (Int) -> Unit) {
    Column(Modifier.width(60.dp).fillMaxHeight().semantics { contentDescription = group }) {
        tabs.forEachIndexed { i, t ->
            val on = i == selected
            val ink = if (on) AureaColors.Accent else AureaColors.Text
            Column(
                Modifier.fillMaxWidth().height(62.dp).padding(3.dp).clip(RoundedCornerShape(5.dp)).background(if (on) AureaColors.AccentDim else Color.Transparent)
                    .semantics(mergeDescendants = true) { role = Role.Tab; this.selected = on }
                    .testTag(t.tag)
                    .tocavel(shrink = 1f) { onSelect(i) },
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                com.aurea.aurea.ui.theme.CupertinoIcon(t.glyph, 20.dp, ink)
                Spacer(Modifier.height(3.dp))
                Text(t.label, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 9.5.sp, fontWeight = if (on) FontWeight.W700 else FontWeight.W400, color = ink)))
            }
        }
    }
}

/** 0.4.6: pedido de aba ao abrir o painel 3D (a Cena 3D pede o Esqueleto). */
internal object Element3DRequest {
    var tab by androidx.compose.runtime.mutableStateOf<Tab3D?>(null)
}

/** As abas do painel 3D (a ordem é a da tela). */
internal enum class Tab3D(val label: Int, val glyph: Char) {
    Material(R.string.ui3d_tab_material, com.aurea.aurea.ui.theme.CupertinoGlyph.CubeFill),
    Shape(R.string.ui3d_tab_shape, com.aurea.aurea.ui.theme.CupertinoGlyph.Textformat),
    Light(R.string.sh_dock_environment, com.aurea.aurea.ui.theme.CupertinoGlyph.Lightbulb),
    Anim(R.string.ui3d_tab_anim, com.aurea.aurea.ui.theme.CupertinoGlyph.Sparkles),
    Parts(R.string.shape3d_parts, com.aurea.aurea.ui.theme.CupertinoGlyph.SquareOnSquare),
    Rig(R.string.rig3d_tab, com.aurea.aurea.ui.theme.CupertinoGlyph.PersonCropCircle),
    Render(R.string.env3d_tab_render, com.aurea.aurea.ui.theme.CupertinoGlyph.Camera),
}

/**
 * PARTES do objeto 3D: escolher a parte (forma 3D: as fichas de sempre, que
 * também levam o gizmo do palco; modelo: o nome de cada objeto do arquivo) e
 * pôr um EFEITO nela. Par do iOS (Element3DPanel.swift, `PartsTab`).
 */
@Composable
private fun PartsTab(env: PanelEnv, shape: com.aurea.aurea.state.Shape3DInfo?) {
    val store = env.store
    var modelPart by rememberSaveable(store.primary) { mutableIntStateOf(0) }
    if (shape != null) {
        Shape3DSection(env, shape, Shape3DPage.Shape)
    } else {
        val names = remember(store.primary, store.curveRevision) { store.modelPartNames() }
        if (names.isNotEmpty()) {
            KitTitle(stringResource(R.string.shape3d_parts))
            ChipRow {
                names.forEachIndexed { i, n ->
                    Box(Modifier.testTag("panel3d.part.$i")) {
                        KitChip(n.ifBlank { stringResource(R.string.fx3d_part_n, i + 1) }, on = modelPart == i) { modelPart = i }
                    }
                }
            }
        }
    }
    val part = if (shape != null) store.shapePartOf(store.primary).coerceAtLeast(0) else modelPart
    Spacer(Modifier.height(6.dp))
    KitHint(stringResource(R.string.fx3d_parts_hint))
    Spacer(Modifier.height(8.dp))
    val add: (String) -> Unit = { key -> if (store.addPartEffect(key, part)) env.onOpenPanel(EditorPanel.Effects) }
    Box(Modifier.testTag("panel3d.fx.part_transform")) {
        ActionCard(stringResource(R.string.fx3d_name_part_transform), stringResource(R.string.fx3d_desc_part_transform)) { add("aurea.3d.part_transform") }
    }
    Spacer(Modifier.height(6.dp))
    Box(Modifier.testTag("panel3d.fx.part_hide")) {
        ActionCard(stringResource(R.string.fx3d_name_part_hide), stringResource(R.string.fx3d_desc_part_hide)) { add("aurea.3d.part_hide") }
    }
    Spacer(Modifier.height(6.dp))
    Box(Modifier.testTag("panel3d.fx.part_texture")) {
        ActionCard(stringResource(R.string.fx3d_name_part_texture), stringResource(R.string.fx3d_desc_part_texture)) { add("aurea.3d.part_texture") }
    }
    if (shape != null) {
        Spacer(Modifier.height(6.dp))
        ActionCard(stringResource(R.string.shape3d_layout_effect), stringResource(R.string.shape3d_layout_hint)) {
            val type = effectTypeId("aurea.shape3d.layout")
            if (store.effects.none { e -> e.typeId == type }) store.addEffect(type)
            env.onOpenPanel(EditorPanel.Effects)
        }
        // Animações prontas por parte (entrada/saída/loop).
        Text3DAnimSection(env, parts = true)
    }
}

/** Controle segmentado: cada aba divide a largura; a acesa usa o destaque. */
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
    Text3DPreset.Puffy to Color(0xFFFF73B8),
    Text3DPreset.Retro80 to Color(0xFFBF1A9E),
    Text3DPreset.NeonEdge to Color(0xFFFF33D9),
    Text3DPreset.Block to Color(0xFFFFD133),
    Text3DPreset.Ice to Color(0xFFC7EBFF),
    Text3DPreset.Stone to Color(0xFF8C8780),
    Text3DPreset.RoseGold to Color(0xFFFAAD99),
)

private val FinishLabels = listOf(
    R.string.ui3d_finish_smooth, R.string.ui3d_finish_brushed, R.string.ui3d_finish_scratched,
    R.string.ui3d_finish_hammered, R.string.ui3d_finish_weathered,
)

@Composable
private fun Text3DMaterialTab(env: PanelEnv, info: Text3DInfo) {
    val store = env.store
    val hasTexture = remember(store.primary, store.detail, store.curveRevision) { store.hasText3DTexture() }
    val texturePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.setShapePartImage(-1, uri, textTexture = true)
    }
    EnvSection(stringResource(R.string.ui3d_ready_materials), null) {
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
    }
    EnvSection(stringResource(R.string.panel_material), null) {
    Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.env3d_texture), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp)))
        Box(Modifier.testTag("text3d.texture.pick")) {
            KitChip(stringResource(if (hasTexture) R.string.shape3d_image_change else R.string.shape3d_image_pick), on = hasTexture) {
                texturePicker.launch(arrayOf("image/*"))
            }
        }
        if (hasTexture) Box(Modifier.testTag("text3d.texture.clear")) {
            KitChip(stringResource(R.string.shape3d_image_clear), on = false) { store.clearText3DTexture() }
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
    }
    EnvSection(stringResource(R.string.ui3d_finish), null, startOpen = false) {
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
    val importFont = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        store.importFonts(uris, forText3d = true)
    }
    EnvSection(stringResource(R.string.ui3d_text), null) {
    ChipRow {
        KitChip(stringResource(R.string.t3d_font), on = false) { store.loadFonts(); fontsOpen = true }
        KitChip(stringResource(R.string.t3d_import_font), on = false) { importFont.launch(arrayOf("font/*", "application/octet-stream", "*/*")) }
    }
    Column(Modifier.fillMaxWidth().padding(top = 8.dp)) {
        Text(stringResource(R.string.panel_alinhamento), style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp)))
        Spacer(Modifier.height(4.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(0 to R.string.panel_esquerda, 1 to R.string.panel_centro, 2 to R.string.panel_direita).forEach { (a, label) ->
                KitChip(stringResource(label), on = info.alignment == a) { store.text3d?.let { store.setText3D(it.copy(alignment = a)) } }
            }
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
    EnvSection(stringResource(R.string.ui3d_extrusion), null) {
    // 100 % = a altura da letra (a malha é gerada de novo a cada passo).
    T3DRow(env, stringResource(R.string.pn_depth), info.depth, 3f, 0.25f, "profundidade do texto 3D", step = 0.5f) { t, v -> t.copy(depth = v) }
    KitHint(stringResource(R.string.ui3d_depth_hint))
    }
    // O chanfro é GEOMETRIA: frente/fundo recuados e o anel do chanfro.
    EnvSection(stringResource(R.string.pn_t3d_bevel), null, on = info.bevel, onSwitch = { on -> store.text3d?.let { store.setText3D(it.copy(bevel = on)) } }) {
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
    Box(Modifier.padding(10.dp)) {
    ActionCard(stringResource(R.string.ui3d_deform_letters), null) {
        val type = effectTypeId("aurea.text3d.layout")
        if (store.effects.none { it.typeId == type }) store.addEffect(type)
        env.onOpenPanel(EditorPanel.Effects)
    }
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
private fun ImportedMaterialSection(env: PanelEnv) {
    val store = env.store
    // Reading detail/curve revision subscribes to playback, edits and undo.
    val detail = store.detail
    val revision = store.curveRevision
    val materials = remember(detail, revision) { store.queryMaterials() }
    var selected by remember(store.primary) { mutableStateOf(0) }
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
    // 0.3.6: a TEXTURA do material escolhido, direto aqui (arquivo ou timeline).
    MaterialTextureRow(env, index, Color(current[2].coerceIn(0f, 1f), current[3].coerceIn(0f, 1f), current[4].coerceIn(0f, 1f)))
    Spacer(Modifier.height(6.dp))
    AureaPropertyPanel(store, "material", current.copyOfRange(2, 8), index,
        onOpenCurve = { env.onOpenPanel(EditorPanel.Curve) })
}
