package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.BONE_FLOATS
import com.aurea.aurea.editor.PoseAxisColors
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale

/**
 * ESQUELETO da Cena 3D (refeito em nov/2026). Par do iOS: Rig3DPanel.swift
 * (`SkeletonPanelView`). Mora na área de baixo da Cena 3D, sem cobrir a cena, e
 * fala com o MESMO osso escolhido do palco. No alto, o osso e o keyframe dele;
 * embaixo, três abas com alvos de toque de 48 dp:
 *  - Ossos: a árvore com busca (a forma garantida de pegar qualquer osso);
 *  - Osso: rotação X/Y/Z, zerar e espelhar;
 *  - Pose: keyframe de todos, copiar/colar, espelhar, zerar, resetar e mostrar.
 * Tudo que muda dado vai pelo motor (EngineRigSkeleton.cpp), um desfazer por ação.
 * Modelo sem esqueleto: o AutoRig no lugar das abas.
 */
@Composable
internal fun SkeletonPanel(store: EditorStore, modifier: Modifier = Modifier) {
    val bones = store.poseBones
    if (bones == null) { SkeletonEmpty(store, modifier); return }
    val count = bones.size / BONE_FLOATS
    // Nós e pais não mudam com a pose: a lista de nomes só refaz com outro modelo.
    val nodes = IntArray(count) { bones[it * BONE_FLOATS].toInt() }
    val parents = IntArray(count) { bones[it * BONE_FLOATS + 1].toInt() }
    val nodesKey = nodes.contentHashCode()
    val names = remember(store.primary, nodesKey) {
        nodes.map { n -> store.modelNodeName(n).ifBlank { "#$n" } }
    }
    val chosen = store.poseBone
    val chosenSlot = nodes.indexOf(chosen)
    val partner = remember(store.primary, nodesKey, chosen) { if (chosen >= 0) store.bonePartner(chosen) else -1 }
    val collapsed = remember(store.primary, nodesKey) { mutableStateListOf<Int>() }
    var query by remember(store.primary) { mutableStateOf("") }
    var tab by remember { mutableIntStateOf(0) }
    // Osso escolhido no palco: abre os pais dele na árvore.
    LaunchedEffect(chosenSlot, nodesKey) {
        var p = if (chosenSlot >= 0) parents[chosenSlot] else -1
        while (p >= 0) { collapsed.remove(p); p = parents[p] }
    }
    Column(modifier.background(AureaColors.EditorPanel).testTag("skeleton.panel")) {
        // --- O osso escolhido --------------------------------------------------------
        Row(Modifier.fillMaxWidth().height(50.dp).padding(start = 14.dp, end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(if (chosenSlot >= 0) names[chosenSlot] else stringResource(R.string.skeleton_bone_none), maxLines = 1, overflow = TextOverflow.Ellipsis,
                    color = if (chosenSlot >= 0) AureaColors.Text else AureaColors.Muted, fontSize = if (chosenSlot >= 0) 15.sp else 13.sp,
                    fontWeight = if (chosenSlot >= 0) FontWeight.W700 else FontWeight.W400, modifier = Modifier.testTag("skeleton.bone.name"))
                if (partner >= 0) {
                    val pi = nodes.indexOf(partner)
                    Text(stringResource(R.string.skeleton_partner, if (pi >= 0) names[pi] else "#$partner"), maxLines = 1, overflow = TextOverflow.Ellipsis,
                        color = AureaColors.Muted, fontSize = 12.sp)
                }
            }
            if (chosenSlot >= 0) {
                val keyed = bones[chosenSlot * BONE_FLOATS + 6] > .5f
                val keyLabel = stringResource(R.string.rig_key_bone)
                Box(Modifier.size(48.dp).semantics { contentDescription = keyLabel; selected = keyed }.testTag("skeleton.key.bone")
                    .tocavel(haptic = true, shrink = 1f) { store.togglePoseKey(false) }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(if (keyed) CupertinoGlyph.SuitDiamondFill else CupertinoGlyph.SuitDiamond, 22.dp, if (keyed) AureaColors.Keyframe else AureaColors.Text)
                }
            }
        }
        // --- Abas --------------------------------------------------------------------
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp).height(44.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Background).padding(3.dp)) {
            listOf(R.string.skeleton_bones, R.string.skeleton_tab_bone, R.string.skeleton_pose).forEachIndexed { i, label ->
                val on = i == tab
                Box(Modifier.weight(1f).fillMaxHeight().clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else Color.Transparent)
                    .semantics { selected = on }.testTag("skeleton.tab.$i").tocavel(shrink = 1f) { tab = i }, contentAlignment = Alignment.Center) {
                    Text(stringResource(label), color = if (on) AureaColors.Accent else AureaColors.Text, fontSize = 13.sp,
                        fontWeight = if (on) FontWeight.W600 else FontWeight.W400, maxLines = 1)
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        when (tab) {
            0 -> Column(Modifier.fillMaxWidth().weight(1f).padding(horizontal = 12.dp)) {
                SearchBox(query) { query = it }
                Spacer(Modifier.height(4.dp))
                val q = query.trim().lowercase(Locale.ROOT)
                val rows = remember(nodesKey, q, collapsed.toList()) {
                    if (q.isNotEmpty()) {
                        (0 until count).filter { names[it].lowercase(Locale.ROOT).contains(q) }.map { it to 0 }
                    } else {
                        // Profundidade-primeiro a partir das raízes, pulando os filhos recolhidos.
                        val children = Array(count) { ArrayList<Int>() }
                        val roots = ArrayList<Int>()
                        for (i in 0 until count) if (parents[i] in 0 until count) children[parents[i]] += i else roots += i
                        val out = ArrayList<Pair<Int, Int>>()
                        fun walk(i: Int, depth: Int) {
                            out += i to depth
                            if (i !in collapsed) for (c in children[i]) walk(c, depth + 1)
                        }
                        roots.forEach { walk(it, 0) }
                        out
                    }
                }
                if (rows.isEmpty()) KitHint(stringResource(R.string.skeleton_no_match))
                val hasChildren = remember(nodesKey) { BooleanArray(count).also { h -> for (i in 0 until count) if (parents[i] in 0 until count) h[parents[i]] = true } }
                LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
                    items(rows, key = { nodes[it.first] }) { (slot, depth) ->
                        BoneRow(
                            name = names[slot],
                            depth = depth,
                            on = slot == chosenSlot,
                            expandable = q.isEmpty() && hasChildren[slot],
                            open = slot !in collapsed,
                            keyed = bones[slot * BONE_FLOATS + 6] > .5f,
                            animated = bones[slot * BONE_FLOATS + 5] > .5f,
                            onToggle = { if (slot in collapsed) collapsed.remove(slot) else collapsed.add(slot) },
                            onPick = { store.choosePoseBone(nodes[slot]) },
                        )
                    }
                }
            }
            1 -> Column(Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 12.dp).padding(bottom = 8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (chosenSlot < 0) KitHint(stringResource(R.string.skeleton_bone_none))
                else {
                    val o = chosenSlot * BONE_FLOATS
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        val labels = arrayOf("X", "Y", "Z")
                        for (axis in 0 until 3) {
                            AxisField(labels[axis], PoseAxisColors[axis], bones[o + 2 + axis], "$chosen.$axis", Modifier.weight(1f)) { value ->
                                val v = floatArrayOf(bones[o + 2], bones[o + 3], bones[o + 4])
                                v[axis] = value
                                store.setBoneRotation(chosen, v[0], v[1], v[2])
                            }
                        }
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        SkeletonTile(CupertinoGlyph.Minus, stringResource(R.string.skeleton_zero_bone), "skeleton.zero.bone", Modifier.weight(1f)) { store.zeroPose(false) }
                        SkeletonTile(CupertinoGlyph.ArrowLeftRight, stringResource(R.string.skeleton_mirror_bone), "skeleton.mirror.bone", Modifier.weight(1f), enabled = partner >= 0) { store.mirrorPose(false) }
                        SkeletonTile(CupertinoGlyph.ArrowCounterclockwise, stringResource(R.string.rig_reset_bone), "skeleton.reset.bone", Modifier.weight(1f)) { store.resetPose(false) }
                    }
                }
            }
            else -> Column(Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 12.dp).padding(bottom = 8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    SkeletonTile(CupertinoGlyph.SuitDiamond, stringResource(R.string.rig_key_all), "skeleton.key.all", Modifier.weight(1f)) { store.togglePoseKey(true) }
                    SkeletonTile(CupertinoGlyph.DocOnDoc, stringResource(R.string.skeleton_copy), "skeleton.copy", Modifier.weight(1f)) { store.copyPose() }
                    SkeletonTile(CupertinoGlyph.DocOnClipboard, stringResource(R.string.skeleton_paste), "skeleton.paste", Modifier.weight(1f), enabled = store.poseClipboard != null) { store.pastePose() }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    SkeletonTile(CupertinoGlyph.ArrowLeftRight, stringResource(R.string.skeleton_mirror_pose), "skeleton.mirror.pose", Modifier.weight(1f)) { store.mirrorPose(true) }
                    SkeletonTile(CupertinoGlyph.Minus, stringResource(R.string.skeleton_zero_pose), "skeleton.zero.pose", Modifier.weight(1f)) { store.zeroPose(true) }
                    SkeletonTile(CupertinoGlyph.ArrowCounterclockwise, stringResource(R.string.rig_reset_all), "skeleton.reset.all", Modifier.weight(1f)) { store.resetPose(true) }
                }
                SkeletonTile(if (store.skeletonHidden) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye, stringResource(R.string.skeleton_show), "skeleton.show", Modifier.fillMaxWidth(), on = !store.skeletonHidden) {
                    store.showSkeleton(store.skeletonHidden)
                }
            }
        }
    }
}

/** Ação do esqueleto: ícone e nome num alvo de 68 dp. */
@Composable
private fun SkeletonTile(glyph: Char, label: String, tag: String, modifier: Modifier, enabled: Boolean = true, on: Boolean = false, onClick: () -> Unit) {
    Column(modifier.height(68.dp).clip(RoundedCornerShape(10.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
        .alpha(if (enabled) 1f else .35f).testTag(tag).semantics { contentDescription = label }
        .tocavel(enabled = enabled, haptic = true, shrink = 1f, onClick = onClick).padding(horizontal = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        CupertinoIcon(glyph, 20.dp, if (on) AureaColors.Accent else AureaColors.Text)
        Spacer(Modifier.height(4.dp))
        Text(label, color = if (on) AureaColors.Accent else AureaColors.Text, fontSize = 11.sp, lineHeight = 13.sp, maxLines = 2,
            overflow = TextOverflow.Ellipsis, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
    }
}

/** Modelo sem esqueleto: criar com o AutoRig, com o progresso real. */
@Composable
private fun SkeletonEmpty(store: EditorStore, modifier: Modifier) {
    val working = store.autoRigState == 1
    Column(modifier.background(AureaColors.EditorPanel).verticalScroll(rememberScrollState()).padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        if (working) {
            val pct = (store.autoRigProgress * 100f).toInt().coerceIn(0, 100)
            Text(stringResource(R.string.rig3d_working, pct), color = AureaColors.Text, fontSize = 14.sp)
            LinearProgressIndicator(progress = { store.autoRigProgress }, modifier = Modifier.fillMaxWidth(), color = AureaColors.Accent, trackColor = AureaColors.Chip)
            SkeletonTile(CupertinoGlyph.Xmark, stringResource(R.string.rig3d_cancel), "skeleton.autorig.cancel", Modifier.fillMaxWidth()) { store.cancelAutoRig() }
        } else {
            Text(stringResource(R.string.rig3d_status_none), color = AureaColors.Text, fontSize = 15.sp, fontWeight = FontWeight.W600)
            KitHint(stringResource(R.string.rig3d_tpose_hint))
            SkeletonTile(CupertinoGlyph.Sparkles, stringResource(R.string.rig3d_autorig), "skeleton.autorig", Modifier.fillMaxWidth(), on = true) { store.startAutoRig() }
        }
    }
}

/** Uma linha da árvore: recuo pela profundidade, seta de abrir/fechar, nome e o losango do keyframe. */
@Composable
private fun BoneRow(name: String, depth: Int, on: Boolean, expandable: Boolean, open: Boolean, keyed: Boolean, animated: Boolean,
                    onToggle: () -> Unit, onPick: () -> Unit) {
    val levelText = stringResource(R.string.skeleton_bone_a11y, name, depth + 1)
    val keyText = if (keyed) stringResource(R.string.skeleton_keyed) else ""
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 48.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (on) AureaColors.AccentDim else Color.Transparent)
            .semantics(mergeDescendants = false) {
                contentDescription = levelText
                if (keyText.isNotEmpty()) stateDescription = keyText
                selected = on
            }
            .tocavel(shrink = 1f, onClick = onPick)
            .padding(start = (4 + 12 * depth.coerceAtMost(10)).dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (expandable) {
            val toggle = stringResource(if (open) R.string.skeleton_collapse else R.string.skeleton_expand, name)
            Box(Modifier.size(44.dp).semantics { contentDescription = toggle }.tocavel(shrink = 1f, onClick = onToggle),
                contentAlignment = Alignment.Center) {
                CupertinoIcon(if (open) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 12.dp, AureaColors.Muted)
            }
        } else {
            Spacer(Modifier.width(44.dp))
        }
        Text(name, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f),
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = if (on) FontWeight.W700 else FontWeight.W500,
                color = if (on) AureaColors.Accent else AureaColors.Text)))
        if (keyed || animated) Text(if (keyed) "◆" else "◇", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp,
            color = if (keyed) AureaColors.Accent else AureaColors.Muted)))
    }
}

/** Campo de rotação em graus com −/+ 5°: o valor entra ao confirmar (um desfazer), não a cada tecla. */
@Composable
private fun AxisField(label: String, color: Color, value: Float, identity: String, modifier: Modifier, onCommit: (Float) -> Unit) {
    key(identity) {
        val shown = String.format(Locale.ROOT, "%.1f", value)
        var draft by remember { mutableStateOf(shown) }
        var focused by remember { mutableStateOf(false) }
        LaunchedEffect(shown, focused) { if (!focused) draft = shown }
        fun commit() {
            val v = draft.replace(',', '.').toFloatOrNull()
            if (v != null && v.isFinite() && kotlin.math.abs(v - value) > 1e-3f) onCommit(v) else draft = shown
        }
        val a11y = stringResource(R.string.skeleton_rot_a11y, label, shown)
        val minus = stringResource(R.string.skeleton_minus, label)
        val plus = stringResource(R.string.skeleton_plus, label)
        Column(modifier.clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip).padding(vertical = 4.dp),
            horizontalAlignment = Alignment.CenterHorizontally) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W700, color = color)))
            BasicTextField(
                draft, { draft = it }, singleLine = true, cursorBrush = SolidColor(AureaColors.Accent),
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text, textDirection = TextDirection.Ltr,
                    textAlign = androidx.compose.ui.text.style.TextAlign.Center)),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { commit() }),
                modifier = Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 2.dp)
                    .semantics { contentDescription = a11y }
                    .onFocusChanged { s -> if (focused && !s.isFocused) commit(); focused = s.isFocused },
            )
            Row {
                Box(Modifier.weight(1f).height(44.dp).semantics { contentDescription = minus }.tocavel(shrink = 1f) { onCommit(value - 5f) },
                    contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.Minus, 14.dp, AureaColors.Text) }
                Box(Modifier.weight(1f).height(44.dp).semantics { contentDescription = plus }.tocavel(shrink = 1f) { onCommit(value + 5f) },
                    contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.Plus, 14.dp, AureaColors.Text) }
            }
        }
    }
}

/** Busca por nome (filtra a árvore numa lista). */
@Composable
private fun SearchBox(text: String, onChange: (String) -> Unit) {
    val placeholder = stringResource(R.string.skeleton_search)
    Row(Modifier.fillMaxWidth().height(40.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip).padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically) {
        CupertinoIcon(CupertinoGlyph.Search, 14.dp, AureaColors.Muted)
        Spacer(Modifier.width(8.dp))
        Box(Modifier.weight(1f)) {
            if (text.isEmpty()) Text(placeholder, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            BasicTextField(text, onChange, singleLine = true, cursorBrush = SolidColor(AureaColors.Accent),
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                modifier = Modifier.fillMaxWidth().semantics { contentDescription = placeholder }.testTag("skeleton.search"))
        }
        if (text.isNotEmpty()) {
            val clear = stringResource(R.string.common_close)
            Box(Modifier.size(36.dp).semantics { contentDescription = clear }.tocavel(shrink = 1f) { onChange("") },
                contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.Xmark, 12.dp, AureaColors.Muted) }
        }
    }
}
