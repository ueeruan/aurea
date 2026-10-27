// =============================================================================
//  Aurea / plugins / PluginManager.hpp
//
//  Os plugins instalados: a pasta, a instalação (conferir, extrair, compilar,
//  testar, registrar), a lista e a remoção.
//
//  Pasta: `<dir>/<id>/` com o plugin.json e os arquivos do pacote (Android:
//  files/plugins; iOS: Documents/Plugins). Na abertura do motor todo plugin da
//  pasta é lido e compilado de novo (só CPU: glslang → SPIR-V); o módulo de GPU
//  sai sob demanda, quando um projeto usa o efeito.
//
//  Instalar é TRANSACIONAL: o pacote é extraído numa pasta temporária, lido,
//  compilado, compilado de novo pelo backend ativo e desenhado uma vez em
//  256×256 com os padrões; só então ele toma o lugar da versão anterior (mesmo
//  id, versão igual ou mais nova) e entra no registro de efeitos. Qualquer
//  recusa deixa a pasta e o catálogo exatamente como estavam.
//
//  Thread-safe: instalar/remover/listar entre si sob um mutex próprio; o
//  registro de efeitos e o de shaders aceitam leitores em qualquer thread.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/plugins/Plugin.hpp"
#include "aurea/render/RuntimeShaders.hpp"

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace aurea::plugins {

/// O que a instalação pede ao motor (a GPU é dele). Ganchos nulos = sem GPU:
/// o plugin instala só com a compilação do glslang (projeto sem preview).
struct InstallHooks {
    /// Compila o SPIR-V no backend ativo (GPUBackend::create_runtime_shader) e
    /// solta o módulo. Devolve vazio quando o backend aceitou; senão a mensagem.
    std::function<std::string(const runtime_shaders::Source& shader)> compileOnBackend;
    /// Desenha `typeId` sozinho (com os padrões) a partir de `registry` em
    /// kTestRenderSize², mede o quadro já com os pipelines prontos e devolve o
    /// tempo em `ms`. Falha = `error` preenchido.
    std::function<bool(const EffectRegistry& registry, EffectTypeId typeId, f32& ms, std::string& error)> testRender;
};

class PluginManager {
public:
    explicit PluginManager(EffectRegistry& registry) noexcept : registry_(registry) {}
    ~PluginManager();
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    /// A pasta dos plugins. Vazio = sem plugins (instalar recusa).
    void set_directory(std::string directory);
    [[nodiscard]] std::string directory() const;

    /// Lê e registra todo plugin instalado na pasta (abertura do motor). Os que
    /// falham ficam na lista com `ok = false` e o motivo. Devolve os registrados.
    u32 load_all() noexcept;

    /// Instala um `.aureaplugin` (ZIP) ou um `.fs` (ISF). Devolve o id; na
    /// recusa, o `Status` traz o código e uma mensagem para a pessoa em
    /// `detail()` (válida enquanto o gerenciador viver).
    [[nodiscard]] Result<std::string> install(const std::string& path, const InstallHooks& hooks) noexcept;

    /// Desinstala: o efeito sai do catálogo (projetos guardam a instância e os
    /// valores, como efeito desconhecido), os shaders saem do registro e a
    /// pasta é apagada. Falso = não havia plugin com esse id.
    bool remove(std::string_view id) noexcept;

    [[nodiscard]] std::vector<PluginInfo> list() const;
    [[nodiscard]] std::string list_json() const { return to_json(list()); }

    /// Tira tudo dos registros (encerramento do motor). A pasta fica.
    void shutdown() noexcept;

    // --- 0.3.3 plugins: interface, script e permissões -----------------------
    /// Lê o pacote SEM instalar (a folha de consentimento mostra o que ele
    /// pede). JSON: {ok, error, id, version, name, description, author, kind,
    /// permissions{network[], clipboard, files}, panels, actions, hasScript,
    /// installedVersion}. Nada muda na pasta.
    [[nodiscard]] std::string inspect_json(const std::string& path) noexcept;
    /// Liga/desliga a interface e o script do plugin (gravado em `.estado.json`).
    bool set_enabled(std::string_view id, bool on) noexcept;
    /// Permissões concedidas pela pessoa (kPerm*; só as que o plugin pede valem).
    bool set_granted(std::string_view id, u32 mask) noexcept;
    /// O plugin instalado `id` (cópia). Falso = não há.
    [[nodiscard]] bool find(std::string_view id, PluginInfo& out) const;
    /// `<pasta>/.dados`: dados dos plugins (storage, quadros) fora das pastas
    /// deles — reinstalar mantém; remover apaga.
    [[nodiscard]] std::string data_directory() const;

private:
    struct Installed {
        PluginInfo info;
        std::vector<ShaderId> shaders;
        bool registered = false;
    };
    struct SavedState { bool enabled = true; u32 granted = 0; };
    void load_state_locked();
    void save_state_locked() noexcept;
    void apply_state_locked(PluginInfo& info);
    std::vector<std::pair<std::string, SavedState>> state_;
    u64 generation_ = 0;
    [[nodiscard]] const char* intern(std::string message);
    void unregister_locked(Installed& p) noexcept;

    EffectRegistry& registry_;
    mutable std::mutex mutex_;
    std::string directory_;
    std::vector<Installed> plugins_;
    std::set<std::string> messages_;   ///< textos que `Status::detail` aponta
    u32 stagingCounter_ = 0;
};

} // namespace aurea::plugins
