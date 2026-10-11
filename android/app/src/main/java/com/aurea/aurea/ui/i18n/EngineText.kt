package com.aurea.aurea.ui.i18n

import android.content.Context
import androidx.annotation.StringRes
import com.aurea.aurea.R
import com.aurea.aurea.state.humanErrorResource
import java.text.Normalizer

/**
 * Texto que vem do MOTOR (C++/JNI) no idioma do app.
 *
 * O motor descreve falhas com frases curtas em português (às vezes em inglês):
 * "buffer do encoder menor que o quadro", "Not enough tracked frames". Mostrar
 * isso cru fazia o português vazar na interface em inglês, espanhol, russo...
 * Aqui cada frase CONHECIDA vira uma chave do catálogo (mesma tabela do iOS em
 * `AureaEngineText.swift` — as duas saem de uma lista só e precisam bater).
 *
 * Frase desconhecida: em português aparece como veio (o motor já fala pt-BR);
 * em qualquer outro idioma cai na mensagem localizada do código de erro
 * (`humanErrorResource`), ou numa frase genérica — nunca em português.
 */
object EngineText {
    /** Texto normalizado (minúsculo, sem acento, sem ponto final) → recurso. `*` no fim = prefixo. */
    private val KNOWN: List<Pair<String, Int>> = listOf(
        "buffer do encoder menor que o quadro" to R.string.eng_encoder_buffer_small,
        "quadro com passo de linha menor que a largura" to R.string.eng_encoder_buffer_small,
        "encoder nao aceita essa resolucao/formato" to R.string.eng_encoder_format,
        "este aparelho nao codifica aac" to R.string.eng_audio_encoder,
        "encoder aac recusou a configuracao" to R.string.eng_audio_encoder,
        "encoder aac nao iniciou" to R.string.eng_audio_encoder,
        "encoder de audio parou" to R.string.eng_audio_encoder,
        "o encoder recusou o audio" to R.string.eng_audio_encoder,
        "buffer de audio invalido" to R.string.eng_audio_encoder,
        "encoder de video nao iniciou" to R.string.eng_video_encoder_start,
        "encoder de video parou de aceitar quadros" to R.string.eng_video_encoder_stalled,
        "encoder nao terminou" to R.string.eng_video_encoder_stalled,
        "encoder nao aceitou o fim do fluxo" to R.string.eng_video_encoder_stalled,
        "encoder recusou o fim do fluxo" to R.string.eng_video_encoder_stalled,
        "encoder nao entregou buffer de entrada" to R.string.eng_video_encoder_stalled,
        "encoder entregou pacote fora do buffer" to R.string.eng_video_encoder_stalled,
        "o encoder recusou o quadro" to R.string.eng_video_encoder_stalled,
        "o encoder recusou o arquivo" to R.string.eng_video_encoder_stalled,
        "muxer mp4 indisponivel" to R.string.eng_file_not_finalized,
        "o arquivo nao foi finalizado" to R.string.eng_file_not_finalized,
        "nenhum quadro chegou ao arquivo" to R.string.eng_file_not_finalized,
        "encoder indisponivel" to R.string.eng_no_encoder,
        "sem encoder de video nesta plataforma" to R.string.eng_no_encoder,
        "fabrica de midia (videotoolbox) indisponivel" to R.string.eng_no_encoder,
        "resolucao acima do que este aparelho exporta" to R.string.eng_res_device_limit,
        "resolucao acima do limite de textura da gpu" to R.string.eng_res_device_limit,
        "sem memoria de gpu para o export" to R.string.eng_export_no_memory,
        "sem memoria para ler os quadros do export" to R.string.eng_export_no_memory,
        "memoria insuficiente para esta resolucao" to R.string.eng_export_no_memory,
        "memoria insuficiente para upscale nesta resolucao" to R.string.eng_export_no_memory,
        "sem memoria para o quadro" to R.string.eng_export_no_memory,
        "todos os quadros rgba em uso" to R.string.eng_export_no_memory,
        "export precisa de gpu" to R.string.eng_no_gpu,
        "sem gpu" to R.string.eng_no_gpu,
        "sem backend grafico" to R.string.eng_no_gpu,
        "renderer sem backend" to R.string.eng_no_gpu,
        "ja existe um export em andamento" to R.string.eng_export_busy,
        "export ja aberto" to R.string.eng_export_busy,
        "gif longo demais: use um trecho menor ou menos quadros por segundo" to R.string.eng_too_long,
        "sequencia longa demais: use um trecho menor ou menos quadros por segundo" to R.string.eng_too_long,
        "sequencia maior que 4 gb" to R.string.eng_over_4gb,
        "pacote maior que 4 gb" to R.string.eng_over_4gb,
        "imagem grande demais para png" to R.string.eng_png_too_big,
        "nao foi possivel criar o gif" to R.string.eng_output_create,
        "nao foi possivel criar o .zip" to R.string.eng_output_create,
        "nao foi possivel criar o png" to R.string.eng_output_create,
        "nao foi possivel abrir a saida" to R.string.eng_output_create,
        "sem caminho de saida" to R.string.eng_output_create,
        "gif fechado" to R.string.eng_output_create,
        "zip fechado" to R.string.eng_output_create,
        "falha ao gravar o gif (armazenamento cheio?)" to R.string.msg_sem_espaco_no_aparelho_libere_espaco,
        "falha ao gravar o .zip (armazenamento cheio?)" to R.string.msg_sem_espaco_no_aparelho_libere_espaco,
        "falha ao gravar o png (armazenamento cheio?)" to R.string.msg_sem_espaco_no_aparelho_libere_espaco,
        "armazenamento cheio" to R.string.msg_sem_espaco_no_aparelho_libere_espaco,
        "armazenamento cheio durante a exportacao" to R.string.msg_sem_espaco_no_aparelho_libere_espaco,
        "sem espaco" to R.string.msg_sem_espaco_no_aparelho_libere_espaco,
        "tamanho de gif invalido" to R.string.eng_invalid_size,
        "imagem vazia" to R.string.eng_invalid_size,
        "quadro vazio" to R.string.eng_invalid_size,
        "dimensoes invalidas no upscale" to R.string.eng_invalid_size,
        "previa sem tamanho" to R.string.eng_invalid_size,
        "nenhum decoder seguro aceitou o video" to R.string.eng_decoder_failed,
        "decoder parado sem entregar frame" to R.string.eng_decoder_failed,
        "frame renderizado nao chegou ao imagereader" to R.string.eng_decoder_failed,
        "decoder sem progresso" to R.string.eng_decoder_failed,
        "sem decodificador" to R.string.eng_decoder_failed,
        "sem decodificador de video nesta plataforma" to R.string.eng_decoder_failed,
        "video ilegivel" to R.string.eng_decoder_failed,
        "decoder unavailable" to R.string.eng_decoder_failed,
        "cannot decode video" to R.string.eng_decoder_failed,
        "nao foi possivel abrir o decode" to R.string.eng_decoder_failed,
        "nenhum projeto aberto" to R.string.eng_no_project,
        "projeto fechado" to R.string.eng_no_project,
        "projeto fechado durante o import" to R.string.eng_no_project,
        "projeto sem composicao" to R.string.eng_no_project,
        "nenhuma composicao" to R.string.eng_no_project,
        "motor indisponivel" to R.string.eng_engine_unavailable,
        "motor nao esta pronto" to R.string.eng_engine_unavailable,
        "sem memoria para a ponte" to R.string.eng_engine_unavailable,
        "cancelado" to R.string.msg_cancelado,
        "cancelled" to R.string.msg_cancelado,
        "transcricao cancelada" to R.string.msg_cancelado,
        "arquivo nao encontrado" to R.string.msg_arquivo_nao_encontrado,
        "arquivo do projeto nao encontrado" to R.string.msg_arquivo_nao_encontrado,
        "sem arquivo" to R.string.msg_arquivo_nao_encontrado,
        "container nao reconhecido" to R.string.msg_formato_de_arquivo_nao_suportado,
        "conteiner ilegivel" to R.string.msg_formato_de_arquivo_nao_suportado,
        "formato nao suportado" to R.string.msg_formato_de_arquivo_nao_suportado,
        "arquivo 3d invalido" to R.string.eng_model_invalid,
        "buffer do modelo ausente" to R.string.eng_model_invalid,
        "objeto 3d nao encontrado" to R.string.eng_model_invalid,
        "modelo sem geometria" to R.string.eng_model_no_geometry,
        "modelo comprimido com draco: exporte sem compressao" to R.string.eng_model_compressed,
        "modelo comprimido com meshopt (khr): exporte sem compressao" to R.string.eng_model_compressed,
        "forma 3d sem geometria" to R.string.eng_model_no_geometry,
        "arquivo sem trilha de video decodificavel" to R.string.eng_no_video_track,
        "sem trilha de video" to R.string.eng_no_video_track,
        "camada sem video" to R.string.eng_no_video_track,
        "arquivo sem trilha de audio decodificavel" to R.string.eng_no_audio_track,
        "sem trilha de audio" to R.string.eng_no_audio_track,
        "midia sem audio compativel" to R.string.eng_no_audio_track,
        "sem decoder para este audio" to R.string.eng_no_audio_track,
        "sem decodificador de audio nesta plataforma" to R.string.eng_no_audio_track,
        "este video nao tem som" to R.string.eng_no_sound,
        "camada sem audio" to R.string.eng_no_sound,
        "camada sem som" to R.string.eng_no_sound,
        "intervalo sem audio" to R.string.eng_no_sound,
        "imagem ilegivel" to R.string.eng_unreadable_media,
        "midia ilegivel" to R.string.eng_unreadable_media,
        "arquivo ilegivel" to R.string.eng_unreadable_media,
        "audio ilegivel" to R.string.eng_unreadable_media,
        "origem ilegivel" to R.string.eng_unreadable_media,
        "nao foi possivel ler o arquivo" to R.string.eng_unreadable_media,
        "leitura do arquivo falhou" to R.string.eng_unreadable_media,
        "nao e uma fonte ttf/otf legivel" to R.string.eng_bad_font,
        "ambiente nao lido*" to R.string.eng_bad_hdri,
        "lut vazio ou maior que 32 mb" to R.string.eng_bad_lut,
        "nao foi possivel ler o lut (limite 32 mb)" to R.string.eng_bad_lut,
        "lut incompleto" to R.string.eng_bad_lut,
        "dados do lut invalidos" to R.string.eng_bad_lut,
        "colunas inesperadas no lut" to R.string.eng_bad_lut,
        "dominio do lut invalido" to R.string.eng_bad_lut,
        "dominio do lut sem intervalo" to R.string.eng_bad_lut,
        "linha do lut longa demais" to R.string.eng_bad_lut,
        "lut combinado ou tamanho invalido" to R.string.eng_bad_lut,
        "lut 3d suporta*" to R.string.eng_bad_lut,
        "arquivo lut indisponivel" to R.string.eng_bad_lut,
        "nao e um projeto do aurea" to R.string.project_file_err_not_project,
        "nao e um arquivo .aurea" to R.string.project_file_err_not_project,
        "nao e um pacote" to R.string.project_file_err_not_project,
        "feito por uma versao mais nova" to R.string.msg_este_projeto_foi_salvo_por_uma,
        "pacote danificado" to R.string.msg_o_projeto_esta_danificado_e_nao,
        "pacote cortado" to R.string.msg_o_projeto_esta_danificado_e_nao,
        "projeto ilegivel" to R.string.msg_o_projeto_esta_danificado_e_nao,
        "secao corrompida" to R.string.msg_o_projeto_esta_danificado_e_nao,
        "nenhuma copia valida do projeto" to R.string.msg_o_projeto_esta_danificado_e_nao,
        "camada bloqueada" to R.string.eng_layer_locked,
        "camada nao encontrada" to R.string.eng_layer_missing,
        "camada sumiu" to R.string.eng_layer_missing,
        "camada nao existe" to R.string.eng_layer_missing,
        "camadas nao encontradas" to R.string.eng_layer_missing,
        "nenhuma camada" to R.string.eng_layer_missing,
        "nada para agrupar" to R.string.eng_nothing_to_group,
        "nada para pre-compor" to R.string.eng_nothing_to_group,
        "nao e um grupo" to R.string.eng_not_group,
        "nao e uma pre-composicao" to R.string.eng_not_group,
        "grupo nao encontrado" to R.string.eng_not_group,
        "nao esta dentro de um grupo" to R.string.eng_not_in_group,
        "a camada tem pai fora do grupo" to R.string.eng_group_links,
        "a camada tem pai dentro do grupo" to R.string.eng_group_links,
        "outra camada tem esta como pai" to R.string.eng_group_links,
        "outra camada do grupo depende desta" to R.string.eng_group_links,
        "a camada usa uma matte do grupo" to R.string.eng_group_links,
        "o grupo tem outra taxa de quadros" to R.string.eng_group_timing,
        "o tempo do grupo foi alterado" to R.string.eng_group_timing,
        "um grupo nao entra nele mesmo" to R.string.eng_group_self,
        "nao deu para agrupar" to R.string.eng_group_failed,
        "nao deu para tirar do grupo" to R.string.eng_group_failed,
        "nao deu para desagrupar" to R.string.eng_group_failed,
        "nenhuma camada desbloqueada" to R.string.eng_no_unlocked_layer,
        "escolha duas camadas desbloqueadas" to R.string.eng_pick_two_layers,
        "camadas desbloqueadas insuficientes" to R.string.eng_pick_two_layers,
        "escolha duas camadas ou mais" to R.string.eng_pick_two_layers,
        "nao foi possivel separar as letras (limite de 256 glifos)" to R.string.eng_glyph_limit,
        "texto sem letras visiveis" to R.string.eng_text_no_glyphs,
        "expressao longa demais" to R.string.eng_expression_too_long,
        "layout transform controlled by expression" to R.string.eng_expression_controls,
        "lente controlada por expressao" to R.string.eng_expression_controls,
        "trilha invalida" to R.string.eng_invalid_track,
        "trilhas invalidas" to R.string.eng_invalid_track,
        "preset invalido" to R.string.eng_preset_invalid,
        "preset de estudio invalido" to R.string.eng_preset_invalid,
        "selecione um modelo 3d" to R.string.eng_pick_model,
        "selecione um modelo 3d importado" to R.string.eng_pick_model,
        "selecione texto 3d" to R.string.eng_pick_text3d,
        "selecione texto 3d desbloqueado" to R.string.eng_pick_text3d,
        "selecione uma forma 3d" to R.string.eng_pick_shape3d,
        "selecione uma forma 3d desbloqueada" to R.string.eng_pick_shape3d,
        "nenhuma fonte disponivel neste aparelho" to R.string.eng_no_fonts,
        "rastreio precisa de camada de video" to R.string.eng_tracking_needs_video,
        "rastreio de mascara precisa de camada de video" to R.string.msg_o_rastreio_de_mascara_precisa_de,
        "a mascara nao encontrou detalhes suficientes para seguir" to R.string.eng_mask_track_failed,
        "mascara sem textura para seguir" to R.string.eng_mask_track_failed,
        "ponto sem textura para seguir" to R.string.eng_mask_track_failed,
        "nao foi possivel aplicar o rastreio" to R.string.eng_track_apply_failed,
        "trecho curto demais" to R.string.eng_range_too_short,
        "tempo do video mudou; reanalise o trecho" to R.string.eng_reanalyse,
        "source changed: analyse again" to R.string.eng_reanalyse,
        "source or timing changed*" to R.string.eng_reanalyse,
        "a analise pertence a outra composicao" to R.string.eng_reanalyse,
        "sem solucao" to R.string.eng_no_solution,
        "stabilization failed" to R.string.eng_no_solution,
        "invalid camera constraints" to R.string.eng_no_solution,
        "rastros de camera invalidos" to R.string.eng_no_solution,
        "rastreio nao terminou" to R.string.eng_no_solution,
        "not enough tracked frames*" to R.string.eng_not_enough_motion,
        "not enough reliable motion*" to R.string.eng_not_enough_motion,
        "nao deu para ler quadros suficientes do video" to R.string.eng_not_enough_motion,
        "incomplete video*" to R.string.eng_incomplete_video,
        "video incompleto*" to R.string.eng_incomplete_video,
        "cannot read analysis frame" to R.string.eng_incomplete_video,
        "video dimensions changed during analysis" to R.string.eng_incomplete_video,
        "analysis memory limit*" to R.string.eng_analysis_memory,
        "trecho excede o limite de memoria de analise*" to R.string.eng_analysis_memory,
        "selecione pontos resolvidos" to R.string.eng_pick_solved_points,
        "selecione pontos 3d resolvidos no trecho analisado" to R.string.eng_pick_solved_points,
        "nenhum ponto 3d selecionado" to R.string.eng_pick_solved_points,
        "nao foi possivel criar. selecione pontos resolvidos*" to R.string.eng_pick_solved_points,
        "corner pin needs four tracked corners" to R.string.eng_corner_pin,
        "select a separate, unparented 2d target" to R.string.eng_pick_2d_target,
        "analysis has no attachment points" to R.string.eng_no_analysis,
        "no completed analysis" to R.string.eng_no_analysis,
        "stabilize with the stabilizer, point or two points analysis" to R.string.eng_stabilize_which,
        "clipe longo demais para a camera 3d*" to R.string.eng_camera_clip_too_long,
        "camera parada no lugar (so gira): sem profundidade" to R.string.eng_camera_rotation_only,
        "new solve ready*" to R.string.eng_camera_new_solve,
        "nao foi possivel gerar legendas*" to R.string.eng_captions_failed,
        "modelo whisper indisponivel" to R.string.eng_whisper_failed,
        "memoria insuficiente para whisper" to R.string.eng_whisper_failed,
        "falha na transcricao local" to R.string.eng_whisper_failed,
        "whisper interrompido" to R.string.eng_whisper_failed,
        "transcricao muito longa" to R.string.eng_whisper_failed,
        "transcricao em andamento" to R.string.eng_whisper_failed,
        "camada sem fala" to R.string.eng_no_speech,
        "nenhuma palavra" to R.string.eng_no_speech,
        "rotobrush: modelo ou quadro indisponivel" to R.string.eng_roto_unavailable,
        "ai upscale model could not be loaded" to R.string.eng_ai_failed,
        "ai depth model could not be loaded" to R.string.eng_ai_failed,
        "ai depth inference failed" to R.string.eng_ai_failed,
        "ai inference could not allocate or process a tile" to R.string.eng_ai_failed,
        "vulkan inference unavailable" to R.string.eng_ai_failed,
        "ai model output shape differs from selected scale" to R.string.eng_ai_failed,
        "ai depth output shape differs from 256x256" to R.string.eng_ai_failed,
    )
    private val exact: Map<String, Int> = KNOWN.filterNot { it.first.endsWith("*") }.toMap()
    private val prefixes: List<Pair<String, Int>> = KNOWN.filter { it.first.endsWith("*") }.map { it.first.dropLast(1) to it.second }

    private val MARKS = Regex("\\p{Mn}+")
    private val SPACES = Regex("\\s+")

    internal fun normalize(raw: String): String {
        val decomposed = Normalizer.normalize(raw.trim().trimEnd('.').trim().lowercase(), Normalizer.Form.NFD)
        return decomposed.replace(MARKS, "").split(SPACES).filter { it.isNotEmpty() }.joinToString(" ")
    }

    /** Recurso do catálogo para uma frase conhecida do motor (null = desconhecida). */
    @StringRes
    fun resource(raw: String?): Int? {
        // Várias linhas (avisos do import): vale a primeira.
        val n = normalize(raw?.lineSequence()?.firstOrNull { it.isNotBlank() } ?: return null)
        if (n.isEmpty()) return null
        exact[n]?.let { return it }
        return prefixes.firstOrNull { n.startsWith(it.first) }?.second
    }

    /** O app está em português? (Só então a frase crua do motor pode aparecer.) */
    fun isPortuguese(context: Context): Boolean =
        AppText.resources(context).configuration.locales[0].language == "pt"

    /**
     * Motivo para ENCAIXAR numa frase do catálogo ("Não deu para exportar: %1$s"):
     * sem ponto final. [code] é o `aurea::Errc` da operação (0 = sem código).
     */
    fun reason(context: Context, raw: String?, code: Int = 0): String {
        val text = raw?.trim().orEmpty()
        resource(text)?.let { return AppText.get(context, it).trimEnd('.', ' ') }
        if (text.isNotEmpty() && isPortuguese(context)) return text
        if (code > 0) {
            val res = humanErrorResource(code)
            return (if (res == R.string.msg_erro_inesperado_codigo) AppText.get(context, res, code) else AppText.get(context, res)).trimEnd('.', ' ')
        }
        return AppText.get(context, R.string.eng_generic)
    }

    /** Frase SOZINHA (toast, linha de status): a primeira letra em maiúscula. */
    fun sentence(context: Context, raw: String?, code: Int = 0): String {
        val text = reason(context, raw, code)
        val locale = AppText.resources(context).configuration.locales[0]
        return text.replaceFirstChar { if (it.isLowerCase()) it.titlecase(locale) else it.toString() }
    }

    /** Progresso do upscale por IA ("IA: 3/120 · 45%") com o rótulo no idioma do app. */
    fun aiProgress(context: Context, raw: String): String =
        AppText.get(context, R.string.eng_ai_progress, raw.removePrefix("IA:").trim())
}
