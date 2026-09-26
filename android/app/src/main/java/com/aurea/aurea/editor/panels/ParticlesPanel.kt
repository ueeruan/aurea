package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.graphics.Color
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/** Particle World: compact controls shared with the native iOS panel. */
@Composable
internal fun ParticlesPanel(env: PanelEnv) {
    val store = env.store
    val v by remember(store) { derivedStateOf { store.particles } }
    val p = v ?: run {
        Text(stringResource(R.string.panel_selecione_camada_particulas), modifier = Modifier.padding(18.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
        return
    }
    // A linha escolhida é o alvo do losango do trilho (mesmo caminho dos efeitos).
    var selected by remember(store.primary) { mutableStateOf<Int?>(null) }
    val railLook by remember(store) { derivedStateOf { selected?.let { particleLook(store, it) } ?: KeyframeLook.None } }
    val railTrack by remember(store) { derivedStateOf { selected?.let { store.primaryKeys().particleTrack(it) }.orEmpty() } }

    Row(Modifier.fillMaxSize()) {
        LeftRail(
            onBack = env.onClose,
            keyframeLook = railLook,
            onKeyframe = selected?.let { param -> { store.toggleParticleKeyframe(param) } },
            curveAnimated = railTrack.size >= 2,
            onCurve = if (railTrack.size >= 2) {
                {
                    val layer = store.primary
                    val t = store.detail?.localPlayhead
                    if (layer != null && t != null) {
                        railTrack.segmentStart(t)?.let { key ->
                            store.selectKeyframe(layer, key)
                            env.onOpenPanel(EditorPanel.Curve)
                        }
                    }
                }
            } else {
                null
            },
        )
        CompositionLocalProvider(LocalParticleSelection provides (selected to { selected = it })) {
            ParticleBody(env, p)
        }
    }
}

/** A linha escolhida do painel (param) e como escolhê-la. */
private val LocalParticleSelection = compositionLocalOf<Pair<Int?, (Int) -> Unit>> { null to {} }

@Composable
private fun ParticleBody(env: PanelEnv, p: List<Float>) {
    val store = env.store
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(18.dp)) {
        PresetRow(store)
        if (p[P.EmitterType] < 10f) {
            Text(stringResource(R.string.world_legacy), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        } else {
        Group(stringResource(R.string.particular_group_emitter))
        Dim(store, stringResource(R.string.particular_radius), P.EmitterRadius, p, 1f, 0f, 2000f, " px")
        Dim(store, stringResource(R.string.panel_particulas_segundo), P.Rate, p, 5f, 0f, 6000f, "/s")
        Dim(store, stringResource(R.string.panel_duracao_cada), P.Lifetime, p, .02f, .05f, 10f, " s", decimals = 2)
        Group(stringResource(R.string.particular_group_physics))
        Dim(store, stringResource(R.string.panel_velocidade), P.Speed, p, 2f, 0f, 4000f, " px/s")
        Dim(store, stringResource(R.string.panel_gravidade), P.GravityY, p, 4f, -4000f, 4000f, " px/s²", negate = true)
        Dim(store, stringResource(R.string.world_resistance), P.Drag, p, .02f, 0f, 10f, "", decimals = 2)
        Group(stringResource(R.string.particular_group_particle))
        Choice(store, stringResource(R.string.particular_shape), P.ParticleType) {
            listOf(stringResource(R.string.particular_shape_circle), stringResource(R.string.particular_shape_square),
                stringResource(R.string.particular_shape_streak), stringResource(R.string.particular_shape_soft))
        }
        Dim(store, stringResource(R.string.panel_tamanho_inicial), P.StartSize, p, .25f, .1f, 120f, " px", decimals = 1)
        Dim(store, stringResource(R.string.panel_tamanho_final), P.EndSize, p, .25f, 0f, 120f, " px", decimals = 1)
        Pct(store, stringResource(R.string.panel_opacidade), P.StartOpacity, p)
        LifeGradient(env)
        }
    }
}

/** Índices de `ParticleParam` no motor. Nomes legíveis, números do contrato. */
private object P {
    const val EmitterType = 0; const val EmitterWidth = 1; const val EmitterHeight = 2
    const val EmitterRadius = 3; const val EmitterRotation = 4; const val EmitterDepth = 5
    const val GridX = 6; const val GridY = 7; const val EmitFill = 8
    const val EmitterOffsetX = 9; const val EmitterOffsetY = 10
    const val Rate = 11; const val Burst = 12; const val Lifetime = 13; const val LifeRandom = 14
    const val Speed = 15; const val SpeedRandom = 16; const val Direction = 17; const val Spread = 18
    const val InheritVelocity = 19; const val Seed = 20
    const val ParticleType = 21; const val Softness = 22; const val Rotation = 23
    const val RotationRandom = 24; const val Spin = 25
    const val StartSize = 26; const val EndSize = 27; const val StartOpacity = 28; const val EndOpacity = 29
    const val GravityX = 30; const val GravityY = 31; const val GravityZ = 32; const val Drag = 33
    const val WindX = 34; const val WindY = 35; const val Turbulence = 36; const val TurbulenceScale = 37
    const val TurbulenceSpeed = 38; const val Vortex = 39; const val Attractor = 40
    const val TrailLength = 41; const val TrailTaper = 42
    const val AuxCount = 43; const val AuxAt = 44; const val AuxLife = 45; const val AuxSpeed = 46
    const val AuxSize = 47; const val AuxSpread = 48
    const val Collision = 49; const val CollisionY = 50; const val CollisionBounce = 51
    const val BlendMode = 52; const val MaxParticles = 53
    // 8.2 (v21) — só no fim: o número é contrato.
    const val EmitterSpace = 54; const val EmitFrom = 55; const val AuxProbability = 56
    const val TrailWidth = 57; const val TrailOpacity = 58
    const val SizeRandom = 59; const val OpacityRandom = 60; const val ColorRandom = 61
    const val CollisionX = 62; const val CollisionZ = 63; const val CollisionRadius = 64
    const val CollisionWidth = 65; const val CollisionHeight = 66; const val CollisionDepth = 67
    const val MeshScale = 68; const val MeshLit = 69
}

/** Escolha de uma camada do projeto (fonte, imagem, modelo), com "Nenhuma". */


/**
 * Cor ao longo da vida: até 8 paradas (posição + cor sRGB). Sem paradas vale o
 * início → fim da partícula. Cada parada tem a amostra (abre o seletor) e a
 * régua da posição; um arrasto inteiro é um passo de desfazer.
 */
@Composable
private fun LifeGradient(env: PanelEnv) {
    val store = env.store
    val saved = store.particleCurves.getOrNull(0).orEmpty().chunked(4).filter { it.size == 4 }
    val stops = if (saved.size >= 2) listOf(saved.first(), saved.last()) else
        listOf(listOf(0f,1f,1f,.3137255f), listOf(1f,.7843137f,.1568627f,.1568627f))
    Row(Modifier.padding(vertical = 12.dp), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
        stops.forEachIndexed { index, color ->
            ColorWell(Color(color[1], color[2], color[3])) {
                store.beginGesture("Particle World color")
                env.openColor(ColorRequest(floatArrayOf(color[1], color[2], color[3], 1f),
                    onChange = { r, g, b, _ ->
                        val colors = stops.toMutableList()
                        colors[index] = listOf(index.toFloat(), r, g, b)
                        store.setParticleLifeCurve(0, colors.flatten())
                    }, onDone = { store.endGesture() }))
            }
            Text(stringResource(if (index == 0) R.string.world_birth_color else R.string.world_death_color),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
        }
    }
}

/**
 * Tamanho ou opacidade ao longo da vida: pontos (posição, multiplicador) numa
 * curva suave (Hermite, presa entre os vizinhos — ver particles_extras.glsl).
 */


/** "+ Ponto" (até 8) e "voltar ao início → fim". */




/** Régua compacta de um ponto de curva (sem losango: a curva não é keyframe). */


@Composable
private fun PresetRow(store: EditorStore) {
    val names = listOf(R.string.world_explosive, R.string.world_jet, R.string.world_vortex)
    Column {
        Text(stringResource(R.string.particular_presets), style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)))
        Spacer(Modifier.height(5.dp))
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            names.forEachIndexed { preset, res ->
                Box(Modifier.clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip)
                    .tocavel(onClick = { store.applyParticlePreset(preset + 10) }).padding(horizontal = 12.dp, vertical = 6.dp)) {
                    Text(stringResource(res), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
                }
            }
        }
    }
}

@Composable
private fun Group(title: String) {
    Spacer(Modifier.height(14.dp))
    Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)))
    Spacer(Modifier.height(4.dp))
}

/** Escolha em fichas — para enum, onde arrastar não faz sentido. */
@Composable
private fun Choice(store: EditorStore, label: String, param: Int, options: @Composable () -> List<String>) {
    val current = (store.particles?.getOrNull(param) ?: 0f).roundToInt()
    val (sel, pick) = LocalParticleSelection.current
    PropertyCustomRow(label, selected = sel == param, onSelect = { pick(param) }, keyframe = particleLook(store, param)) {
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(5.dp)) {
            options().forEachIndexed { i, name ->
                val on = i == current
                Box(Modifier.clip(RoundedCornerShape(7.dp))
                    .background(if (on) AureaColors.Accent else AureaColors.Chip)
                    .tocavel(onClick = { store.setParticleParam(param, i.toFloat()) })
                    .padding(horizontal = 10.dp, vertical = 5.dp)) {
                    Text(name, style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp,
                        color = if (on) AureaColors.OnAccent else AureaColors.Text)))
                }
            }
        }
    }
}



@Composable
private fun Dim(store: EditorStore, label: String, param: Int, p: List<Float>,
                unitsPerDp: Float, min: Float, max: Float, unit: String, negate: Boolean = false, decimals: Int = 0) {
    val raw = p.getOrNull(param) ?: 0f
    val shown = if (negate) -raw else raw
    val text = if (decimals > 0) numeroPtBr(shown, decimals) + unit else "${shown.roundToInt()}$unit"
    Ruler(store, label, param, raw, text, unitsPerDp, min, max, negate)
}



@Composable
private fun Pct(store: EditorStore, label: String, param: Int, p: List<Float>, unitsPerDp: Float = 0.01f) {
    val raw = p.getOrNull(param) ?: 0f
    Ruler(store, label, param, raw, "${(raw * 100f).roundToInt()}%", unitsPerDp, 0f, 1f)
}

@Composable
private fun Ruler(store: EditorStore, label: String, param: Int, value: Float,
                  text: String, unitsPerDp: Float, min: Float, max: Float, negate: Boolean = false) {
    val (sel, pick) = LocalParticleSelection.current
    PropertyCustomRow(label, selected = sel == param, onSelect = { pick(param) }, keyframe = particleLook(store, param)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { (store.particles?.get(param) ?: value).let { if (negate) -it else it } },
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { (store.particles?.get(param) ?: value).let { if (negate) -it else it } },
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture("partículas") },
                        onValue = { store.setParticleParam(param, if (negate) -it else it) },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(numeroPtBr(value, 1).let { text }, onTap = null)
        }
    }
}
