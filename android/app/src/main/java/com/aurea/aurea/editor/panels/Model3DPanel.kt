package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
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
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.KeyframeDiamondIcon
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.displayToEngine
import com.aurea.aurea.ui.ds.engineToDisplay
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.rgbaColor
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
// PAINEL DO MODELO 3D (13/11/2026, conceito aprovado pelo dono; o NodeVideo foi só
// referência de comportamento). Um modelo 3D é UMA camada; o painel dele tem
// poucas telas, um assunto por tela e controles grandes:
//
//   Material — as BOLAS dos visuais do modelo inteiro (Original, Plástico, Fosco,
//              Metal, Cromo, Ouro, Vidro: só o que o motor desenha pelo ajuste de
//              material) e, embaixo, Cor · Metal · Rugosidade · Opacidade com ◆.
//              "Ajustar" escolhe o modelo inteiro (padrão) ou um material.
//   Textura  — Aplicar em: modelo inteiro / uma parte ›  (Parts3DEditor.kt).
//   Animação — clipe do arquivo, EVOLUÇÃO (0–100 % do clipe, com ◆ — anima-se a
//              animação do arquivo por keyframes), Repetir e Velocidade.
//   Luz e sombra — projeta/recebe sombra, o interior e a LUZ DO OBJETO (estúdio
//              ou HDRI só dele, intensidade, giro, exposição) — Element3DPanel.
//
// 14/11/2026 (3D opções extras, v76): Material ganhou POSIÇÃO (assentar no chão,
// centralizar, ajustar à tela), o BRILHO (emissão: cor + intensidade, com ◆) e o
// visual Neon; Textura ganhou deslocar/girar/rolar; Animação ganhou Começar em,
// De trás para frente, Ida e volta e a Velocidade com ◆.
//
// A regra mora no motor (EngineModelPanel.cpp); par do iOS: Model3DViews em
// Panel3DView.swift. Mover/girar o modelo continua no palco e na timeline.
// =============================================================================

/** Os visuais do modelo, na ordem do motor (Engine::apply_model_material_preset). */
private val ModelLookLabels = listOf(
    R.string.m3d_look_original, R.string.m3d_look_plastic, R.string.m3d_look_matte, R.string.m3d_look_metal,
    R.string.m3d_look_chrome, R.string.m3d_look_gold, R.string.m3d_look_glass, R.string.m3d_look_neon,
)

/** Cor chapada de cada bola enquanto a do motor não chega. */
private val ModelLookSwatch = listOf(
    Color(0xFFB9BDC6), Color(0xFFE4E4E6), Color(0xFF9C9C9F), Color(0xFF8F949D),
    Color(0xFFF2F4F9), Color(0xFFFFC457), Color(0x99C7EBFF), Color(0xFF14D9FF),
)

/** Bits dos canais do material (motor: R, G, B, opacidade, metal, rugosidade; v76: brilho R, G, B, força). */
private const val MASK_COLOR = 7
private const val MASK_GLOW_COLOR = 0x1C0
private const val PARAM_GLOW = 6
private const val PARAM_GLOW_STRENGTH = 9

/**
 * 14/11/2026 (pedido do dono: "não é só tocar e marcar no normal?"): sem ◆ em cada
 * linha. Toca-se na linha (ou se mexe nela) para escolhê-la e o ◆ da coluna da
 * esquerda marca o keyframe dela, como nos outros painéis do AUREA.
 */
internal object Model3DKeys {
    var target by mutableStateOf<String?>(null)
    var look by mutableStateOf(KeyframeLook.None)
    var toggle by mutableStateOf<(() -> Unit)?>(null)
    fun clear() { target = null; look = KeyframeLook.None; toggle = null }
    /** A linha [tag] é a escolhida: o trilho passa a mostrar o ◆ dela. */
    fun claim(tag: String, keyLook: KeyframeLook, onToggle: () -> Unit) {
        target = tag; look = keyLook; toggle = onToggle
    }
}

/** Mantém o ◆ do trilho em dia com a linha escolhida (o estado muda com o cabeçote). */
@Composable
internal fun KeyRowSync(tag: String, keyLook: KeyframeLook, onToggle: () -> Unit) {
    androidx.compose.runtime.SideEffect {
        if (Model3DKeys.target == tag) { Model3DKeys.look = keyLook; Model3DKeys.toggle = onToggle }
    }
}
private const val PARAM_OPACITY = 3
private const val PARAM_METAL = 4
private const val PARAM_ROUGH = 5

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun ModelMaterialTab(env: PanelEnv) {
    val store = env.store
    val layer = store.primary ?: return
    val detail = store.detail
    val revision = store.curveRevision
    val clipRevision = store.modelClipRevision
    val playhead = store.playhead
    val materials = remember(layer, detail, revision, clipRevision, playhead) { store.queryMaterials() }
    val missing = remember(layer, detail, revision) { store.selectedModelMissingTextures() }
    val locked = detail?.locked == true
    Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp).testTag("panel3d.modelMaterial")) {
        // POSIÇÃO (v76): assentar no chão, centralizar e ajustar à tela — um passo de desfazer cada.
        KitTitle(stringResource(R.string.m3d_place))
        Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            PlaceTile(CupertinoGlyph.ArrowDownToLine, stringResource(R.string.m3d_place_ground), "panel3d.place.ground", locked,
                Modifier.weight(1f)) { store.placeModel(layer, 0) }
            PlaceTile(CupertinoGlyph.SmallcircleCircle, stringResource(R.string.m3d_place_center), "panel3d.place.center", locked,
                Modifier.weight(1f)) { store.placeModel(layer, 1) }
            PlaceTile(CupertinoGlyph.Fullscreen, stringResource(R.string.m3d_place_fit), "panel3d.place.fit", locked,
                Modifier.weight(1f)) { store.placeModel(layer, 2) }
        }
        KitHint(stringResource(R.string.m3d_place_hint))
        Spacer(Modifier.height(8.dp))
        // FBX/OBJ com textura/.mtl que não veio junto: o mesmo "Importar texturas" do import.
        if (missing > 0) {
            Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.model_textures_missing, missing), modifier = Modifier.weight(1f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                KitChip(stringResource(R.string.model_textures_title), on = false) { store.askModelTextures() }
            }
        }
        if (materials.isEmpty()) {
            Spacer(Modifier.height(10.dp))
            KitHint(stringResource(R.string.m3d_no_materials))
            return@Column
        }
        val look = remember(layer, revision, clipRevision, materials) { store.modelMaterialPreset(layer) }
        KitTitle(stringResource(R.string.m3d_look))
        KitHint(stringResource(if (look < 0) R.string.m3d_look_custom else R.string.m3d_look_hint))
        FlowRow(
            Modifier.fillMaxWidth().padding(vertical = 10.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            ModelLookLabels.forEachIndexed { i, res ->
                LookBall(stringResource(res), rememberModelLookThumb(store, layer, i), ModelLookSwatch[i], on = look == i,
                    Modifier.testTag("panel3d.look.$i")) { if (!locked && look != i) store.applyModelMaterialPreset(layer, i) }
            }
        }
        // Ajustar: o modelo inteiro (padrão) ou um material do arquivo.
        var target by rememberSaveable(layer) { mutableIntStateOf(-1) }
        if (target >= 0 && materials.none { it[0].toInt() == target }) target = -1
        KitTitle(stringResource(R.string.m3d_adjust))
        if (materials.size > 1) ChipRow {
            Box(Modifier.testTag("panel3d.adjust.all")) { KitChip(stringResource(R.string.m3d_whole_model), target < 0) { target = -1 } }
            materials.forEach { m ->
                val id = m[0].toInt()
                Box(Modifier.testTag("panel3d.adjust.$id")) { KitChip(stringResource(R.string.m3d_material_n, id + 1), target == id) { target = id } }
            }
        }
        val values = materials.firstOrNull { it[0].toInt() == target } ?: materials.first()
        val targets = if (target < 0) materials.map { it[0].toInt() }.toSet() else setOf(target)
        val local = detail?.localFrame(playhead)
        val keys = store.primaryKeys().filter { it.property == TrackProperty.MATERIAL_PARAM && it.effectIndex in targets }
        fun lookOf(mask: Int): KeyframeLook {
            val ks = keys.filter { (mask shr it.paramIndex) and 1 == 1 }
            return when { ks.any { it.time == local } -> KeyframeLook.KeyHere; ks.isNotEmpty() -> KeyframeLook.Animated; else -> KeyframeLook.None }
        }
        ModelColorRow(env, stringResource(R.string.panel_cor), floatArrayOf(values[2], values[3], values[4]), 0, MASK_COLOR,
            target, lookOf(MASK_COLOR), locked, "panel3d.mat.color", "cor do modelo 3D")
        MaterialSlider(env, stringResource(R.string.pn_t3d_metallic), values[2 + PARAM_METAL], PARAM_METAL, target, lookOf(1 shl PARAM_METAL), "panel3d.mat.metal")
        MaterialSlider(env, stringResource(R.string.pn_t3d_roughness), values[2 + PARAM_ROUGH], PARAM_ROUGH, target, lookOf(1 shl PARAM_ROUGH), "panel3d.mat.rough")
        MaterialSlider(env, stringResource(R.string.panel_opacidade), values[2 + PARAM_OPACITY], PARAM_OPACITY, target, lookOf(1 shl PARAM_OPACITY), "panel3d.mat.opacity")
        // BRILHO (v76): a luz própria do material (emissão) — cor e intensidade, cada uma com ◆.
        val glow = remember(layer, revision, clipRevision, playhead, target, detail) { store.modelMaterialEmission(layer, target) }
        if (glow != null && glow.size >= 4) {
            Spacer(Modifier.height(6.dp))
            KitTitle(stringResource(R.string.m3d_glow))
            ModelColorRow(env, stringResource(R.string.m3d_glow_color), floatArrayOf(glow[0], glow[1], glow[2]), PARAM_GLOW, MASK_GLOW_COLOR,
                target, lookOf(MASK_GLOW_COLOR), locked, "panel3d.mat.glow.color", "brilho do modelo 3D")
            MaterialSlider(env, stringResource(R.string.m3d_glow_strength), glow[3], PARAM_GLOW_STRENGTH, target,
                lookOf(1 shl PARAM_GLOW_STRENGTH), "panel3d.mat.glow.strength", maxPct = 2000f)
            KitHint(stringResource(R.string.m3d_glow_hint))
        }
        // Um material escolhido: a textura DELE (arquivo ou camada da timeline), como antes.
        if (target >= 0) {
            Spacer(Modifier.height(4.dp))
            KitHint(stringResource(R.string.m3d_texture_material_hint))
            MaterialTextureRow(env, target, Color(values[2].coerceIn(0f, 1f), values[3].coerceIn(0f, 1f), values[4].coerceIn(0f, 1f)))
        }
    }
}

/** Uma bola grande (64 dp) com o nome embaixo; a escolhida tem o anel de destaque. */
@Composable
private fun LookBall(label: String, thumb: ImageBitmap?, fallback: Color, on: Boolean, modifier: Modifier, onClick: () -> Unit) {
    Column(
        modifier.width(70.dp)
            .semantics(mergeDescendants = true) { selected = on; role = Role.Button; contentDescription = label }
            .tocavel(shrink = 0.96f, onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier.size(64.dp).clip(CircleShape)
                .border(if (on) 2.5.dp else 1.dp, if (on) AureaColors.Accent else AureaColors.Border, CircleShape)
                .padding(4.dp),
            contentAlignment = Alignment.Center,
        ) { MaterialBall(thumb, fallback, 56.dp) }
        Spacer(Modifier.height(5.dp))
        Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, fontWeight = if (on) FontWeight.W700 else FontWeight.W500,
                color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

/**
 * Uma cor do material (a cor do modelo, ou a do BRILHO na v76): amostra grande; tocar
 * escolhe a linha (o ◆ da esquerda marca R, G e B juntos — [mask]). [linear] = os três
 * canais lineares a partir de [firstParam].
 */
@Composable
private fun ModelColorRow(
    env: PanelEnv, label: String, linear: FloatArray, firstParam: Int, mask: Int, target: Int, look: KeyframeLook,
    locked: Boolean, tag: String, gesture: String,
) {
    val store = env.store
    val display = engineToDisplay(floatArrayOf(linear[0], linear[1], linear[2], 1f))
    fun open() {
        if (locked) return
        store.beginGesture(gesture)
        env.openColor(ColorRequest(display.copyOf(), onChange = { r, g, b, _ ->
            val next = displayToEngine(r, g, b, 1f)
            for (c in 0..2) store.setMaterialParameter(target, firstParam + c, next[c])
        }, onDone = { store.endGesture() }, withAlpha = false))
    }
    val toggle = { if (!locked) store.toggleModelMaterialKeys(target, mask) }
    KeyRowSync(tag, look, toggle)
    val chosen = Model3DKeys.target == tag
    Row(
        Modifier.fillMaxWidth().heightIn(min = 60.dp).testTag(tag)
            .clip(androidx.compose.foundation.shape.RoundedCornerShape(10.dp))
            .background(if (chosen) AureaColors.Accent.copy(alpha = 0.12f) else Color.Transparent)
            .tocavel(shrink = 1f) { Model3DKeys.claim(tag, look, toggle) }
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600,
            color = if (look != KeyframeLook.None) AureaColors.Accent else AureaColors.Text)))
        ColorWell(rgbaColor(display), Modifier.size(44.dp).testTag("$tag.picker").semantics { contentDescription = label }) {
            Model3DKeys.claim(tag, look, toggle); open()
        }
    }
}

/** Régua grande de um canal do material (0–100 %; a força do brilho vai a [maxPct]), com o ◆ dela. */
@Composable
private fun MaterialSlider(env: PanelEnv, label: String, value01: Float, param: Int, target: Int, look: KeyframeLook, tag: String,
                           maxPct: Float = 100f) {
    val store = env.store
    val gesture = "material do modelo 3D"
    val toggle = { store.toggleModelMaterialKeys(target, 1 shl param) }
    KeyRowSync(tag, look, toggle)
    Box(Modifier.testTag(tag)) {
        HumanRow(
            env, label, value01 * 100f, if (maxPct > 100f) 5f else 0.5f, 0f, maxPct, "%", 0, null,
            onStart = { Model3DKeys.claim(tag, look, toggle); store.beginGesture(gesture) },
            onValue = { store.setMaterialParameter(target, param, it / 100f) },
            onEnd = { store.endGesture() },
            onCommit = { v -> store.beginGesture(gesture); store.setMaterialParameter(target, param, v / 100f); store.endGesture() },
            selected = Model3DKeys.target == tag,
            onSelect = { Model3DKeys.claim(tag, look, toggle) },
            keyframe = look,
        )
    }
}

/** Um botão grande da Posição (ícone em cima, nome embaixo). */
@Composable
private fun PlaceTile(glyph: Char, label: String, tag: String, locked: Boolean, modifier: Modifier, onClick: () -> Unit) {
    Column(
        modifier.heightIn(min = 68.dp).clip(androidx.compose.foundation.shape.RoundedCornerShape(12.dp))
            .background(AureaColors.Chip).testTag(tag)
            .semantics(mergeDescendants = true) { role = Role.Button; contentDescription = label }
            .tocavel(shrink = 0.96f) { if (!locked) onClick() }
            .padding(horizontal = 6.dp, vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        com.aurea.aurea.ui.theme.CupertinoIcon(glyph, 22.dp, if (locked) AureaColors.Muted else AureaColors.Text)
        Spacer(Modifier.height(6.dp))
        Text(label, maxLines = 2, textAlign = TextAlign.Center,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, fontWeight = FontWeight.W600,
                color = if (locked) AureaColors.Muted else AureaColors.Text)))
    }
}

// --- Textura ---------------------------------------------------------------------

@Composable
internal fun ModelTextureTab(env: PanelEnv) {
    val store = env.store
    val layer = store.primary ?: return
    val names = remember(layer, store.curveRevision) { store.modelPartNames() }
    Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp).testTag("panel3d.modelTexture")) {
        if (names.isEmpty()) {
            Spacer(Modifier.height(10.dp))
            KitHint(stringResource(R.string.m3d_texture_no_parts))
            return@Column
        }
        var onePart by rememberSaveable(layer) { mutableStateOf(false) }
        KitTitle(stringResource(R.string.m3d_apply_to))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            KitChip(stringResource(R.string.m3d_whole_model), !onePart, Modifier.testTag("panel3d.texture.whole")) { onePart = false }
            KitChip(stringResource(R.string.m3d_one_part) + " ›", onePart, Modifier.testTag("panel3d.texture.part")) { onePart = true }
        }
        Spacer(Modifier.height(6.dp))
        if (onePart) Parts3DEditor(env, names) else WholeModelTexture(env)
    }
}

// --- Animação --------------------------------------------------------------------

@Composable
internal fun ModelAnimationTab(env: PanelEnv) {
    val store = env.store
    val layer = store.primary ?: return
    val clipRevision = store.modelClipRevision
    val revision = store.curveRevision
    val playhead = store.playhead
    val detail = store.detail
    val clips = remember(layer, clipRevision, revision) { store.modelAnimationClips(layer) }
    val anim = remember(layer, clipRevision, revision, playhead, detail) { store.modelAnimation(layer) } ?: return
    val locked = detail?.locked == true
    Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp).testTag("panel3d.modelAnim")) {
        KitTitle(stringResource(R.string.ae_model_clip))
        if (clips.isEmpty()) {
            KitHint(stringResource(R.string.ae_model_clip_empty))
            return@Column
        }
        val clip = anim[0].toInt()
        ChipRow {
            Box(Modifier.testTag("model3d.clip.none")) {
                KitChip(stringResource(R.string.ae_model_clip_none), on = clip < 0) { if (!locked) store.setModelAnimationClip(layer, -1) }
            }
            clips.forEachIndexed { i, name ->
                Box(Modifier.testTag("model3d.clip.$i")) {
                    KitChip(name.ifBlank { stringResource(R.string.ae_model_clip_n, i + 1) }, on = clip == i) {
                        if (!locked) store.setModelAnimationClip(layer, i)
                    }
                }
            }
        }
        if (clip < 0) {
            Spacer(Modifier.height(6.dp))
            KitHint(stringResource(R.string.m3d_anim_pick_clip))
            return@Column
        }
        KitHint(stringResource(R.string.m3d_anim_duration, numeroPtBr(anim[1], 2)))
        Spacer(Modifier.height(10.dp))
        // EVOLUÇÃO: onde o clipe está (0–100 %), com ◆ — a animação do arquivo por keyframes.
        val keyed = anim[6] > 0.5f
        val look = when { anim[7] > 0.5f -> KeyframeLook.KeyHere; keyed -> KeyframeLook.Animated; else -> KeyframeLook.None }
        val gesture = "evolução da animação"
        val progressTag = "panel3d.anim.progress"
        val toggleProgress = { if (!locked) store.toggleModelAnimationKey(layer) }
        // A Evolução vem escolhida (v76: a Velocidade também tem ◆; tocar nela a escolhe).
        if (Model3DKeys.target?.startsWith("panel3d.anim.") != true) Model3DKeys.claim(progressTag, look, toggleProgress)
        KeyRowSync(progressTag, look, toggleProgress)
        Box(Modifier.testTag(progressTag)) {
            HumanRow(
                env, stringResource(R.string.m3d_anim_progress), anim[3] * 100f, 0.25f, 0f, 100f, "%", 1, null,
                onStart = { Model3DKeys.claim(progressTag, look, toggleProgress); store.beginGesture(gesture) },
                onValue = { if (!locked) store.setModelAnimationParam(layer, 0, it / 100f) },
                onEnd = { store.endGesture() },
                onCommit = { v -> if (!locked) { store.beginGesture(gesture); store.setModelAnimationParam(layer, 0, v / 100f); store.endGesture() } },
                selected = Model3DKeys.target == progressTag,
                onSelect = { Model3DKeys.claim(progressTag, look, toggleProgress) },
                keyframe = look,
            )
        }
        KitHint(stringResource(R.string.m3d_anim_progress_hint))
        Spacer(Modifier.height(10.dp))
        val playsAlone = anim[2] < 0f && !keyed
        if (!playsAlone) {
            PanelItemRow(CupertinoGlyph.PlayFill, stringResource(R.string.m3d_anim_auto), "panel3d.anim.auto") {
                if (!locked) store.setModelAnimationParam(layer, 0, -1f)
            }
            KitHint(stringResource(R.string.m3d_anim_auto_hint))
        } else {
            // Tocando sozinho: Repetir, Velocidade (com ◆), Começar em, De trás para frente e
            // Ida e volta (com a Evolução marcada, quem manda é ela).
            Box(Modifier.testTag("panel3d.anim.loop")) {
                ToggleLine(stringResource(R.string.m3d_anim_loop), anim[4] > 0.5f) { on ->
                    if (!locked) store.setModelAnimationParam(layer, 1, if (on) 1f else 0f)
                }
            }
            val extras = anim.size >= 14
            val speedTag = "panel3d.anim.speed"
            val speedLook = when {
                extras && anim[12] > 0.5f -> KeyframeLook.KeyHere
                extras && anim[11] > 0.5f -> KeyframeLook.Animated
                else -> KeyframeLook.None
            }
            val toggleSpeed = { if (!locked) store.toggleModelAnimationParamKey(layer, 2) }
            KeyRowSync(speedTag, speedLook, toggleSpeed)
            val speed = "velocidade da animação"
            Box(Modifier.testTag(speedTag)) {
                HumanRow(
                    env, stringResource(R.string.m3d_anim_speed), (if (extras) anim[13] else anim[5]) * 100f, 1f, 5f, 400f, "%", 0, 100f,
                    onStart = { Model3DKeys.claim(speedTag, speedLook, toggleSpeed); store.beginGesture(speed) },
                    onValue = { if (!locked) store.setModelAnimationParam(layer, 2, it / 100f) },
                    onEnd = { store.endGesture() },
                    onCommit = { v -> if (!locked) { store.beginGesture(speed); store.setModelAnimationParam(layer, 2, v / 100f); store.endGesture() } },
                    selected = Model3DKeys.target == speedTag,
                    onSelect = { Model3DKeys.claim(speedTag, speedLook, toggleSpeed) },
                    keyframe = speedLook,
                )
            }
            KitHint(stringResource(R.string.m3d_anim_speed_hint))
            if (extras) {
                // Começar em: em segundos do clipe (o motor guarda a fração).
                val duration = anim[1].coerceAtLeast(0.01f)
                val start = "começo da animação"
                Box(Modifier.testTag("panel3d.anim.start")) {
                    HumanRow(
                        env, stringResource(R.string.m3d_anim_start), anim[8] * duration, 0.01f, 0f, duration, "s", 2, 0f,
                        onStart = { store.beginGesture(start) },
                        onValue = { if (!locked) store.setModelAnimationParam(layer, 3, it / duration) },
                        onEnd = { store.endGesture() },
                        onCommit = { v -> if (!locked) { store.beginGesture(start); store.setModelAnimationParam(layer, 3, v / duration); store.endGesture() } },
                    )
                }
                Box(Modifier.testTag("panel3d.anim.reverse")) {
                    ToggleLine(stringResource(R.string.m3d_anim_reverse), anim[9] > 0.5f) { on ->
                        if (!locked) store.setModelAnimationParam(layer, 4, if (on) 1f else 0f)
                    }
                }
                Box(Modifier.testTag("panel3d.anim.pingpong")) {
                    ToggleLine(stringResource(R.string.m3d_anim_pingpong), anim[10] > 0.5f) { on ->
                        if (!locked) store.setModelAnimationParam(layer, 5, if (on) 1f else 0f)
                    }
                }
            }
        }

    }
}
