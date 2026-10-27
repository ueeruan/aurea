package com.aurea.aurea.state

import com.aurea.aurea.R

/**
 * Regras puras do painel 3D, fora do EditorStore para os testes de JVM.
 * As mesmas tabelas existem no iOS (AureaModel.hdriFailure e Panel3DView).
 */

/**
 * O que `text3d` passa a valer num refresh. A receita do arrasto/digitação
 * que ainda não foi ao motor (`pending`, da camada `pendingLayer`) vence a do
 * motor para a MESMA camada: sem isso o refresh do fim do gesto trazia o valor
 * antigo de volta e o envio atrasado o gravava (profundidade/chanfro "não mudavam").
 */
internal fun <T> text3dAfterRefresh(pending: Boolean, pendingLayer: Long, current: Long?, local: T?, fromEngine: () -> T?): T? =
    if (pending && local != null && current != null && pendingLayer == current) local else fromEngine()

/**
 * Bug 0.3.6 ("as configurações do texto 3D não mudam neste projeto"): o motor
 * recusava a edição (camada bloqueada, memória do projeto cheia, fonte, letras)
 * e a tela só voltava ao valor antigo, sem dizer nada. Errc da última edição
 * (Engine::last_text3d_status) → frase. Nulo = aplicada ou cancelada (sem aviso).
 * O mesmo mapa existe no iOS (Panel3DView.text3DFailureKey).
 */
internal fun text3dErrorMessage(code: Int): Int? = when (code) {
    0, 25 -> null                                                   // Ok, Cancelled
    5 -> R.string.editor_camada_bloqueada_desbloqueie_editar        // InvalidState: camada bloqueada
    8, 9 -> R.string.t3d036_err_memory                              // OutOfMemory, BudgetExceeded
    6 -> R.string.t3d036_err_font                                   // NotSupported: nenhuma fonte
    2 -> R.string.t3d036_err_geometry                               // OutOfRange: malha não sai
    17 -> R.string.msg_nao_foi_possivel_importar_a_imagem           // UnsupportedFormat: textura
    else -> R.string.t3d036_err_generic
}

/** Extensões que o seletor de ambiente aceita (o motor reconhece pelo conteúdo). */
internal val HDRI_FILE_EXTENSIONS = setOf("hdr", "hdri", "pic", "exr", "jpg", "jpeg", "png", "zip")

/** Nome de arquivo → extensão aceita para HDRI, ou nulo. */
internal fun hdriExtensionOf(displayName: String?): String? =
    (displayName ?: "ambiente.hdr").substringAfterLast(".", "").lowercase().takeIf { it in HDRI_FILE_EXTENSIONS }

/**
 * Código do motor (Errc, positivo) do import de HDRI → frase. −1000 = a cópia
 * para o app falhou. Nulo = sem frase específica (mostra o código).
 */
internal fun hdriErrorMessage(code: Long): Int? = when (code) {
    -1_000L, 3L, 10L -> R.string.msg_hdri_err_unreadable      // NotFound, IoError
    6L, 16L, 17L -> R.string.msg_hdri_err_format              // NotSupported, UnsupportedCodec, UnsupportedFormat
    11L, 14L -> R.string.msg_hdri_err_corrupt                 // CorruptData, DecodeFailed
    8L, 9L -> R.string.msg_hdri_err_too_large                 // OutOfMemory, BudgetExceeded
    else -> null
}
