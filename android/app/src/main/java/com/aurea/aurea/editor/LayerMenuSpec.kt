package com.aurea.aurea.editor

// =============================================================================
// Funções do AUREA (13/11/2026, FUNCTIONS_IA.md §3.5, fase F2): o ⋮ da camada
// só com AÇÕES SOBRE A CAMADA, no máximo [LAYER_MENU_MAX] linhas (cabeçalhos não
// contam), como o ⋮ da barra da camada da referência. Conta pura, testada na JVM
// (LayerMenuSpecTest); gêmea de `layerMenuActions` no iOS (ToolbarView.swift).
//
// O que saiu daqui e onde mora agora (nada se perde):
//  · Velocidade → fileira de tempo do painel da camada;
//  · Opções de texto → Editar texto (grade);
//  · Fantoche, Rastreio (×3), Modos de corte + Congelar, Animadores, Rig, Operadores,
//    3D (camada 3D, virar objeto 3D, prender a osso, câmera do grupo, aceitar luzes) e
//    as chaves da camada (ajuste + "afeta", guia, preservar transparência, linha
//    magnética, tímida, qualidade e amostragem no PC) → navegador de Efeitos,
//    grupo "Funções do Aurea" (EffectTool);
//  · 3D e Desfoque de movimento → menu da camada na composição;
//  · Copiar/colar camada e estilo → Copiar e colar do transporte;
//  · Ocultar tímidas (composição) e Importar do AE → Ajustes do projeto.
// =============================================================================

/** Teto de linhas do ⋮ da camada (a referência tem ≈ 10; o AUREA chegou a 52). */
internal const val LAYER_MENU_MAX = 16

/** Uma linha do ⋮ da camada, na ordem da tela. */
internal enum class LayerMenuAction {
    // Camada
    Lock, Hide, Solo, KeyframeTracks, Duplicate, PullToPlayhead, BringForward, SendBackward,
    // Etiqueta (uma fileira de ícones)
    Labels,
    // Forma
    Flip, ClipMask, ConvertOutline, EnterGroup, Ungroup,
    /** "Grupo ›": converter em grupo, pôr num grupo que existe, tirar do grupo aberto. */
    GroupMenu,
    // Mídia
    ReplaceMedia, RemoveBackground, MediaInfo, ExtractAudio,
}

/** O que o ⋮ precisa saber da camada (tipos de `LayerType.kind`: 1 vídeo, 2 imagem, 3 áudio, 5 forma, 12 grupo). */
internal data class LayerMenuInput(
    val kind: Int,
    /** Máscara de recorte possível (ou já ligada). */
    val clipMask: Boolean = false,
    /** A forma viva pode virar contorno. */
    val canConvertOutline: Boolean = false,
)

/** As linhas do ⋮ para a camada, na ordem: Camada · Etiqueta · Forma · Mídia. */
internal fun layerMenuActions(input: LayerMenuInput): List<LayerMenuAction> = buildList {
    val kind = input.kind
    val visual = kind != 3
    val media = kind == 1 || kind == 2
    addAll(listOf(
        LayerMenuAction.Lock, LayerMenuAction.Hide, LayerMenuAction.Solo, LayerMenuAction.KeyframeTracks,
        LayerMenuAction.Duplicate, LayerMenuAction.PullToPlayhead, LayerMenuAction.BringForward, LayerMenuAction.SendBackward,
    ))
    add(LayerMenuAction.Labels)
    if (visual) {
        add(LayerMenuAction.Flip)
        if (input.clipMask) add(LayerMenuAction.ClipMask)
        if (kind == 5 && input.canConvertOutline) add(LayerMenuAction.ConvertOutline)
        if (kind == 12) {
            add(LayerMenuAction.EnterGroup)
            add(LayerMenuAction.Ungroup)
        }
        add(LayerMenuAction.GroupMenu)
    }
    if (media) {
        add(LayerMenuAction.ReplaceMedia)
        add(LayerMenuAction.RemoveBackground)
    }
    if (media || kind == 3) add(LayerMenuAction.MediaInfo)
    if (kind == 1) add(LayerMenuAction.ExtractAudio)
}
