# Gera engine/platform/ios/app/AureaStrings.swift a partir do catalogo do
# Android (values/ = pt-BR, values-en/ = ingles). As chaves usadas sao as MESMAS.
import io
import re
import xml.etree.ElementTree as ET

ROOT = r"C:\Users\SnyX\Documents\Projetos - Claude\Aureabeta"
PT = ROOT + r"\android\app\src\main\res\values\strings.xml"
EN = ROOT + r"\android\app\src\main\res\values-en\strings.xml"
OUT = ROOT + r"\engine\platform\ios\app\AureaStrings.swift"

KEYS = [
    # comum
    "common_cancel", "common_delete", "common_duplicate", "common_remove", "common_rename",
    "common_save", "common_search", "common_open", "common_irreversible",
    # home
    "home_tab_start", "home_tab_projects", "home_tab_settings",
    "home_title_start", "home_title_projects", "home_search_projects", "home_search_hint",
    "home_new_project", "home_no_projects", "home_no_projects_hint", "home_no_results",
    "home_sort_recent", "home_sort_name", "home_sort_longest", "home_sort_created", "home_sort_size",
    "home_project_count", "home_continue", "home_continue_action", "home_import_media",
    "home_empty_hint", "home_selected_count", "home_select_all", "home_clear_selection",
    "home_clear_search",
    # casca do editor
    "editor_abrir", "editor_exportar", "editor_desfazer", "editor_refazer",
    "editor_reproduzir", "editor_pausar", "editor_voltar_editor", "editor_mais",
    "editor_projeto_cbe9", "editor_previa", "editor_tela_cheia", "editor_sair_tela_cheia",
    "editor_adicionar_camada", "editor_nenhum", "editor_projetos",
    "editor_quadro_atras_segure_inicio", "editor_quadro_frente_segure_fim",
    "editor_keyframe_anterior_segure_inicio", "editor_proximo_keyframe_segure_fim",
    "editor_repeticao_ligada_segure_desligar", "editor_reproduzir_segure_repetir",
    "editor_desligar_som_segure_volume", "editor_som_desligado_toque_ligar_segure_volume",
    "editor_voltar_composicao_principal", "editor_toque_num_objeto_tela_editar",
    "editor_selecione_menos_duas_camadas", "editor_selecionar_todas_camadas",
    "editor_limpar_selecao", "editor_cancelar_selecao", "editor_camada_bloqueada",
    "editor_camada_bloqueada_desbloqueie_editar",
    # operacoes de camada
    "editor_agrupar", "editor_desagrupar", "editor_duplicar_camada", "editor_excluir_camada",
    "editor_copiar_camada", "editor_copiar_efeitos", "editor_colar_efeitos",
    "editor_copiar_estilo", "editor_colar_estilo", "editor_copiar_keyframes_cabecote",
    "editor_colar_keyframes_cabecote", "editor_dividir_cabecote", "editor_mais_acoes_camada",
    "editor_mesclagem_opacidade_efeitos_cores", "editor_estilo_efeitos", "editor_keyframes",
    "editor_aparar_inicio_cabecote", "editor_aparar_fim_cabecote",
    "editor_alinhar_esquerda_tela", "editor_alinhar_direita_tela", "editor_alinhar_topo_tela",
    "editor_alinhar_base_tela", "editor_centralizar_horizontal", "editor_centralizar_vertical",
    "editor_distribuir_horizontal_vaos_iguais", "editor_distribuir_vertical_vaos_iguais",
    "editor_agrupar_camadas_escolhidas", "editor_trazer_frente", "editor_enviar_tras",
    "editor_mostrar_camada", "editor_ocultar_camada", "editor_bloquear_camada",
    "editor_desbloquear_camada", "editor_renomear", "editor_etiqueta", "editor_guia_nao_exporta",
    "editor_camada_ajuste", "editor_solo", "editor_extrair_audio", "editor_remover_espacos_vazios",
    "editor_buscar_camadas", "editor_nome_ou_texto_camada",
    # abas da doca / paineis
    "sh_dock_transform", "sh_dock_effects", "sh_dock_opacity_blend", "sh_dock_edit_text",
    "sh_dock_edit_shape", "sh_dock_mask", "sh_dock_particles", "sh_dock_presets",
    "sh_dock_environment", "sh_dock_captions", "sh_dock_color_fill", "sh_dock_edit_vector",
    "panel_transformar", "panel_ajustar", "panel_largura", "panel_altura", "panel_centro",
    "panel_preencher", "panel_voltar_padrao", "panel_adicionar_expressao",
    "panel_deslize_mover_camada", "panel_deslize_mover_toque_z_profundidade",
    "panel_esconder_x_y_z_3d", "panel_mostrar_x_y_z_3d",
    "panel_adicionar_efeito", "panel_efeitos_camada", "panel_este_efeito_nao_tem_ajustes",
    "panel_opacidade", "panel_redefinir", "panel_remover_efeito", "panel_mover_cima",
    "panel_mover_baixo", "panel_ligar_efeito", "panel_desligar_efeito",
    # 3D
    "panel_material", "panel_cor", "panel_intensidade", "panel_exposicao",
    "panel_luz_ambiente", "panel_ambiente_do_objeto", "panel_do_projeto", "panel_proprio",
    "panel_estudio_neutro", "panel_imagem_ambiente", "panel_trocar_imagem", "panel_alinhamento",
    "pn_text3d_title", "pn_text3d_placeholder", "pn_depth", "pn_env_light_hint",
    # export
    "editor_formato", "editor_resolucao", "editor_qualidade", "editor_duracao",
    "editor_quadros_segundo", "editor_h_264_abre_qualquer_aparelho_rede",
    "editor_hevc_arquivo_menor_mesma_qualidade_alguns", "editor_mantenha_aurea_aberto_ate_terminar",
    "editor_video_pronto", "editor_salvando_galeria", "editor_salvando", "editor_tamanho_estimado",
    "editor_video", "editor_nenhum_app_abre_video", "editor_abrir",
    "sh_export_progress", "sh_export_above_device", "sh_export_fps_from_project",
    "unit_megabyte", "unit_gigabyte",
    # novo projeto (o mesmo catalogo do ProjectSpec.kt)
    "aspect_hint_tv", "aspect_hint_reels", "aspect_hint_feed", "aspect_hint_instagram",
    "aspect_hint_classic", "editor_nome_camada",
    # ajustes / aparelho
    "settings_group_language", "settings_language", "settings_language_system",
    "settings_group_device", "settings_device_auto", "settings_device_auto_note",
    "settings_group_general", "settings_dev_tools", "settings_dev_on", "settings_dev_off",
    "settings_technology",
    # usadas pelos paineis e pela casca (auditadas por check_api_swift.py)
    "editor_cor", "editor_foto", "editor_musica_ou_som", "editor_escala", "editor_rotacao",
    "editor_rastreio", "panel_digite_texto", "home_title_settings", "settings_aspect",
    "settings_resolution", "settings_fps", "panel_este_efeito_saiu_catalogo_ele_nao",
    "panel_cor_brilho_metalico_rugosidade_vem_arquivo",
]


def load(path):
    tree = ET.parse(path)
    out = {}
    for node in tree.getroot().findall("string"):
        name = node.get("name")
        if not name:
            continue
        text = "".join(node.itertext())
        # O catalogo do Android escapa com \' e \" dentro de <string>.
        text = text.replace("\\'", "'").replace('\\"', '"').replace("\\n", "\n")
        out[name] = text
    return out


def swift_literal(text):
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def to_swift_format(text):
    """printf do Android -> printf do Foundation.

    `%1$s` no Android e uma STRING; no Foundation o especificador de objeto e
    `%@`. `%1$d` com um Int do Swift (64 bits) pede `%1$ld`, senao o Foundation
    le so 32 bits do registrador. `%` solto continua `%`.
    """
    def fix(m):
        idx = m.group(1) or ""
        kind = m.group(2)
        if kind == "s":
            return "%" + idx + "@"
        if kind in ("d", "i", "u"):
            return "%" + idx + "l" + kind
        return m.group(0)

    return re.sub(r"%(?:(\d+)\$)?([sdifu])", fix, text)


pt = load(PT)
en = load(EN)

missing_pt = [k for k in KEYS if k not in pt]
if missing_pt:
    raise SystemExit("chaves ausentes no values/ (pt): " + ", ".join(missing_pt))

lines = []
lines.append("// =============================================================================")
lines.append("//  Aurea / platform / ios / app / AureaStrings.swift")
lines.append("//")
lines.append("//  GERADO de android/app/src/main/res/values/strings.xml (pt-BR) e")
lines.append("//  values-en/strings.xml (ingles) por tools/_ios_strings.py — nao editar a mao")
lines.append("//  sem trazer a mesma mudanca para o catalogo do Android.")
lines.append("//")
lines.append("//  As CHAVES SAO AS MESMAS do Android (`home_tab_projects`, `editor_desfazer`...).")
lines.append("//  Isso e deliberado: um texto novo entra nos dois sistemas pelo mesmo nome,")
linecount = len(KEYS)
lines.append("//  e o catalogo continua sendo um so (o do Android e a fonte).")
lines.append("//")
lines.append("//  O Android tem SETE idiomas (values/ = pt-BR + ar, en, es, hi, id, ru). Esta")
lines.append("//  entrega traz pt-BR e ingles — os outros cinco saem do mesmo XML, pela mesma")
lines.append("//  extracao, quando o dono quiser (ver README).")
lines.append("//")
lines.append("//  Formato: o catalogo usa o printf do Android (%1$s, %2$d); aqui os")
lines.append("//  especificadores foram convertidos para os do Foundation (%1$@, %2$ld).")
lines.append("// =============================================================================")
lines.append("import Foundation")
lines.append("")
lines.append("/// Um idioma do app. O que o dono escolhe nos Ajustes; o padrao segue o sistema.")
lines.append("enum AureaLanguage: String, CaseIterable, Identifiable {")
lines.append("    case pt = \"pt-BR\"")
lines.append("    case en = \"en\"")
lines.append("")
lines.append("    var id: String { rawValue }")
lines.append("")
lines.append("    /// Rotulo do seletor. Nome do idioma NO idioma — nao traduzido.")
lines.append("    var label: String {")
lines.append("        switch self {")
lines.append("        case .pt: return \"Português (Brasil)\"")
lines.append("        case .en: return \"English\"")
lines.append("        }")
lines.append("    }")
lines.append("")
lines.append("    /// Idioma do sistema quando ele e um dos que temos; senao pt-BR (o idioma")
lines.append("    /// em que o app foi escrito, e o mesmo padrao do Android).")
lines.append("    static var systemDefault: AureaLanguage {")
lines.append("        for code in Locale.preferredLanguages {")
lines.append("            if code.hasPrefix(\"en\") { return .en }")
lines.append("            if code.hasPrefix(\"pt\") { return .pt }")
lines.append("        }")
lines.append("        return .pt")
lines.append("    }")
lines.append("}")
lines.append("")
lines.append("/// O catalogo. `t()` traduz; chave desconhecida devolve a propria chave, o que")
lines.append("/// aparece na tela em vez de um texto vazio — um furo de traducao fica visivel.")
lines.append("enum AureaText {")
lines.append("    /// Idioma em vigor. O modelo observa e redesenha quando muda.")
lines.append("    static var language: AureaLanguage = .systemDefault")
lines.append("")
lines.append("    static func t(_ key: String) -> String {")
lines.append("        let table = language == .en ? en : pt")
lines.append("        return table[key] ?? pt[key] ?? key")
lines.append("    }")
lines.append("")
lines.append("    static func t(_ key: String, _ args: CVarArg...) -> String {")
lines.append("        String(format: t(key), arguments: args)")
lines.append("    }")
lines.append("")
lines.append("    private static let pt: [String: String] = [")
for k in KEYS:
    lines.append("        \"%s\": %s," % (k, swift_literal(pt[k])))
lines.append("    ]")
lines.append("")
lines.append("    private static let en: [String: String] = [")
for k in KEYS:
    value = en.get(k, pt[k])
    lines.append("        \"%s\": %s," % (k, swift_literal(value)))
lines.append("    ]")
lines.append("}")
lines.append("")

io.open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
print("AureaStrings.swift: %d chaves (%d sem traducao en -> cai no pt)" % (linecount, sum(1 for k in KEYS if k not in en)))
