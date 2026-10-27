// =============================================================================
//  Aurea / plugins / PluginRuntime.hpp
//
//  0.3.4 plugins: o runtime de UM plugin com script — o contexto AureaScript,
//  o estado dos painéis (valores dos controles, listas, textos), o console de
//  erros, o `storage` e as APIs que dependem de permissão (`fetch`,
//  `clipboard`, `files`).
//
//  A API de objetos do AUREA (app.project, camadas, keyframes...) não mora
//  aqui: o motor a instala no contexto (`ApiInstaller`, EnginePluginsV2.cpp).
//  Assim este runtime não conhece o Engine e o teste o exercita sozinho.
//
//  Threads: `call`/`event`/`open_panel` rodam na thread de quem chama (a
//  interface usa uma thread de trabalho com pilha grande, nunca a principal),
//  UMA por vez por plugin (a segunda recebe "ocupado"). `state_json`,
//  `console_json` e `cancel` valem de qualquer thread, inclusive durante a chamada.
//
//  Toda resposta é JSON (o formato está em docs/plugins/README.md, "API do motor").
// =============================================================================
#pragma once

#include "aurea/plugins/Plugin.hpp"
#include "aurea/plugins/Script.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace aurea::plugins {

// --- E/S do anfitrião (a plataforma implementa; o motor confere a permissão antes) ---
struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    u32 timeoutMs = 15000;
    usize maxBytes = 2u << 20;
};
struct HttpResponse {
    bool ok = false;            ///< transporte: houve resposta HTTP
    i32 status = 0;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::string error;          ///< sem resposta: o motivo (tempo, DNS, TLS, tamanho)
};
struct PickedFile {
    bool ok = false;            ///< falso = a pessoa cancelou (ou falhou: `error`)
    std::string name;
    std::string path;           ///< cópia legível pelo motor (cache do app)
    u64 size = 0;
    std::string error;
};
/// A plataforma NÃO segue redirecionamentos (um 3xx volta ao script como está):
/// o domínio conferido é o domínio contatado.
struct HostIO {
    std::function<HttpResponse(const HttpRequest&)> fetch;
    std::function<bool(std::string& text)> clipboardRead;
    std::function<bool(const std::string& text)> clipboardWrite;
    std::function<PickedFile(const std::string& pluginId, const std::vector<std::string>& types)> pickFile;
};

/// Em volta de cada chamada vinda da interface (o motor abre/fecha o grupo de desfazer).
struct CallHooks {
    std::function<void(const std::string& label)> begin;
    std::function<void()> end;
};

inline constexpr usize kMaxConsoleLines = 200;
inline constexpr usize kMaxFetchBodyOut = 256u * 1024u;
inline constexpr u32 kMaxFetchTimeoutMs = 30000;
inline constexpr usize kMaxPickedFileBytes = 1u << 20;
/// `Object::hostTag` de um quadro renderizado (comp.renderFrame): `hostData` é
/// um std::string com o caminho do PNG. `ui.setImage` aceita esse objeto.
inline constexpr u32 kHostTagFrame = 0xF4A3u;

class PluginRuntime {
public:
    using ApiInstaller = std::function<void(script::Context&, PluginRuntime&)>;

    PluginRuntime(PluginInfo info, std::string dataDirectory, script::Limits limits = {});
    ~PluginRuntime();
    PluginRuntime(const PluginRuntime&) = delete;
    PluginRuntime& operator=(const PluginRuntime&) = delete;

    [[nodiscard]] const PluginInfo& info() const noexcept { return info_; }
    /// Permissões concedidas (o gerenciador muda sem recriar o runtime).
    void set_granted(u32 mask) noexcept { granted_ = mask & info_.permissions.mask(); }
    [[nodiscard]] u32 granted() const noexcept { return granted_; }
    void set_io(std::shared_ptr<const HostIO> io) noexcept;

    /// Chama a função `function` do script com `argsJson` (lista JSON).
    [[nodiscard]] std::string call(std::string_view function, std::string_view argsJson, std::string_view locale,
                                   const ApiInstaller& install, const CallHooks& hooks) noexcept;
    /// A pessoa mexeu num controle: grava o valor e chama o onClick/onChange/onSelect.
    /// `kind`: "click" | "change" | "select". `valueJson`: o valor novo (JSON).
    [[nodiscard]] std::string event(std::string_view control, std::string_view kind, std::string_view valueJson,
                                    std::string_view locale, const ApiInstaller& install, const CallHooks& hooks) noexcept;
    /// O painel abriu: chama o `onOpen` dele (se houver) e devolve o estado.
    [[nodiscard]] std::string open_panel(std::string_view panel, std::string_view locale, const ApiInstaller& install,
                                         const CallHooks& hooks) noexcept;

    /// Estado dos controles: {values, items, texts, labels, disabled, hidden, images, progress, busy}.
    [[nodiscard]] std::string state_json() const;
    /// Últimas linhas do console: [{level, text, time}] (level 0 log .. 3 erro).
    [[nodiscard]] std::string console_json() const;
    void clear_console();
    /// Para a chamada em andamento (qualquer thread).
    void cancel() noexcept;
    [[nodiscard]] bool busy() const noexcept { return busy_.load(std::memory_order_acquire); }
    /// Joga o contexto fora: a próxima chamada recarrega o script (estado dos controles fica).
    void reload();

    // --- Para a API (nativas do motor e deste runtime) ----------------------------
    void log(int level, std::string text);
    void message(std::string kind, std::string text);
    void request_panel(std::string panel) { openPanel_ = std::move(panel); }
    [[nodiscard]] const std::string& locale() const noexcept { return locale_; }
    /// Texto do manifesto (`strings`) no idioma da chamada; vazio = não há.
    [[nodiscard]] std::string text(std::string_view key) const;
    /// Caminho de um arquivo DO PACOTE (relativo, sem ".."); vazio = inválido/ausente.
    [[nodiscard]] std::string package_file(std::string_view relative) const;
    /// Pasta de dados do plugin (`<plugins>/.dados/<id>`), criada sob demanda.
    [[nodiscard]] std::string data_folder() const;
    /// Imagem de um controle `image` (caminho absoluto; vazio limpa).
    void set_image(std::string_view control, std::string path);

private:
    struct ControlState {
        const ControlDecl* decl = nullptr;
        f64 number = 0.0;
        bool flag = false;
        std::string text;                ///< text/color/label; list = id escolhido
        bool hasSelection = false;       ///< list
        bool textSet = false;            ///< label: o script trocou o texto
        bool itemsFromScript = false;    ///< list: o script trocou os itens
        std::string label;               ///< rótulo trocado pelo script (vazio = o do manifesto)
        bool disabled = false;
        bool hidden = false;
        std::vector<std::pair<std::string, std::string>> items;   ///< list: id, rótulo
        std::vector<std::string> details;
        std::string image;               ///< image: caminho absoluto
    };
    struct Message { std::string kind; std::string text; };
    struct ConsoleLine { int level = 0; std::string text; f64 time = 0.0; };

    bool ensure_loaded(const ApiInstaller& install, script::ScriptError& error);
    void install_runtime_api();
    std::string run(const std::function<bool(script::Value& result, script::ScriptError& error)>& body, std::string_view label,
                    std::string_view locale, const ApiInstaller& install, const CallHooks& hooks) noexcept;
    std::string result_json(bool ok, const std::string& valueJson, const script::ScriptError* error);
    ControlState* control(std::string_view id);
    script::Value control_value(script::Context& c, const ControlState& s);
    void set_control_value(script::Context& c, ControlState& s, const script::Value& v);
    std::string state_json_locked() const;
    void load_storage();
    void save_storage();

    PluginInfo info_;
    std::string dataDirectory_;
    script::Limits limits_;
    std::unique_ptr<script::Context> ctx_;
    bool loaded_ = false;
    script::ScriptError loadError_;
    bool loadFailed_ = false;
    u32 granted_ = 0;
    std::shared_ptr<const HostIO> io_;
    std::mutex ioMutex_;

    std::mutex callMutex_;                 ///< uma chamada por vez
    std::atomic<bool> busy_{false};
    mutable std::mutex stateMutex_;        ///< controles, console, mensagens
    std::vector<std::pair<std::string, ControlState>> controls_;
    f64 progress_ = -1.0;
    std::string progressText_;
    std::vector<ConsoleLine> console_;
    std::vector<Message> messages_;
    std::string openPanel_;
    std::string locale_;

    std::string storage_;                  ///< JSON do storage (objeto)
    bool storageLoaded_ = false;

    std::chrono::steady_clock::time_point born_ = std::chrono::steady_clock::now();
};

} // namespace aurea::plugins
