// =============================================================================
//  Aurea / plugins / PluginUi.hpp
//
//  0.3.4 plugins: o que o `plugin.json` (formato 2) declara além do efeito —
//  painéis e ações de interface, o script e as permissões.
//
//  A interface é DECLARATIVA: o plugin diz que controles quer (botão, slider,
//  régua, cor, escolha, texto, lista, imagem, rótulo, chave) e onde (aba da
//  Home, painel do editor, menus, barra da camada); quem desenha são os
//  componentes do próprio AUREA, nas duas plataformas. Não existe HTML nem
//  webview: um plugin não consegue imitar telas do sistema nem pedir senha.
//
//  Referência completa: docs/plugins/README.md ("Painéis", "Permissões").
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aurea::plugins {

using LocalizedText = std::vector<std::pair<std::string, std::string>>;

// --- Limites -------------------------------------------------------------------
inline constexpr u32 kMaxPanels = 8;
inline constexpr u32 kMaxControlsPerPanel = 32;
inline constexpr u32 kMaxActions = 16;
inline constexpr u32 kMaxListItems = 200;
inline constexpr u32 kMaxChoiceOptions = 32;
inline constexpr u32 kMaxNetworkDomains = 8;
inline constexpr u32 kMaxStringKeys = 512;
inline constexpr usize kMaxScriptBytes = 512u * 1024u;
inline constexpr usize kMaxImageBytes = 2u << 20;       ///< imagem de painel no pacote
inline constexpr usize kMaxStorageBytes = 256u * 1024u;  ///< `storage` do plugin

// --- Lugares (bits) --------------------------------------------------------------
inline constexpr u32 kPlaceEditorPanel = 1u << 0;   ///< painel no editor (moldura PanelFrame)
inline constexpr u32 kPlaceHomeTab = 1u << 1;       ///< aba na Home
inline constexpr u32 kPlaceEditorMenu = 1u << 2;    ///< menu de plugins do editor
inline constexpr u32 kPlaceLayerMenu = 1u << 3;     ///< ⋮ da camada
inline constexpr u32 kPlaceLayerToolbar = 1u << 4;  ///< botão na barra da camada

// --- Permissões (bits) -------------------------------------------------------------
inline constexpr u32 kPermNetwork = 1u << 0;
inline constexpr u32 kPermClipboard = 1u << 1;
inline constexpr u32 kPermFiles = 1u << 2;

enum class ControlType : u8 { Button = 0, Slider, Ruler, Color, Choice, Text, List, Image, Label, Toggle };

[[nodiscard]] const char* control_type_name(ControlType t) noexcept;
[[nodiscard]] const char* placement_name(u32 bit) noexcept;
/// Os nomes de ícone aceitos (a lista que as duas plataformas mapeiam para os glifos do AUREA).
[[nodiscard]] bool valid_icon(std::string_view name) noexcept;
[[nodiscard]] const std::vector<std::string_view>& icon_names() noexcept;
/// Nome de função de script: [A-Za-z_$][A-Za-z0-9_$]{0,63}.
[[nodiscard]] bool valid_function_name(std::string_view name) noexcept;
/// Domínio de rede declarável: letras, dígitos, '-' e '.', com ponto, sem esquema/porta/caminho.
[[nodiscard]] bool valid_domain(std::string_view host) noexcept;

struct ListItem {
    std::string id;
    LocalizedText label;
    LocalizedText detail;
};

struct ControlDecl {
    std::string id;
    ControlType type = ControlType::Button;
    LocalizedText label;
    LocalizedText placeholder;        ///< text
    LocalizedText text;               ///< label (o texto mostrado)
    std::string style;                ///< button: "primary" | "secondary" | "danger"
    f64 min = 0.0;
    f64 max = 1.0;
    f64 step = 0.0;                   ///< 0 = contínuo
    f64 def = 0.0;                    ///< slider/ruler/choice
    bool defBool = false;             ///< toggle
    std::string defText;              ///< text ("…") e color ("#RRGGBB"/"#RRGGBBAA")
    std::string unit;                 ///< slider/ruler ("px", "%", "s")
    bool multiline = false;           ///< text
    u32 maxLength = 2000;             ///< text
    std::vector<LocalizedText> options;   ///< choice
    std::vector<ListItem> items;          ///< list (iniciais; o script troca)
    std::string src;                  ///< image: arquivo do pacote (png/jpg)
    f32 aspect = 0.0f;                ///< image: largura/altura (0 = 16:9)
    std::string onClick;              ///< button
    std::string onChange;             ///< slider, ruler, color, choice, text, toggle
    std::string onSelect;             ///< list
};

struct PanelDecl {
    std::string id;
    LocalizedText title;
    std::string icon;
    u32 placement = kPlaceEditorPanel;
    std::vector<ControlDecl> controls;
    std::string onOpen;               ///< chamada ao abrir o painel (opcional)
};

struct ActionDecl {
    std::string id;
    LocalizedText title;
    std::string icon;
    u32 placement = kPlaceEditorMenu;
    std::string call;                 ///< função do script
    std::string panel;                ///< ou: abre este painel
    bool needsLayer = false;          ///< só com uma camada escolhida
};

struct Permissions {
    std::vector<std::string> network;   ///< domínios (subdomínios inclusos)
    bool clipboard = false;
    bool files = false;
    [[nodiscard]] u32 mask() const noexcept {
        return (network.empty() ? 0u : kPermNetwork) | (clipboard ? kPermClipboard : 0u) | (files ? kPermFiles : 0u);
    }
};

struct UiDecl {
    std::vector<PanelDecl> panels;
    std::vector<ActionDecl> actions;
    [[nodiscard]] bool empty() const noexcept { return panels.empty() && actions.empty(); }
    [[nodiscard]] const ControlDecl* control(std::string_view id) const noexcept {
        for (const PanelDecl& p : panels)
            for (const ControlDecl& c : p.controls) if (c.id == id) return &c;
        return nullptr;
    }
    [[nodiscard]] const PanelDecl* panel(std::string_view id) const noexcept {
        for (const PanelDecl& p : panels) if (p.id == id) return &p;
        return nullptr;
    }
};

/// Textos do script por idioma (`app.t("chave")`): idioma → (chave → texto).
using StringTable = std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>;

} // namespace aurea::plugins
