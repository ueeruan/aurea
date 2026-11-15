package com.aurea.aurea.editor

import com.aurea.aurea.editor.panels.AnimationTab as AT
import com.aurea.aurea.editor.panels.animationTabs
import com.aurea.aurea.effects.EffectGroup
import com.aurea.aurea.effects.EffectTool
import com.aurea.aurea.effects.effectGroupOf
import com.aurea.aurea.effects.effectToolOf
import com.aurea.aurea.effects.groupEntries
import com.aurea.aurea.effects.pickableTools
import com.aurea.aurea.effects.toolCatalogEntry
import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Funções do AUREA (13/11/2026, FUNCTIONS_IA.md fases F0–F2 com as decisões do
 * dono): o ⋮ da camada só com ações (≤ 16 linhas), as funções extras no
 * navegador de Efeitos (grupo "Funções do Aurea") e nada perdido no caminho.
 * Gêmeos no iOS: `layerMenuActions` (ToolbarView.swift) e `fxPickableTools`
 * (EffectsBrowser.swift).
 */
class LayerFunctionsTest {
    private val kinds = LayerType.entries.map { it.kind }

    @Test fun layerMenuNeverPassesSixteenLinesForAnyLayer() {
        for (kind in kinds) for (clip in listOf(false, true)) for (outline in listOf(false, true)) {
            val actions = layerMenuActions(LayerMenuInput(kind, clipMask = clip, canConvertOutline = outline))
            assertTrue("tipo $kind: ${actions.size} linhas", actions.size <= LAYER_MENU_MAX)
            assertEquals("sem linha repetida", actions.size, actions.toSet().size)
        }
    }

    @Test fun layerMenuKeepsTheReferenceLayerActions() {
        val video = layerMenuActions(LayerMenuInput(LayerType.Video.kind, clipMask = true))
        // Ações sobre a camada da referência (travar, ocultar, ordem, etiqueta, espelhar,
        // máscara de recorte, grupo) e as de mídia.
        listOf(
            LayerMenuAction.Lock, LayerMenuAction.Hide, LayerMenuAction.Solo, LayerMenuAction.KeyframeTracks,
            LayerMenuAction.Duplicate, LayerMenuAction.PullToPlayhead, LayerMenuAction.BringForward,
            LayerMenuAction.SendBackward, LayerMenuAction.Labels, LayerMenuAction.Flip, LayerMenuAction.ClipMask,
            LayerMenuAction.GroupMenu, LayerMenuAction.ReplaceMedia, LayerMenuAction.RemoveBackground,
            LayerMenuAction.MediaInfo, LayerMenuAction.ExtractAudio,
        ).forEach { assertTrue("vídeo sem $it", it in video) }
        assertEquals(LAYER_MENU_MAX, video.size)   // o pior caso cabe exato
        // Grupo: entrar e desagrupar; forma viva: converter em contorno.
        val group = layerMenuActions(LayerMenuInput(LayerType.Group.kind))
        assertTrue(LayerMenuAction.EnterGroup in group && LayerMenuAction.Ungroup in group)
        assertTrue(LayerMenuAction.ConvertOutline in layerMenuActions(LayerMenuInput(LayerType.Shape.kind, canConvertOutline = true)))
        assertFalse(LayerMenuAction.ConvertOutline in layerMenuActions(LayerMenuInput(LayerType.Text.kind, canConvertOutline = true)))
        // Áudio: nada de espelhar nem grupo de forma; só as ações da camada e a informação da mídia.
        val audio = layerMenuActions(LayerMenuInput(LayerType.Audio.kind))
        assertFalse(LayerMenuAction.Flip in audio)
        assertTrue(LayerMenuAction.MediaInfo in audio)
        assertFalse(LayerMenuAction.ReplaceMedia in audio)
    }

    @Test fun everyFunctionThatLeftTheMenuHasATool() {
        // O que saiu do ⋮ e virou ferramenta do navegador de Efeitos, por tipo.
        assertTrue(EffectTool.Puppet in pickableTools(LayerType.Image.kind))
        listOf(EffectTool.Tracking, EffectTool.CameraTrack, EffectTool.PreciseCut).forEach {
            assertTrue("vídeo sem $it", it in pickableTools(LayerType.Video.kind))
        }
        assertTrue(EffectTool.PreciseCut in pickableTools(LayerType.Audio.kind))
        // F4: Layout do texto (espaçamento, caixa, caminho, dinâmico) só no texto.
        assertTrue(EffectTool.TextLayout in pickableTools(LayerType.Text.kind))
        assertFalse(EffectTool.TextLayout in pickableTools(LayerType.Shape.kind))
        assertTrue(EffectTool.ShapeOperators in pickableTools(LayerType.Shape.kind, vector = true))
        assertFalse(EffectTool.ShapeOperators in pickableTools(LayerType.Shape.kind, vector = false))
        assertTrue(EffectTool.RigMocap in pickableTools(LayerType.Model3D.kind, importedModel = true))
        assertFalse(EffectTool.RigMocap in pickableTools(LayerType.Model3D.kind, importedModel = false))
        // 3D da camada e Animação: toda camada visual; Opções da camada: toda camada.
        for (kind in kinds) {
            assertTrue(EffectTool.LayerOptions in pickableTools(kind))
            assertEquals(kind != LayerType.Audio.kind, EffectTool.Layer3D in pickableTools(kind))
            assertEquals(kind != LayerType.Audio.kind, EffectTool.Animation in pickableTools(kind))
        }
        // Nada de rastreio, fantoche nem corte preciso onde não há o que usar.
        assertFalse(EffectTool.Tracking in pickableTools(LayerType.Text.kind))
        assertFalse(EffectTool.Puppet in pickableTools(LayerType.Video.kind))
        assertFalse(EffectTool.PreciseCut in pickableTools(LayerType.Image.kind))
    }

    @Test fun aureaFunctionsHaveTheirOwnGroupFirst() {
        val functions = EffectTool.entries.filter { it.category == "Ferramentas" }
        assertEquals(9, functions.size)
        functions.forEach { tool ->
            val entry = toolCatalogEntry(tool, tool.name)
            assertEquals(EffectGroup.AureaTools, effectGroupOf(entry))
            assertEquals(tool, effectToolOf(entry.typeId))
        }
        // As ferramentas antigas continuam nos grupos de sempre.
        assertEquals(EffectGroup.Text, effectGroupOf(toolCatalogEntry(EffectTool.Captions, "Legendas")))
        assertEquals(EffectGroup.Matte, effectGroupOf(toolCatalogEntry(EffectTool.Mask, "Máscara")))
        // "Funções do Aurea" é o primeiro ladrilho.
        val groups = groupEntries(listOf(
            toolCatalogEntry(EffectTool.Captions, "Legendas"),
            toolCatalogEntry(EffectTool.LayerOptions, "Opções da camada"),
        )).map { it.first }
        assertEquals(listOf(EffectGroup.AureaTools, EffectGroup.Text), groups)
        // Chaves estáveis e sem colisão de typeId (a chave das grades é o typeId).
        assertEquals(EffectTool.entries.size, EffectTool.entries.map { it.typeId }.toSet().size)
    }

    @Test fun animationPanelTabsFollowTheLayer() {
        // F3: Camada sempre; Texto e Prontas no texto e no texto 3D; Explodir só no texto 2D. ≤ 4 abas.
        assertEquals(listOf(AT.Layer), animationTabs(LayerType.Shape.kind, text3D = false))
        assertEquals(listOf(AT.Layer, AT.Text, AT.Ready, AT.Explode), animationTabs(LayerType.Text.kind, text3D = false))
        assertEquals(listOf(AT.Layer, AT.Text, AT.Ready), animationTabs(LayerType.Model3D.kind, text3D = true))
        for (kind in kinds) assertTrue(animationTabs(kind, text3D = false).size <= 4)
    }
}
