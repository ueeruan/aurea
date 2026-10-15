package com.aurea.aurea.editor.panels

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Shape3DCatalog
import com.aurea.aurea.state.Shape3DInfo
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.PropertyRow
import com.aurea.aurea.ui.ds.VectorPropertyRow
import com.aurea.aurea.ui.ds.compactPropertyNumber
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/** Qual aba do painel 3D mostra a forma: Material (cor e imagem) ou Forma (partes). */
internal enum class Shape3DPage { Material, Shape }

/**
 * FORMA 3D no painel 3D: as PARTES em fichas ("Tudo" = a forma inteira),
 * nas duas abas — Material mostra a cor e a imagem da parte; Forma, o
 * losango, o reset e as réguas dela.
 * Escolher uma parte leva o gizmo e o dedo do palco para ela; aqui ficam a
 * cor, a imagem da galeria, o losango de keyframe (as 9 trilhas da parte no
 * cabeçote), o reset e as réguas de posição/giro/escala — o mesmo motor de
 * keyframes das outras propriedades (preview = export).
 */
@Composable
internal fun Shape3DSection(env: PanelEnv, info: Shape3DInfo, page: Shape3DPage = Shape3DPage.Material) {
    val store = env.store
    val part = store.shapePartOf(store.primary)
    ShapeTitle(stringResource(Shape3DCatalog.names.getOrElse(info.kind) { R.string.shape3d_title }) + " · " + stringResource(R.string.shape3d_parts))
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        ShapeChip(stringResource(R.string.shape3d_whole), on = part < 0, modifier = Modifier.testTag("shape3d.part.all")) { store.chooseShapePart(-1) }
        val layer = store.primary
        for (i in 0 until info.partCount) {
            val nameRes = Shape3DCatalog.partName(info.kind, i)
            val label = if (Shape3DCatalog.numbered(info.kind, i)) stringResource(nameRes, i + 1) else stringResource(nameRes)
            val c = info.colors[i]
            // A bola do material da parte (cor + imagem da galeria), feita pelo motor.
            val thumb = if (layer != null) rememberMaterialThumb(store, layer, i, store.curveRevision) else null
            ShapeChip(label, on = part == i, dot = Color(c[0], c[1], c[2]), thumb = thumb, modifier = Modifier.testTag("shape3d.part.$i")) {
                store.chooseShapePart(if (part == i) -1 else i)
            }
        }
    }
    Spacer(Modifier.height(10.dp))
    if (page == Shape3DPage.Material) {
        // Cor: da parte, ou de todas (a ficha mostra a da 1ª).
        val shown = info.colors.getOrNull(part.coerceAtLeast(0)) ?: floatArrayOf(1f, 1f, 1f, 1f)
        Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.panel_cor), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ColorWell(Color(shown[0], shown[1], shown[2])) {
                val target = part
                store.beginGesture("cor da parte")
                env.openColor(ColorRequest(shown.copyOf(), onChange = { r, g, b, _ ->
                    store.setShapePartColor(target, floatArrayOf(r, g, b, 1f))
                }, onDone = { store.endGesture() }))
            }
        }
        // Imagem no material da parte (ou de todas, "Tudo"): a textura de cor
        // base da forma. Tinha saído na reorganização da aba 3D (relato 1.2.0:
        // "material no longer shows add image"); o efeito "Trocar textura 3D"
        // continua para trocar por parte com repetição/keyframes.
        val hasImage = if (part >= 0) info.images.getOrElse(part) { false } else info.images.any { it }
        val imagePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            if (uri != null) store.setShapePartImage(part, uri)
        }
        Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.shape3d_image), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ShapeChip(stringResource(if (hasImage) R.string.shape3d_image_change else R.string.shape3d_image_pick), on = hasImage,
                modifier = Modifier.testTag("shape3d.image.pick")) { imagePicker.launch(arrayOf("image/*")) }
            if (hasImage) {
                Spacer(Modifier.width(6.dp))
                ShapeChip(stringResource(R.string.shape3d_image_clear), on = false, modifier = Modifier.testTag("shape3d.image.clear")) { store.clearShapePartImage(part) }
            }
        }
        Spacer(Modifier.height(14.dp))
        return
    }
    if (part < 0) {
        // Só o cubo (e as fatias dele) divide em partes.
        if (info.kind == 0) SplitCubeBox(store)
        Spacer(Modifier.height(8.dp))
        return
    }
    val values = store.shapePartValues(part) ?: return
    val (animated, here) = store.shapePartKeyBits(part)
    // Preserve existing part tracks alongside the part effects below.
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        ShapeChip((if (here != 0) "◆ " else "◇ ") + stringResource(if (here != 0) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui),
            on = here != 0, modifier = Modifier.testTag("shape3d.key")) { store.toggleShapePartKey(part) }
        ShapeChip(stringResource(R.string.shape3d_reset), on = false) { store.resetShapePart(part) }
    }
    val pos = stringResource(R.string.fx_posicao)
    val rot = stringResource(R.string.panel_rotacao)
    PartVectorRuler(env, part, pos, values, 0, .005f, -5f, 5f, "", 2, animated, here)
    PartVectorRuler(env, part, rot, values, 3, 1f, -3600f, 3600f, "°", 1, animated, here)
    val scale = values[6]
    val scaleLabel = stringResource(R.string.panel_escala)
    val owner = store.primary; val generation = store.projectGeneration; val composition = store.composition?.id
    fun setScale(value: Float) {
        if (store.primary != owner || store.projectGeneration != generation || store.composition?.id != composition || store.shapePartOf(owner) != part || store.detail?.locked == true || !value.isFinite()) return
        store.setShapePartTransform(part, FloatArray(9) { if (it >= 6) value / 100f else 0f }, 0b111 shl 6)
    }
    androidx.compose.runtime.key(generation, composition, owner, part, "scale") {
        PropertyRow(scaleLabel, scale * 100f, .5f, 1f, 2000f, { "${numeroPtBr(it, 1)}%" },
            false, {}, { store.beginGesture("parte da forma 3D") }, ::setScale, store::endGesture,
            onTapValue = { env.openKeypad(KeypadRequest(scaleLabel, scale * 100f, "%", 1f, 2000f, 1, onConfirm = ::setScale)) },
            keyframe = if (here and (0b111 shl 6) != 0) KeyframeLook.KeyHere else if (animated and (0b111 shl 6) != 0) KeyframeLook.Animated else KeyframeLook.None,
            enabled = store.detail?.locked != true, modifier = Modifier.testTag("shape3d.part.scale"))
    }
    ShapeHint(stringResource(R.string.shape3d_part_hint))
    Spacer(Modifier.height(8.dp))
}

/**
 * "Dividir em partes" do cubo: eixo do corte (X/Y/Z), quantas partes (2..16,
 * padrão 3) e o botão. Quem divide é o motor (Engine::split_shape3d).
 */
@Composable
private fun SplitCubeBox(store: EditorStore) {
    var axis by rememberSaveable { mutableIntStateOf(0) }
    var count by rememberSaveable { mutableIntStateOf(3) }
    Spacer(Modifier.height(14.dp))
    ShapeTitle(stringResource(R.string.shape3d_split_title))
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.shape3d_split_axis), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        for (a in 0..2) {
            val name = "XYZ"[a].toString()
            val desc = stringResource(R.string.shape3d_split_axis_desc, name)
            ShapeChip(name, on = axis == a, modifier = Modifier.testTag("shape3d.split.axis.$a")
                .semantics { contentDescription = desc; selected = axis == a }) { axis = a }
            if (a < 2) Spacer(Modifier.width(6.dp))
        }
    }
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.shape3d_split_count), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        val less = stringResource(R.string.shape3d_split_less)
        val more = stringResource(R.string.shape3d_split_more)
        val countDesc = stringResource(R.string.shape3d_split_count_desc, count)
        ShapeChip("−", on = false, modifier = Modifier.testTag("shape3d.split.less").semantics { contentDescription = less }) {
            count = (count - 1).coerceAtLeast(2)
        }
        Text("$count", modifier = Modifier.width(36.dp).semantics { contentDescription = countDesc }, textAlign = TextAlign.Center,
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W700)))
        ShapeChip("+", on = false, modifier = Modifier.testTag("shape3d.split.more").semantics { contentDescription = more }) {
            count = (count + 1).coerceAtMost(16)
        }
    }
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        ShapeChip(stringResource(R.string.shape3d_split_apply), on = true, modifier = Modifier.testTag("shape3d.split.apply")) {
            store.splitShape3D(axis, count)
        }
    }
    ShapeHint(stringResource(R.string.shape3d_split_hint))
}

@Composable
private fun ShapeTitle(t: String) {
    Text(t, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(6.dp))
}

@Composable
private fun ShapeHint(t: String) {
    Spacer(Modifier.height(6.dp))
    Text(t, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)))
}

@Composable
private fun ShapeChip(
    label: String,
    on: Boolean,
    modifier: Modifier = Modifier,
    dot: Color? = null,
    thumb: ImageBitmap? = null,
    onClick: () -> Unit,
) {
    Row(
        modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(start = if (dot != null) 6.dp else 12.dp, end = 12.dp, top = if (dot != null) 3.dp else 6.dp,
                bottom = if (dot != null) 3.dp else 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (dot != null) {
            // Parte: a bola do material (com a imagem, se houver); a cor chapada até ela chegar.
            MaterialBall(thumb, dot, 26.dp)
            Spacer(Modifier.width(6.dp))
        }
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

/** One ruler for the chosen channel; existing part keyframe masks stay unchanged. */
@Composable
private fun PartVectorRuler(env: PanelEnv, part: Int, label: String, values: FloatArray, first: Int,
    step: Float, min: Float, max: Float, unit: String, decimals: Int, animated: Int, here: Int) {
    val store = env.store
    val owner = store.primary; val generation = store.projectGeneration; val composition = store.composition?.id
    androidx.compose.runtime.key(generation, composition, owner, part, first) {
        var axis by androidx.compose.runtime.remember { mutableIntStateOf(0) }
        fun write(component: Int, value: Float) {
            if (store.primary != owner || store.projectGeneration != generation || store.composition?.id != composition || store.shapePartOf(owner) != part || store.detail?.locked == true || !value.isFinite()) return
            val channel = first + component
            store.setShapePartTransform(part, FloatArray(9).also { it[channel] = value }, 1 shl channel)
        }
        VectorPropertyRow(label, (0..2).map { values[first + it] }, listOf("X", "Y", "Z"), axis,
            step, min, max, { numeroPtBr(it, decimals) + unit }, true, { axis = it },
            { store.beginGesture("parte da forma 3D") }, ::write, store::endGesture,
            onTapValue = { component ->
                env.openKeypad(KeypadRequest("$label · ${"XYZ"[component]}", values[first + component], unit, min, max, decimals) { write(component, it) })
            }, keyframes = (0..2).map { c ->
                val bit = 1 shl (first + c)
                if (here and bit != 0) KeyframeLook.KeyHere else if (animated and bit != 0) KeyframeLook.Animated else KeyframeLook.None
            }, enabled = store.detail?.locked != true, tag = "shape3d.part.${if (first == 0) "position" else "rotation"}",
            displayFormat = { compactPropertyNumber(it, decimals) })
    }
}
