import Foundation

// Texto que vem do MOTOR (C++/ponte ObjC++) no idioma do app.
//
// O motor descreve falhas com frases curtas em português (às vezes em inglês):
// "buffer do encoder menor que o quadro", "Not enough tracked frames". Mostrar
// isso cru fazia o português vazar na interface em inglês, espanhol, russo...
// Cada frase CONHECIDA vira uma chave do catálogo — a MESMA tabela do Android
// (`ui/i18n/EngineText.kt`); as duas saem de uma lista só e precisam bater.
//
// Frase desconhecida: em português aparece como veio (o motor já fala pt-BR);
// em qualquer outro idioma cai na mensagem do código de erro, ou numa frase
// genérica — nunca em português.
enum AureaEngineText {
    /// Texto normalizado (minúsculo, sem acento, sem ponto final) → chave. `*` no fim = prefixo.
    private static let known: [(String, String)] = [
        ("buffer do encoder menor que o quadro", "eng_encoder_buffer_small"),
        ("quadro com passo de linha menor que a largura", "eng_encoder_buffer_small"),
        ("encoder nao aceita essa resolucao/formato", "eng_encoder_format"),
        ("este aparelho nao codifica aac", "eng_audio_encoder"),
        ("encoder aac recusou a configuracao", "eng_audio_encoder"),
        ("encoder aac nao iniciou", "eng_audio_encoder"),
        ("encoder de audio parou", "eng_audio_encoder"),
        ("o encoder recusou o audio", "eng_audio_encoder"),
        ("buffer de audio invalido", "eng_audio_encoder"),
        ("encoder de video nao iniciou", "eng_video_encoder_start"),
        ("encoder de video parou de aceitar quadros", "eng_video_encoder_stalled"),
        ("encoder nao terminou", "eng_video_encoder_stalled"),
        ("encoder nao aceitou o fim do fluxo", "eng_video_encoder_stalled"),
        ("encoder recusou o fim do fluxo", "eng_video_encoder_stalled"),
        ("encoder nao entregou buffer de entrada", "eng_video_encoder_stalled"),
        ("encoder entregou pacote fora do buffer", "eng_video_encoder_stalled"),
        ("o encoder recusou o quadro", "eng_video_encoder_stalled"),
        ("o encoder recusou o arquivo", "eng_video_encoder_stalled"),
        ("muxer mp4 indisponivel", "eng_file_not_finalized"),
        ("o arquivo nao foi finalizado", "eng_file_not_finalized"),
        ("nenhum quadro chegou ao arquivo", "eng_file_not_finalized"),
        ("encoder indisponivel", "eng_no_encoder"),
        ("sem encoder de video nesta plataforma", "eng_no_encoder"),
        ("fabrica de midia (videotoolbox) indisponivel", "eng_no_encoder"),
        ("resolucao acima do que este aparelho exporta", "eng_res_device_limit"),
        ("resolucao acima do limite de textura da gpu", "eng_res_device_limit"),
        ("sem memoria de gpu para o export", "eng_export_no_memory"),
        ("sem memoria para ler os quadros do export", "eng_export_no_memory"),
        ("memoria insuficiente para esta resolucao", "eng_export_no_memory"),
        ("memoria insuficiente para upscale nesta resolucao", "eng_export_no_memory"),
        ("sem memoria para o quadro", "eng_export_no_memory"),
        ("todos os quadros rgba em uso", "eng_export_no_memory"),
        ("export precisa de gpu", "eng_no_gpu"),
        ("sem gpu", "eng_no_gpu"),
        ("sem backend grafico", "eng_no_gpu"),
        ("renderer sem backend", "eng_no_gpu"),
        ("ja existe um export em andamento", "eng_export_busy"),
        ("export ja aberto", "eng_export_busy"),
        ("gif longo demais: use um trecho menor ou menos quadros por segundo", "eng_too_long"),
        ("sequencia longa demais: use um trecho menor ou menos quadros por segundo", "eng_too_long"),
        ("sequencia maior que 4 gb", "eng_over_4gb"),
        ("pacote maior que 4 gb", "eng_over_4gb"),
        ("imagem grande demais para png", "eng_png_too_big"),
        ("nao foi possivel criar o gif", "eng_output_create"),
        ("nao foi possivel criar o .zip", "eng_output_create"),
        ("nao foi possivel criar o png", "eng_output_create"),
        ("nao foi possivel abrir a saida", "eng_output_create"),
        ("sem caminho de saida", "eng_output_create"),
        ("gif fechado", "eng_output_create"),
        ("zip fechado", "eng_output_create"),
        ("falha ao gravar o gif (armazenamento cheio?)", "msg_sem_espaco_no_aparelho_libere_espaco"),
        ("falha ao gravar o .zip (armazenamento cheio?)", "msg_sem_espaco_no_aparelho_libere_espaco"),
        ("falha ao gravar o png (armazenamento cheio?)", "msg_sem_espaco_no_aparelho_libere_espaco"),
        ("armazenamento cheio", "msg_sem_espaco_no_aparelho_libere_espaco"),
        ("armazenamento cheio durante a exportacao", "msg_sem_espaco_no_aparelho_libere_espaco"),
        ("sem espaco", "msg_sem_espaco_no_aparelho_libere_espaco"),
        ("tamanho de gif invalido", "eng_invalid_size"),
        ("imagem vazia", "eng_invalid_size"),
        ("quadro vazio", "eng_invalid_size"),
        ("dimensoes invalidas no upscale", "eng_invalid_size"),
        ("previa sem tamanho", "eng_invalid_size"),
        ("nenhum decoder seguro aceitou o video", "eng_decoder_failed"),
        ("decoder parado sem entregar frame", "eng_decoder_failed"),
        ("frame renderizado nao chegou ao imagereader", "eng_decoder_failed"),
        ("decoder sem progresso", "eng_decoder_failed"),
        ("sem decodificador", "eng_decoder_failed"),
        ("sem decodificador de video nesta plataforma", "eng_decoder_failed"),
        ("video ilegivel", "eng_decoder_failed"),
        ("decoder unavailable", "eng_decoder_failed"),
        ("cannot decode video", "eng_decoder_failed"),
        ("nao foi possivel abrir o decode", "eng_decoder_failed"),
        ("nenhum projeto aberto", "eng_no_project"),
        ("projeto fechado", "eng_no_project"),
        ("projeto fechado durante o import", "eng_no_project"),
        ("projeto sem composicao", "eng_no_project"),
        ("nenhuma composicao", "eng_no_project"),
        ("motor indisponivel", "eng_engine_unavailable"),
        ("motor nao esta pronto", "eng_engine_unavailable"),
        ("sem memoria para a ponte", "eng_engine_unavailable"),
        ("cancelado", "msg_cancelado"),
        ("cancelled", "msg_cancelado"),
        ("transcricao cancelada", "msg_cancelado"),
        ("arquivo nao encontrado", "msg_arquivo_nao_encontrado"),
        ("arquivo do projeto nao encontrado", "msg_arquivo_nao_encontrado"),
        ("sem arquivo", "msg_arquivo_nao_encontrado"),
        ("container nao reconhecido", "msg_formato_de_arquivo_nao_suportado"),
        ("conteiner ilegivel", "msg_formato_de_arquivo_nao_suportado"),
        ("formato nao suportado", "msg_formato_de_arquivo_nao_suportado"),
        ("arquivo 3d invalido", "eng_model_invalid"),
        ("buffer do modelo ausente", "eng_model_invalid"),
        ("objeto 3d nao encontrado", "eng_model_invalid"),
        ("modelo sem geometria", "eng_model_no_geometry"),
        ("modelo comprimido com draco: exporte sem compressao", "eng_model_compressed"),
        ("modelo comprimido com meshopt (khr): exporte sem compressao", "eng_model_compressed"),
        ("forma 3d sem geometria", "eng_model_no_geometry"),
        ("arquivo sem trilha de video decodificavel", "eng_no_video_track"),
        ("sem trilha de video", "eng_no_video_track"),
        ("camada sem video", "eng_no_video_track"),
        ("arquivo sem trilha de audio decodificavel", "eng_no_audio_track"),
        ("sem trilha de audio", "eng_no_audio_track"),
        ("midia sem audio compativel", "eng_no_audio_track"),
        ("sem decoder para este audio", "eng_no_audio_track"),
        ("sem decodificador de audio nesta plataforma", "eng_no_audio_track"),
        ("este video nao tem som", "eng_no_sound"),
        ("camada sem audio", "eng_no_sound"),
        ("camada sem som", "eng_no_sound"),
        ("intervalo sem audio", "eng_no_sound"),
        ("imagem ilegivel", "eng_unreadable_media"),
        ("midia ilegivel", "eng_unreadable_media"),
        ("arquivo ilegivel", "eng_unreadable_media"),
        ("audio ilegivel", "eng_unreadable_media"),
        ("origem ilegivel", "eng_unreadable_media"),
        ("nao foi possivel ler o arquivo", "eng_unreadable_media"),
        ("leitura do arquivo falhou", "eng_unreadable_media"),
        ("nao e uma fonte ttf/otf legivel", "eng_bad_font"),
        ("ambiente nao lido*", "eng_bad_hdri"),
        ("lut vazio ou maior que 32 mb", "eng_bad_lut"),
        ("nao foi possivel ler o lut (limite 32 mb)", "eng_bad_lut"),
        ("lut incompleto", "eng_bad_lut"),
        ("dados do lut invalidos", "eng_bad_lut"),
        ("colunas inesperadas no lut", "eng_bad_lut"),
        ("dominio do lut invalido", "eng_bad_lut"),
        ("dominio do lut sem intervalo", "eng_bad_lut"),
        ("linha do lut longa demais", "eng_bad_lut"),
        ("lut combinado ou tamanho invalido", "eng_bad_lut"),
        ("lut 3d suporta*", "eng_bad_lut"),
        ("arquivo lut indisponivel", "eng_bad_lut"),
        ("nao e um projeto do aurea", "project_file_err_not_project"),
        ("nao e um arquivo .aurea", "project_file_err_not_project"),
        ("nao e um pacote", "project_file_err_not_project"),
        ("feito por uma versao mais nova", "msg_este_projeto_foi_salvo_por_uma"),
        ("pacote danificado", "msg_o_projeto_esta_danificado_e_nao"),
        ("pacote cortado", "msg_o_projeto_esta_danificado_e_nao"),
        ("projeto ilegivel", "msg_o_projeto_esta_danificado_e_nao"),
        ("secao corrompida", "msg_o_projeto_esta_danificado_e_nao"),
        ("nenhuma copia valida do projeto", "msg_o_projeto_esta_danificado_e_nao"),
        ("camada bloqueada", "eng_layer_locked"),
        ("camada nao encontrada", "eng_layer_missing"),
        ("camada sumiu", "eng_layer_missing"),
        ("camada nao existe", "eng_layer_missing"),
        ("camadas nao encontradas", "eng_layer_missing"),
        ("nenhuma camada", "eng_layer_missing"),
        ("nada para agrupar", "eng_nothing_to_group"),
        ("nada para pre-compor", "eng_nothing_to_group"),
        ("nao e um grupo", "eng_not_group"),
        ("nao e uma pre-composicao", "eng_not_group"),
        ("grupo nao encontrado", "eng_not_group"),
        ("nao esta dentro de um grupo", "eng_not_in_group"),
        ("a camada tem pai fora do grupo", "eng_group_links"),
        ("a camada tem pai dentro do grupo", "eng_group_links"),
        ("outra camada tem esta como pai", "eng_group_links"),
        ("outra camada do grupo depende desta", "eng_group_links"),
        ("a camada usa uma matte do grupo", "eng_group_links"),
        ("o grupo tem outra taxa de quadros", "eng_group_timing"),
        ("o tempo do grupo foi alterado", "eng_group_timing"),
        ("um grupo nao entra nele mesmo", "eng_group_self"),
        ("nao deu para agrupar", "eng_group_failed"),
        ("nao deu para tirar do grupo", "eng_group_failed"),
        ("nao deu para desagrupar", "eng_group_failed"),
        ("nenhuma camada desbloqueada", "eng_no_unlocked_layer"),
        ("escolha duas camadas desbloqueadas", "eng_pick_two_layers"),
        ("camadas desbloqueadas insuficientes", "eng_pick_two_layers"),
        ("escolha duas camadas ou mais", "eng_pick_two_layers"),
        ("nao foi possivel separar as letras (limite de 256 glifos)", "eng_glyph_limit"),
        ("texto sem letras visiveis", "eng_text_no_glyphs"),
        ("expressao longa demais", "eng_expression_too_long"),
        ("layout transform controlled by expression", "eng_expression_controls"),
        ("lente controlada por expressao", "eng_expression_controls"),
        ("trilha invalida", "eng_invalid_track"),
        ("trilhas invalidas", "eng_invalid_track"),
        ("preset invalido", "eng_preset_invalid"),
        ("preset de estudio invalido", "eng_preset_invalid"),
        ("selecione um modelo 3d", "eng_pick_model"),
        ("selecione um modelo 3d importado", "eng_pick_model"),
        ("selecione texto 3d", "eng_pick_text3d"),
        ("selecione texto 3d desbloqueado", "eng_pick_text3d"),
        ("selecione uma forma 3d", "eng_pick_shape3d"),
        ("selecione uma forma 3d desbloqueada", "eng_pick_shape3d"),
        ("nenhuma fonte disponivel neste aparelho", "eng_no_fonts"),
        ("rastreio precisa de camada de video", "eng_tracking_needs_video"),
        ("rastreio de mascara precisa de camada de video", "msg_o_rastreio_de_mascara_precisa_de"),
        ("a mascara nao encontrou detalhes suficientes para seguir", "eng_mask_track_failed"),
        ("mascara sem textura para seguir", "eng_mask_track_failed"),
        ("ponto sem textura para seguir", "eng_mask_track_failed"),
        ("nao foi possivel aplicar o rastreio", "eng_track_apply_failed"),
        ("trecho curto demais", "eng_range_too_short"),
        ("tempo do video mudou; reanalise o trecho", "eng_reanalyse"),
        ("source changed: analyse again", "eng_reanalyse"),
        ("source or timing changed*", "eng_reanalyse"),
        ("a analise pertence a outra composicao", "eng_reanalyse"),
        ("sem solucao", "eng_no_solution"),
        ("stabilization failed", "eng_no_solution"),
        ("invalid camera constraints", "eng_no_solution"),
        ("rastros de camera invalidos", "eng_no_solution"),
        ("rastreio nao terminou", "eng_no_solution"),
        ("not enough tracked frames*", "eng_not_enough_motion"),
        ("not enough reliable motion*", "eng_not_enough_motion"),
        ("nao deu para ler quadros suficientes do video", "eng_not_enough_motion"),
        ("incomplete video*", "eng_incomplete_video"),
        ("video incompleto*", "eng_incomplete_video"),
        ("cannot read analysis frame", "eng_incomplete_video"),
        ("video dimensions changed during analysis", "eng_incomplete_video"),
        ("analysis memory limit*", "eng_analysis_memory"),
        ("trecho excede o limite de memoria de analise*", "eng_analysis_memory"),
        ("selecione pontos resolvidos", "eng_pick_solved_points"),
        ("selecione pontos 3d resolvidos no trecho analisado", "eng_pick_solved_points"),
        ("nenhum ponto 3d selecionado", "eng_pick_solved_points"),
        ("nao foi possivel criar. selecione pontos resolvidos*", "eng_pick_solved_points"),
        ("corner pin needs four tracked corners", "eng_corner_pin"),
        ("select a separate, unparented 2d target", "eng_pick_2d_target"),
        ("analysis has no attachment points", "eng_no_analysis"),
        ("no completed analysis", "eng_no_analysis"),
        ("stabilize with the stabilizer, point or two points analysis", "eng_stabilize_which"),
        ("clipe longo demais para a camera 3d*", "eng_camera_clip_too_long"),
        ("camera parada no lugar (so gira): sem profundidade", "eng_camera_rotation_only"),
        ("new solve ready*", "eng_camera_new_solve"),
        ("nao foi possivel gerar legendas*", "eng_captions_failed"),
        ("modelo whisper indisponivel", "eng_whisper_failed"),
        ("memoria insuficiente para whisper", "eng_whisper_failed"),
        ("falha na transcricao local", "eng_whisper_failed"),
        ("whisper interrompido", "eng_whisper_failed"),
        ("transcricao muito longa", "eng_whisper_failed"),
        ("transcricao em andamento", "eng_whisper_failed"),
        ("camada sem fala", "eng_no_speech"),
        ("nenhuma palavra", "eng_no_speech"),
        ("rotobrush: modelo ou quadro indisponivel", "eng_roto_unavailable"),
        ("ai upscale model could not be loaded", "eng_ai_failed"),
        ("ai depth model could not be loaded", "eng_ai_failed"),
        ("ai depth inference failed", "eng_ai_failed"),
        ("ai inference could not allocate or process a tile", "eng_ai_failed"),
        ("vulkan inference unavailable", "eng_ai_failed"),
        ("ai model output shape differs from selected scale", "eng_ai_failed"),
        ("ai depth output shape differs from 256x256", "eng_ai_failed"),
    ]
    private static let exact: [String: String] = {
        var map: [String: String] = [:]
        for (text, key) in known where !text.hasSuffix("*") { map[text] = key }
        return map
    }()
    private static let prefixes: [(String, String)] = known.filter { $0.0.hasSuffix("*") }.map { (String($0.0.dropLast()), $0.1) }

    static func normalize(_ raw: String) -> String {
        var text = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        while text.hasSuffix(".") { text.removeLast() }
        text = text.folding(options: [.diacriticInsensitive, .caseInsensitive], locale: Locale(identifier: "en_US_POSIX")).lowercased()
        return text.split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
    }

    /// Chave do catálogo para uma frase conhecida do motor (nil = desconhecida).
    static func key(_ raw: String?) -> String? {
        // Várias linhas (avisos do import): vale a primeira.
        guard let line = raw?.split(whereSeparator: { $0.isNewline }).first(where: { !$0.trimmingCharacters(in: .whitespaces).isEmpty }) else { return nil }
        let n = normalize(String(line))
        if n.isEmpty { return nil }
        if let key = exact[n] { return key }
        return prefixes.first { n.hasPrefix($0.0) }?.1
    }

    /// O app está em português? (Só então a frase crua do motor pode aparecer.)
    static var isPortuguese: Bool { AureaText.language.resolved == .pt }

    /// Mensagem do `aurea::Errc` (mesmos números do Android, `humanErrorResource`).
    static func errcKey(_ code: Int) -> String? {
        switch code {
        case 3: return "msg_arquivo_nao_encontrado"
        case 6, 24: return "msg_recurso_nao_suportado_neste_aparelho"
        case 8, 9: return "msg_memoria_insuficiente_feche_outros_apps_e"
        case 10: return "msg_erro_ao_ler_ou_gravar_o"
        case 11, 13: return "msg_o_arquivo_esta_danificado"
        case 12: return "msg_este_projeto_foi_salvo_por_uma"
        case 14: return "msg_nao_foi_possivel_decodificar_a_midia"
        case 15: return "msg_falha_ao_codificar_o_video"
        case 16: return "msg_codec_de_video_nao_suportado_por"
        case 17: return "msg_formato_de_arquivo_nao_suportado"
        case 18: return "msg_a_midia_original_nao_esta_mais"
        case 19: return "msg_a_gpu_foi_reiniciada_tente_de"
        case 20: return "msg_memoria_de_video_gpu_insuficiente_baixe"
        case 25: return "msg_cancelado"
        case 28: return "msg_sem_espaco_no_aparelho_libere_espaco"
        case 29: return "msg_o_arquivo_de_midia_esta_danificado"
        case 30: return "msg_o_projeto_esta_danificado_e_nao"
        case 31: return "msg_este_aparelho_nao_tem_codificador_para"
        default: return nil
        }
    }

    private static func trimmed(_ text: String) -> String {
        var t = text.trimmingCharacters(in: .whitespaces)
        while t.hasSuffix(".") { t.removeLast() }
        return t
    }

    /// Motivo para ENCAIXAR numa frase do catálogo ("Não deu para exportar: %@"):
    /// sem ponto final. `code` é o `aurea::Errc` da operação (0 = sem código).
    static func reason(_ raw: String?, code: Int = 0) -> String {
        let text = raw?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if let key = key(text) { return trimmed(AureaText.t(key)) }
        if !text.isEmpty && isPortuguese { return text }
        if code > 0 {
            if let key = errcKey(code) { return trimmed(AureaText.t(key)) }
            return trimmed(AureaText.t("msg_erro_inesperado_codigo", String(code)))
        }
        return AureaText.t("eng_generic")
    }

    /// Frase SOZINHA (toast, linha de status): a primeira letra em maiúscula.
    static func sentence(_ raw: String?, code: Int = 0) -> String {
        let text = reason(raw, code: code)
        guard let first = text.first, first.isLowercase else { return text }
        return first.uppercased() + text.dropFirst()
    }

    /// Progresso do upscale por IA ("IA: 3/120 · 45%") com o rótulo no idioma do app.
    static func aiProgress(_ raw: String) -> String {
        var body = raw
        if body.hasPrefix("IA:") { body.removeFirst(3) }
        return AureaText.t("eng_ai_progress", body.trimmingCharacters(in: .whitespaces))
    }
}
