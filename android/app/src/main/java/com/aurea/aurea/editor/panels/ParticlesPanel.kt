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
    val links = store.particleLinks
    val self = store.primary
    val rows = store.layers.filter { it.id != self }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        PresetRow(store)
        Spacer(Modifier.height(10.dp))

        // --- Emissor: de onde sai -------------------------------------------------
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
                stringResource(R.string.particular_emitter_layer),
                stringResource(R.string.particular_emitter_text),
                stringResource(R.string.particular_emitter_path),
                stringResource(R.string.particular_emitter_mesh),
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
            in 6..9 -> {
                // Quem pode emitir: cada tipo lê a fonte do seu jeito (ver
                // ParticleExtras.cpp). Sem fonte, sai da caixa do emissor.
                val candidates = rows.filter { r ->
                    when (emitter) {
                        6 -> r.kind == LayerType.Image.kind || r.kind == LayerType.Text.kind || r.kind == LayerType.Shape.kind
                        7 -> r.kind == LayerType.Text.kind
                        8 -> r.maskCount > 0 || r.kind == LayerType.Shape.kind || r.kind == LayerType.Text.kind
                        else -> r.kind == LayerType.Model3D.kind
                    }
                }
                LayerPick(stringResource(R.string.particular_source), links?.getOrNull(0) ?: 0L, candidates,
                    stringResource(R.string.particular_source_hint_none)) { store.setParticleSource(it) }
                Choice(store, stringResource(R.string.particular_emit_from), P.EmitFrom) {
                    listOf(
                        stringResource(R.string.particular_emit_vertices),
                        stringResource(R.string.particular_emit_surface),
                        stringResource(R.string.particular_emit_edges),
                    )
                }
            }
        }
        Dim(store, stringResource(R.string.particular_offset_x), P.EmitterOffsetX, p, 1f, -4000f, 4000f, " px")
        Dim(store, stringResource(R.string.particular_offset_y), P.EmitterOffsetY, p, 1f, -4000f, 4000f, " px")
        Choice(store, stringResource(R.string.particular_space), P.EmitterSpace) {
            listOf(stringResource(R.string.particular_space_local), stringResource(R.string.particular_space_world))
        }

        // --- Emissão --------------------------------------------------------------
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

        // --- Partícula: o que sai -------------------------------------------------
        val shape = p[P.ParticleType].roundToInt()
        Group(stringResource(R.string.particular_group_particle))
        Choice(store, stringResource(R.string.particular_shape), P.ParticleType) {
            listOf(
                stringResource(R.string.particular_shape_circle),
                stringResource(R.string.particular_shape_square),
                stringResource(R.string.particular_shape_streak),
                stringResource(R.string.particular_shape_soft),
                stringResource(R.string.particular_shape_texture),
                stringResource(R.string.particular_shape_mesh),
            )
        }
        if (shape == 4) {
            LayerPick(stringResource(R.string.particular_texture_image), links?.getOrNull(1) ?: 0L,
                rows.filter { it.kind == LayerType.Image.kind },
                stringResource(R.string.particular_texture_hint)) { store.setParticleTexture(it) }
        }
        if (shape == 5) {
            LayerPick(stringResource(R.string.particular_mesh_model), links?.getOrNull(2) ?: 0L,
                rows.filter { it.kind == LayerType.Model3D.kind },
                stringResource(R.string.particular_mesh_hint)) { store.setParticleMesh(it) }
            Dim(store, stringResource(R.string.particular_mesh_scale), P.MeshScale, p, 0.01f, 0.01f, 100f, "x", decimals = 2)
            Toggle(store, stringResource(R.string.particular_mesh_lit), P.MeshLit, p)
        }
        Dim(store, stringResource(R.string.panel_tamanho_inicial), P.StartSize, p, 0.5f, 0f, 2000f, " px")
        Dim(store, stringResource(R.string.panel_tamanho_final), P.EndSize, p, 0.5f, 0f, 2000f, " px")
        Pct(store, stringResource(R.string.panel_opacidade_inicial), P.StartOpacity, p)
        Pct(store, stringResource(R.string.panel_opacidade_final), P.EndOpacity, p)
        Angle(store, stringResource(R.string.panel_rotacao), P.Rotation, p)
        Angle(store, stringResource(R.string.particular_rotation_random), P.RotationRandom, p, 0f, 360f)
        Dim(store, stringResource(R.string.panel_giro), P.Spin, p, 2f, -3600f, 3600f, " °/s")
        Pct(store, stringResource(R.string.particular_size_random), P.SizeRandom, p)
        Pct(store, stringResource(R.string.particular_opacity_random), P.OpacityRandom, p)
        Pct(store, stringResource(R.string.particular_color_random), P.ColorRandom, p)

        // --- Ao longo da vida -----------------------------------------------------
        Group(stringResource(R.string.particular_group_life))
        LifeGradient(env)
        LifeCurve(store, 1, stringResource(R.string.particular_size_curve), 0f, 4f)
        LifeCurve(store, 2, stringResource(R.string.particular_opacity_curve), 0f, 1f)

        // --- Física: para onde vai ------------------------------------------------
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
        val collision = p[P.Collision].roundToInt()
        Choice(store, stringResource(R.string.particular_collision), P.Collision) {
            listOf(
                stringResource(R.string.particular_collision_none),
                stringResource(R.string.particular_collision_plane),
                stringResource(R.string.particular_collision_sphere),
                stringResource(R.string.particular_collision_box),
            )
        }
        when (collision) {
            1 -> Dim(store, stringResource(R.string.particular_collision_y), P.CollisionY, p, 1f, -4000f, 4000f, " px")
            2, 3 -> {
                // Centro a partir do centro do emissor (Y é o mesmo controle da altura do plano).
                Dim(store, stringResource(R.string.particular_collision_cx), P.CollisionX, p, 1f, -4000f, 4000f, " px")
                Dim(store, stringResource(R.string.particular_collision_cy), P.CollisionY, p, 1f, -4000f, 4000f, " px")
                Dim(store, stringResource(R.string.particular_collision_cz), P.CollisionZ, p, 1f, -4000f, 4000f, " px")
                if (collision == 2) {
                    Dim(store, stringResource(R.string.particular_radius), P.CollisionRadius, p, 0.5f, 0f, 8000f, " px")
                } else {
                    Dim(store, stringResource(R.string.particular_width), P.CollisionWidth, p, 1f, 0f, 16000f, " px")
                    Dim(store, stringResource(R.string.particular_height), P.CollisionHeight, p, 1f, 0f, 16000f, " px")
                    Dim(store, stringResource(R.string.particular_depth), P.CollisionDepth, p, 1f, 0f, 16000f, " px")
                }
            }
        }
        if (collision > 0) Pct(store, stringResource(R.string.particular_bounce), P.CollisionBounce, p)

        // --- Aux ------------------------------------------------------------------
        Group(stringResource(R.string.particular_group_aux))
        Dim(store, stringResource(R.string.particular_aux_count), P.AuxCount, p, 0.1f, 0f, 16f, "")
        if (p[P.AuxCount].roundToInt() > 0) {
            Pct(store, stringResource(R.string.particular_aux_probability), P.AuxProbability, p)
            Pct(store, stringResource(R.string.particular_aux_at), P.AuxAt, p)
            Dim(store, stringResource(R.string.panel_duracao_cada), P.AuxLife, p, 0.02f, 0.05f, 20f, " s")
            Dim(store, stringResource(R.string.panel_velocidade), P.AuxSpeed, p, 2f, 0f, 5000f, " px/s")
            Dim(store, stringResource(R.string.panel_tamanho), P.AuxSize, p, 0.3f, 0f, 500f, " px")
            Angle(store, stringResource(R.string.panel_abertura), P.AuxSpread, p)
        }

        // --- Rastro ---------------------------------------------------------------
        Group(stringResource(R.string.particular_group_trail))
        Dim(store, stringResource(R.string.particular_trail_len), P.TrailLength, p, 0.002f, 0f, 2f, " s", decimals = 2)
        if (p[P.TrailLength] > 0f) {
            Pct(store, stringResource(R.string.particular_trail_taper), P.TrailTaper, p)
            Dim(store, stringResource(R.string.particular_trail_width), P.TrailWidth, p, 0.01f, 0f, 8f, "x", decimals = 2)
            Pct(store, stringResource(R.string.particular_trail_opacity), P.TrailOpacity, p)
        }

        // --- Renderização ---------------------------------------------------------
        Group(stringResource(R.string.particular_group_render))
        Choice(store, stringResource(R.string.particular_blend), P.BlendMode) {
            listOf(stringResource(R.string.particular_blend_normal), stringResource(R.string.particular_blend_add))
        }
        Pct(store, stringResource(R.string.particular_softness), P.Softness, p)

        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(9.dp)).background(AureaColors.Chip)
            .tocavel { setAdvanced(!advanced) }.padding(horizontal = 12.dp, vertical = 9.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text((if (advanced) "▾ " else "▸ ") + stringResource(R.string.particular_advanced),
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
    // 8.2 (v21) — só no fim: o número é contrato.
    const val EmitterSpace = 54; const val EmitFrom = 55; const val AuxProbability = 56
    const val TrailWidth = 57; const val TrailOpacity = 58
    const val SizeRandom = 59; const val OpacityRandom = 60; const val ColorRandom = 61
    const val CollisionX = 62; const val CollisionZ = 63; const val CollisionRadius = 64
    const val CollisionWidth = 65; const val CollisionHeight = 66; const val CollisionDepth = 67
    const val MeshScale = 68; const val MeshLit = 69
}

/** Escolha de uma camada do projeto (fonte, imagem, modelo), com "Nenhuma". */
@Composable
private fun LayerPick(label: String, current: Long, candidates: List<LayerRow>, emptyHint: String, onPick: (Long) -> Unit) {
    Spacer(Modifier.height(6.dp))
    Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    if (candidates.isEmpty()) {
        KitHint(emptyHint)
    } else {
        ChipRow {
            KitChip(stringResource(R.string.particular_none), current == 0L) { onPick(0L) }
            candidates.forEach { r ->
                KitChip(r.name.ifEmpty { LayerType.of(r.kind).label }, current == r.id) { onPick(r.id) }
            }
        }
    }
}

/**
 * Cor ao longo da vida: até 8 paradas (posição + cor sRGB). Sem paradas vale o
 * início → fim da partícula. Cada parada tem a amostra (abre o seletor) e a
 * régua da posição; um arrasto inteiro é um passo de desfazer.
 */
@Composable
private fun LifeGradient(env: PanelEnv) {
    val store = env.store
    val flat = store.particleCurves.getOrNull(0).orEmpty()
    val stops = flat.chunked(4).filter { it.size == 4 }
    Spacer(Modifier.height(6.dp))
    Text(stringResource(R.string.particular_color_gradient), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    if (stops.isEmpty()) {
        ChipRow {
            KitHint(stringResource(R.string.particular_curve_off))
            KitChip(stringResource(R.string.particular_curve_use), false) {
                store.setParticleLifeCurve(0, listOf(0f, 1f, 1f, 1f, 1f, 1f, 0.45f, 0.1f))
            }
        }
        return
    }
    fun send(list: List<List<Float>>) = store.setParticleLifeCurve(0, list.flatten())
    stops.forEachIndexed { i, s ->
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            ColorWell(Color(s[1].coerceIn(0f, 1f), s[2].coerceIn(0f, 1f), s[3].coerceIn(0f, 1f))) {
                store.beginGesture("cor ao longo da vida")
                env.openColor(ColorRequest(floatArrayOf(s[1], s[2], s[3], 1f),
                    onChange = { r, g, b, _ ->
                        val now = store.particleCurves.getOrNull(0).orEmpty().chunked(4).map { it.toMutableList() }
                        if (i < now.size) { now[i][1] = r; now[i][2] = g; now[i][3] = b; send(now) }
                    },
                    onDone = { store.endGesture() }))
            }
            Spacer(Modifier.width(8.dp))
            Box(Modifier.weight(1f)) {
                PointRuler(store, stringResource(R.string.particular_curve_at), s[0], 0f, 1f, 0.005f, pct = true) { v ->
                    val now = store.particleCurves.getOrNull(0).orEmpty().chunked(4).map { it.toMutableList() }
                    if (i < now.size) { now[i][0] = v; send(now) }
                }
            }
            RemoveChip { send(stops.filterIndexed { j, _ -> j != i }) }
        }
    }
    CurveButtons(stops.size) { add ->
        if (add) {
            val last = stops.last()
            val prev = stops.getOrNull(stops.size - 2) ?: last
            send(stops + listOf(listOf((prev[0] + last[0]) * 0.5f, last[1], last[2], last[3])))
        } else {
            store.setParticleLifeCurve(0, emptyList())
        }
    }
}

/**
 * Tamanho ou opacidade ao longo da vida: pontos (posição, multiplicador) numa
 * curva suave (Hermite, presa entre os vizinhos — ver particles_extras.glsl).
 */
@Composable
private fun LifeCurve(store: EditorStore, kind: Int, title: String, min: Float, max: Float) {
    val pts = store.particleCurves.getOrNull(kind).orEmpty().chunked(2).filter { it.size == 2 }
    Spacer(Modifier.height(6.dp))
    Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    if (pts.isEmpty()) {
        ChipRow {
            KitHint(stringResource(R.string.particular_curve_off))
            KitChip(stringResource(R.string.particular_curve_use), false) {
                // Sobe e desce (tamanho) / aparece e some (opacidade).
                store.setParticleLifeCurve(kind, listOf(0f, if (kind == 1) 0.2f else 0f, 0.3f, 1f, 1f, 0f))
            }
        }
        return
    }
    fun send(list: List<List<Float>>) = store.setParticleLifeCurve(kind, list.flatten())
    pts.forEachIndexed { i, s ->
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f)) {
                PointRuler(store, stringResource(R.string.particular_curve_at), s[0], 0f, 1f, 0.005f, pct = true) { v ->
                    val now = store.particleCurves.getOrNull(kind).orEmpty().chunked(2).map { it.toMutableList() }
                    if (i < now.size) { now[i][0] = v; send(now) }
                }
            }
            Spacer(Modifier.width(6.dp))
            Box(Modifier.weight(1f)) {
                PointRuler(store, stringResource(R.string.particular_curve_value), s[1], min, max, 0.005f, pct = kind == 2) { v ->
                    val now = store.particleCurves.getOrNull(kind).orEmpty().chunked(2).map { it.toMutableList() }
                    if (i < now.size) { now[i][1] = v; send(now) }
                }
            }
            RemoveChip { send(pts.filterIndexed { j, _ -> j != i }) }
        }
    }
    CurveButtons(pts.size) { add ->
        if (add) {
            val last = pts.last()
            val prev = pts.getOrNull(pts.size - 2) ?: last
            send(pts + listOf(listOf((prev[0] + last[0]) * 0.5f, last[1])))
        } else {
            store.setParticleLifeCurve(kind, emptyList())
        }
    }
}

/** "+ Ponto" (até 8) e "voltar ao início → fim". */
@Composable
private fun CurveButtons(count: Int, onAction: (add: Boolean) -> Unit) {
    ChipRow {
        if (count < 8) KitChip(stringResource(R.string.particular_curve_add), false) { onAction(true) }
        KitChip(stringResource(R.string.particular_curve_clear), false) { onAction(false) }
    }
}

@Composable
private fun RemoveChip(onClick: () -> Unit) {
    val desc = stringResource(R.string.particular_remove)
    Box(Modifier.padding(start = 6.dp).height(36.dp).clip(RoundedCornerShape(9.dp)).background(AureaColors.Chip)
        .tocavel(shrink = 1f, onClick = onClick).padding(horizontal = 10.dp), contentAlignment = Alignment.Center) {
        Text("×", style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)),
            modifier = Modifier.semantics { contentDescription = desc })
    }
}

/** Régua compacta de um ponto de curva (sem losango: a curva não é keyframe). */
@Composable
private fun PointRuler(store: EditorStore, label: String, value: Float, min: Float, max: Float, unitsPerDp: Float,
                       pct: Boolean, onValue: (Float) -> Unit) {
    val send by rememberUpdatedState(onValue)
    val cur by rememberUpdatedState(value)
    Column {
        Text("$label " + if (pct) "${(value * 100f).roundToInt()}%" else numeroPtBr(value, 2),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
        Box(Modifier.fillMaxWidth().height(32.dp)) {
            TickRuler(
                value = { cur },
                unitsPerDp = unitsPerDp,
                active = true,
                modifier = Modifier.fillMaxSize().valueDrag(
                    enabled = true,
                    start = { cur },
                    unitsPerDp = { unitsPerDp },
                    min = min,
                    max = max,
                    onStart = { store.beginGesture("curva das partículas") },
                    onValue = { send(it) },
                    onEnd = { store.endGesture() },
                ),
            )
        }
    }
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
                unitsPerDp: Float, min: Float, max: Float, unit: String, negate: Boolean = false, decimals: Int = 0) {
    val raw = p.getOrNull(param) ?: 0f
    val shown = if (negate) -raw else raw
    val text = if (decimals > 0) numeroPtBr(shown, decimals) + unit else "${shown.roundToInt()}$unit"
    Ruler(store, label, param, raw, text, unitsPerDp, min, max)
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
