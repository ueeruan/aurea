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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
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

/**
 * AUREA PARTICULAR — o painel.
 *
 * Organizado em SEIS grupos, e nessa ordem de propósito: Emissor (de onde sai),
 * Partícula (o que sai), Física (para onde vai), Aux (o que ela gera), Sorte
 * (como aparece) e Avançado (o que quase ninguém mexe). Nada de cem
 * controles numa tela só.
 *
 * Os índices são `ParticleParam` no motor: o número é contrato, não posição
 * na tela. Um controle que muda de lugar não muda de significado.
 */
@Composable
internal fun ParticlesPanel(env: PanelEnv) {
    val store = env.store
    val v by remember(store) { derivedStateOf { store.particles } }
    val p = v ?: run {
        Text(stringResource(R.string.panel_selecione_camada_particulas), modifier = Modifier.padding(18.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
        return
    }
    var advanced by remember { mutableStateOf(false) }
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
            ParticleBody(env, p, advanced) { advanced = it }
        }
    }
}

/** A linha escolhida do painel (param) e como escolhê-la. */
private val LocalParticleSelection = compositionLocalOf<Pair<Int?, (Int) -> Unit>> { null to {} }

@Composable
private fun ParticleBody(env: PanelEnv, p: List<Float>, advanced: Boolean, setAdvanced: (Boolean) -> Unit) {
    val store = env.store
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        PresetRow(store)
        Spacer(Modifier.height(10.dp))

        val emitter = p[P.EmitterType].roundToInt()
        Group(stringResource(R.string.particular_group_emitter))
        Choice(store, stringResource(R.string.particular_emitter), P.EmitterType) {
            listOf(
                stringResource(R.string.particular_emitter_point),
                stringResource(R.string.particular_emitter_box),
                stringResource(R.string.particular_emitter_sphere),
                stringResource(R.string.particular_emitter_disc),
                stringResource(R.string.particular_emitter_line),
                stringResource(R.string.particular_emitter_grid),
            )
        }
        when (emitter) {
            1 -> { Dim(store, stringResource(R.string.particular_width), P.EmitterWidth, p, 1f, 0f, 4000f, " px")
                   Dim(store, stringResource(R.string.particular_height), P.EmitterHeight, p, 1f, 0f, 4000f, " px") }
            2 -> { Dim(store, stringResource(R.string.particular_radius), P.EmitterRadius, p, 0.5f, 0f, 4000f, " px")
                   Toggle(store, stringResource(R.string.particular_fill), P.EmitFill, p) }
            3 -> Dim(store, stringResource(R.string.particular_radius), P.EmitterRadius, p, 0.5f, 0f, 4000f, " px")
            4 -> { Dim(store, stringResource(R.string.particular_length), P.EmitterWidth, p, 1f, 0f, 4000f, " px")
                   Angle(store, stringResource(R.string.particular_rotation), P.EmitterRotation, p) }
            5 -> { Dim(store, stringResource(R.string.particular_width), P.EmitterWidth, p, 1f, 1f, 4000f, " px")
                   Dim(store, stringResource(R.string.particular_height), P.EmitterHeight, p, 1f, 1f, 4000f, " px")
                   Dim(store, stringResource(R.string.particular_cols), P.GridX, p, 0.1f, 1f, 64f, "")
                   Dim(store, stringResource(R.string.particular_rows), P.GridY, p, 0.1f, 1f, 64f, "") }
        }
        Dim(store, stringResource(R.string.particular_offset_x), P.EmitterOffsetX, p, 1f, -4000f, 4000f, " px")
        Dim(store, stringResource(R.string.particular_offset_y), P.EmitterOffsetY, p, 1f, -4000f, 4000f, " px")

        Group(stringResource(R.string.particular_group_emission))
        Dim(store, stringResource(R.string.panel_particulas_segundo), P.Rate, p, 0.2f, 0f, 5000f, "/s")
        Dim(store, stringResource(R.string.particular_burst), P.Burst, p, 0.2f, 0f, 20000f, "")
        Dim(store, stringResource(R.string.panel_duracao_cada), P.Lifetime, p, 0.02f, 0.05f, 60f, " s")
        Pct(store, stringResource(R.string.particular_life_random), P.LifeRandom, p)
        Dim(store, stringResource(R.string.panel_velocidade), P.Speed, p, 4f, 0f, 8000f, " px/s")
        Pct(store, stringResource(R.string.particular_speed_random), P.SpeedRandom, p)
        Angle(store, stringResource(R.string.panel_direcao), P.Direction, p, -360f, 360f)
        Angle(store, stringResource(R.string.panel_abertura), P.Spread, p)
        Pct(store, stringResource(R.string.particular_inherit), P.InheritVelocity, p, 2f)
        Dim(store, stringResource(R.string.particular_seed), P.Seed, p, 0.2f, 0f, 100000f, "")

        Group(stringResource(R.string.particular_group_particle))
        Choice(store, stringResource(R.string.particular_shape), P.ParticleType) {
            listOf(
                stringResource(R.string.particular_shape_circle),
                stringResource(R.string.particular_shape_square),
                stringResource(R.string.particular_shape_streak),
                stringResource(R.string.particular_shape_soft),
            )
        }
        Dim(store, stringResource(R.string.panel_tamanho_inicial), P.StartSize, p, 0.5f, 0f, 2000f, " px")
        Dim(store, stringResource(R.string.panel_tamanho_final), P.EndSize, p, 0.5f, 0f, 2000f, " px")
        Pct(store, stringResource(R.string.panel_opacidade_inicial), P.StartOpacity, p)
        Pct(store, stringResource(R.string.panel_opacidade_final), P.EndOpacity, p)
        Angle(store, stringResource(R.string.panel_rotacao), P.Rotation, p)
        Angle(store, stringResource(R.string.particular_rotation_random), P.RotationRandom, p, 0f, 360f)
        Dim(store, stringResource(R.string.panel_giro), P.Spin, p, 2f, -3600f, 3600f, " °/s")

        Group(stringResource(R.string.particular_group_physics))
        Dim(store, stringResource(R.string.particular_gravity_x), P.GravityX, p, 8f, -8000f, 8000f, " px/s²")
        Dim(store, stringResource(R.string.panel_gravidade), P.GravityY, p, 8f, -8000f, 8000f, " px/s²", negate = true)
        Dim(store, stringResource(R.string.particular_wind), P.WindX, p, 4f, -8000f, 8000f, " px/s²", negate = true)
        Pct(store, stringResource(R.string.particular_drag), P.Drag, p, 4f)
        Dim(store, stringResource(R.string.particular_turbulence), P.Turbulence, p, 0.5f, 0f, 2000f, "")
        Dim(store, stringResource(R.string.particular_turb_scale), P.TurbulenceScale, p, 0.05f, 0.05f, 20f, "x")
        Dim(store, stringResource(R.string.particular_turb_speed), P.TurbulenceSpeed, p, 0.05f, 0f, 20f, "x")
        Dim(store, stringResource(R.string.particular_vortex), P.Vortex, p, 4f, -3600f, 3600f, " °/s")
        Dim(store, stringResource(R.string.particular_attractor), P.Attractor, p, 0.5f, -100f, 100f, "")

        Group(stringResource(R.string.particular_group_aux))
        Dim(store, stringResource(R.string.particular_aux_count), P.AuxCount, p, 0.1f, 0f, 16f, "")
        if (p[P.AuxCount].roundToInt() > 0) {
            Pct(store, stringResource(R.string.particular_aux_at), P.AuxAt, p)
            Dim(store, stringResource(R.string.panel_duracao_cada), P.AuxLife, p, 0.02f, 0.05f, 20f, " s")
            Dim(store, stringResource(R.string.panel_velocidade), P.AuxSpeed, p, 2f, 0f, 5000f, " px/s")
            Dim(store, stringResource(R.string.panel_tamanho), P.AuxSize, p, 0.3f, 0f, 500f, " px")
            Angle(store, stringResource(R.string.panel_abertura), P.AuxSpread, p)
        }

        Group(stringResource(R.string.particular_group_trail))
        Dim(store, stringResource(R.string.particular_trail_len), P.TrailLength, p, 0.002f, 0f, 2f, " s")
        Pct(store, stringResource(R.string.particular_trail_taper), P.TrailTaper, p)

        Group(stringResource(R.string.particular_group_render))
        Choice(store, stringResource(R.string.particular_blend), P.BlendMode) {
            listOf(stringResource(R.string.particular_blend_normal), stringResource(R.string.particular_blend_add))
        }
        Pct(store, stringResource(R.string.particular_softness), P.Softness, p)
        Choice(store, stringResource(R.string.particular_collision), P.Collision) {
            listOf(stringResource(R.string.particular_collision_none), stringResource(R.string.particular_collision_plane))
        }
        if (p[P.Collision].roundToInt() == 1) {
            Dim(store, stringResource(R.string.particular_collision_y), P.CollisionY, p, 1f, -4000f, 4000f, " px")
            Pct(store, stringResource(R.string.particular_bounce), P.CollisionBounce, p)
        }

        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(9.dp)).background(AureaColors.Chip)
            .tocavel { setAdvanced(!advanced) }.padding(horizontal = 12.dp, vertical = 9.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text(if (advanced) "▾ " else "▸ " + stringResource(R.string.particular_advanced),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, color = AureaColors.Text)))
        }
        if (advanced) {
            Dim(store, stringResource(R.string.particular_max_particles), P.MaxParticles, p, 20f, 1f, 1000000f, "")
            Angle(store, stringResource(R.string.particular_emitter_rotation), P.EmitterRotation, p)
            Dim(store, stringResource(R.string.particular_depth), P.EmitterDepth, p, 1f, 0f, 4000f, " px")
            Dim(store, stringResource(R.string.particular_gravity_z), P.GravityZ, p, 8f, -8000f, 8000f, " px/s²")
            Dim(store, stringResource(R.string.particular_wind_y), P.WindY, p, 4f, -8000f, 8000f, " px/s²")
        }
        Spacer(Modifier.height(6.dp))
        Text(stringResource(R.string.particular_deterministic_note),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)))
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
}

@Composable
private fun PresetRow(store: EditorStore) {
    val names = listOf(
        R.string.panel_faiscas, R.string.panel_neve, R.string.panel_poeira_luz,
        R.string.preset_rain, R.string.preset_fireflies, R.string.preset_embers,
        R.string.preset_confetti, R.string.preset_starfield, R.string.preset_magic_dust,
        R.string.preset_logo_burst,
    )
    Column {
        Text(stringResource(R.string.particular_presets), style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)))
        Spacer(Modifier.height(5.dp))
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            names.forEachIndexed { preset, res ->
                Box(Modifier.clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip)
                    .tocavel(onClick = { store.applyParticlePreset(preset) }).padding(horizontal = 12.dp, vertical = 6.dp)) {
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
private fun Toggle(store: EditorStore, label: String, param: Int, p: List<Float>) {
    val on = (p.getOrNull(param) ?: 0f) >= 0.5f
    val (sel, pick) = LocalParticleSelection.current
    PropertyCustomRow(label, selected = sel == param, onSelect = { pick(param) }, keyframe = particleLook(store, param)) {
        Box(Modifier.clip(RoundedCornerShape(7.dp))
            .background(if (on) AureaColors.Accent else AureaColors.Chip)
            .tocavel(onClick = { store.setParticleParam(param, if (on) 0f else 1f) })
            .padding(horizontal = 12.dp, vertical = 5.dp)) {
            Text(if (on) stringResource(R.string.common_on) else stringResource(R.string.common_off),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp,
                    color = if (on) AureaColors.OnAccent else AureaColors.Text)))
        }
    }
}

@Composable
private fun Dim(store: EditorStore, label: String, param: Int, p: List<Float>,
                unitsPerDp: Float, min: Float, max: Float, unit: String, negate: Boolean = false) {
    val raw = p.getOrNull(param) ?: 0f
    val shown = if (negate) -raw else raw
    Ruler(store, label, param, raw, "${shown.roundToInt()}$unit", unitsPerDp, min, max)
}

@Composable
private fun Angle(store: EditorStore, label: String, param: Int, p: List<Float>, min: Float = -180f, max: Float = 180f) {
    val raw = p.getOrNull(param) ?: 0f
    Ruler(store, label, param, raw, "${raw.roundToInt()}°", 1f, min, max)
}

@Composable
private fun Pct(store: EditorStore, label: String, param: Int, p: List<Float>, unitsPerDp: Float = 0.01f) {
    val raw = p.getOrNull(param) ?: 0f
    Ruler(store, label, param, raw, "${(raw * 100f).roundToInt()}%", unitsPerDp, 0f, 1f)
}

@Composable
private fun Ruler(store: EditorStore, label: String, param: Int, value: Float,
                  text: String, unitsPerDp: Float, min: Float, max: Float) {
    val (sel, pick) = LocalParticleSelection.current
    PropertyCustomRow(label, selected = sel == param, onSelect = { pick(param) }, keyframe = particleLook(store, param)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { store.particles?.get(param) ?: value },
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { store.particles?.get(param) ?: value },
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture("partículas") },
                        onValue = { store.setParticleParam(param, it) },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(numeroPtBr(value, 1).let { text }, onTap = null)
        }
    }
}
