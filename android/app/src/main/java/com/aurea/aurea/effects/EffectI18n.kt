package com.aurea.aurea.effects

import android.content.res.Resources
import com.aurea.aurea.R
import com.aurea.aurea.engine.EffectParam

// =============================================================================
//  O TEXTO QUE O MOTOR PUBLICA NUM EFEITO, NO IDIOMA DO APP.
//
//  O motor fala pt-BR (rótulo do parâmetro, opções de lista, unidade) e continua
//  assim. A interface traduz pela IDENTIDADE — `typeId` + índice do parâmetro
//  (+ índice da opção) — com a tabela gerada `EffectI18nTable` (chave + id do
//  parâmetro no catálogo do motor → recurso). Sem entrada na tabela, fica o
//  texto do motor: um efeito novo aparece, só não traduzido.
// =============================================================================

/** Unidades que o motor escreve em português; "%", "px", "Hz", "°"... ficam como estão. */
private val UnitRes = mapOf(
    "quadros" to R.string.fxu_frames,
    "px/quadro" to R.string.fxu_px_per_frame,
    "oitavas" to R.string.fxu_octaves,
    "voltas" to R.string.fxu_turns,
)

/** Os parâmetros de um efeito do tipo [typeId] com rótulo, opções e unidade traduzidos. */
internal fun localizeEffectParams(res: Resources, typeId: Int, params: List<EffectParam>): List<EffectParam> =
    params.map { p ->
        val ids = EffectI18nTable.entries[EffectI18nTable.slot(typeId, p.index)]
        val unit = UnitRes[p.unit]?.let(res::getString) ?: p.unit
        if (ids == null && unit == p.unit) return@map p
        val label = ids?.let { res.getString(it[0]) } ?: p.label
        // Opções só pela identidade completa: se o motor mudou a lista, fica a dele.
        val options = if (ids != null && ids.size - 1 == p.enumLabels.size) {
            List(p.enumLabels.size) { res.getString(ids[it + 1]) }
        } else {
            p.enumLabels
        }
        EffectParam(
            index = p.index, type = p.type, flags = p.flags, min = p.min, max = p.max,
            value = p.value, defaultValue = p.defaultValue, label = label, unit = unit,
            enumLabels = options, animated = p.animated, hardMin = p.hardMin, hardMax = p.hardMax,
        )
    }
