#include "aurea/plugins/PluginManager.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/ProjectPackage.hpp"
#include "PluginInternal.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace aurea::plugins {

namespace fs = std::filesystem;

namespace {

std::string utf8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path path_of(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

bool read_text_file(const fs::path& path, usize maxBytes, std::string& out) {
    std::vector<u8> bytes;
    if (!fileio::read_all(utf8(path), bytes, maxBytes)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

/// Caminho de arquivo DENTRO do pacote: relativo, sem "..", sem unidade.
bool safe_relative(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    if (name.front() == '/' || name.front() == '\\' || name.find(':') != std::string_view::npos) return false;
    usize start = 0;
    while (start <= name.size()) {
        usize end = name.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = name.size();
        const std::string_view part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

/// Um plugin pronto para registrar: lido, conferido e compilado (CPU).
struct Prepared {
    Manifest manifest;
    std::vector<std::vector<u32>> spirv;
    std::vector<CompiledStackStep> stack;
};

struct Failure {
    Errc code = Errc::InvalidArgument;
    std::string errorCode;
    std::string message;
};

bool prepare(const fs::path& root, const EffectRegistry& registry, Prepared& out, Failure& fail) {
    std::string text;
    if (!read_text_file(root / "plugin.json", kMaxManifestBytes + 4, text)) {
        fail = {Errc::UnsupportedFormat, "manifest", "plugin.json ausente ou ilegivel no pacote"};
        return false;
    }
    std::string error;
    if (!parse_manifest(text, out.manifest, error)) {
        fail = {Errc::InvalidArgument, "manifest", error};
        return false;
    }
    const Manifest& m = out.manifest;
    if (m.kind == Kind::Shader) {
        if (!runtime_shaders::compiler_available()) {
            fail = {Errc::NotSupported, "unsupported", "este build do app nao compila shaders de plugin"};
            return false;
        }
        for (u32 p = 0; p < m.passes.size(); ++p) {
            const std::string& file = m.passes[p].shader;
            if (!safe_relative(file)) {
                fail = {Errc::InvalidArgument, "manifest", "passe " + std::to_string(p + 1) + ": caminho de shader invalido \"" + file + "\""};
                return false;
            }
            const fs::path path = root / path_of(file);
            std::error_code ec;
            if (!fs::is_regular_file(path, ec)) {
                fail = {Errc::NotFound, "shader_source", "passe " + std::to_string(p + 1) + ": arquivo de shader ausente: " + file};
                return false;
            }
            const auto size = fs::file_size(path, ec);
            if (ec || size > kMaxShaderBytes) {
                fail = {Errc::BudgetExceeded, "shader_source", "passe " + std::to_string(p + 1) + ": " + file + " maior que 64 KB"};
                return false;
            }
            std::string source;
            if (!read_text_file(path, kMaxShaderBytes, source)) {
                fail = {Errc::IoError, "shader_source", "passe " + std::to_string(p + 1) + ": nao foi possivel ler " + file};
                return false;
            }
            std::vector<u32> spirv;
            if (!compile_pass(m, p, source, spirv, error)) {
                fail = {Errc::ShaderCompileFailed, "shader_compile", error};
                return false;
            }
            out.spirv.push_back(std::move(spirv));
        }
    } else {
        if (!compile_stack(m, registry, out.stack, error)) {
            fail = {Errc::InvalidArgument, "stack", error};
            return false;
        }
    }
    return true;
}

PluginInfo info_of(const Manifest& m, const fs::path& dir) {
    PluginInfo i;
    i.id = m.id;
    i.version = m.version;
    i.author = m.author;
    i.kind = m.kind;
    i.scene3d = m.scene3d;
    i.category = m.category;
    i.names = m.name;
    i.descriptions = m.description;
    i.effectKey = effect_key(m.id);
    i.effectTypeId = effect_type_id(i.effectKey);
    i.directory = utf8(dir);
    i.params = m.params;
    i.passCount = static_cast<u32>(m.passes.size());
    return i;
}

/// O efeito do plugin (os ids de shader já registrados, se for "shader").
std::unique_ptr<Effect> make_effect(const Prepared& p, const std::vector<ShaderId>& shaders) {
    if (p.manifest.kind == Kind::Shader) return make_shader_effect(p.manifest, shaders);
    return make_stack_effect(p.manifest, p.stack);
}

std::vector<ShaderId> register_shaders(const Prepared& p) {
    std::vector<ShaderId> ids;
    for (usize k = 0; k < p.spirv.size(); ++k) {
        const ShaderId id = runtime_shaders::register_shader(
            ShaderStage::Fragment, p.spirv[k], "plugin " + p.manifest.id + " passe " + std::to_string(k + 1));
        if (id == ShaderId::Count) {
            for (const ShaderId done : ids) runtime_shaders::unregister_shader(done);
            return {};
        }
        ids.push_back(id);
    }
    return ids;
}

std::string package_error(Errc code) {
    switch (code) {
        case Errc::UnsupportedFormat: return "o arquivo nao e um pacote .aureaplugin (um ZIP com plugin.json)";
        case Errc::CorruptData: return "pacote corrompido (ZIP invalido, nome de arquivo inseguro ou CRC errado)";
        case Errc::BudgetExceeded: return "pacote grande demais";
        case Errc::IoError: return "nao foi possivel ler o pacote";
        case Errc::StorageFull: return "sem espaco para instalar o plugin";
        default: return std::string("nao foi possivel abrir o pacote: ") + std::string(to_string(code));
    }
}

void remove_tree(const fs::path& p) {
    std::error_code ec;
    fs::remove_all(p, ec);
}

} // namespace

PluginManager::~PluginManager() { shutdown(); }

void PluginManager::set_directory(std::string directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    directory_ = std::move(directory);
}

std::string PluginManager::directory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return directory_;
}

const char* PluginManager::intern(std::string message) {
    // Chamado sob mutex_. O conjunto nunca apaga: o ponteiro vale até o fim.
    return messages_.insert(std::move(message)).first->c_str();
}

void PluginManager::unregister_locked(Installed& p) noexcept {
    if (p.registered) (void)registry_.retire(p.info.effectTypeId);
    p.registered = false;
    for (const ShaderId id : p.shaders) runtime_shaders::unregister_shader(id);
    p.shaders.clear();
}

void PluginManager::shutdown() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Installed& p : plugins_) unregister_locked(p);
    plugins_.clear();
}

u32 PluginManager::load_all() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Installed& p : plugins_) unregister_locked(p);
    plugins_.clear();
    if (directory_.empty()) return 0;
    const fs::path root = path_of(directory_);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return 0;
    std::vector<fs::path> dirs;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        const std::string name = utf8(it->path().filename());
        if (name.empty()) continue;
        if (name.front() == '.') {
            // Instalação interrompida (app fechado no meio): nada dela vale.
            if (name.starts_with(".instalando-")) remove_tree(it->path());
            continue;
        }
        dirs.push_back(it->path());
    }
    std::sort(dirs.begin(), dirs.end());
    u32 loaded = 0;
    for (const fs::path& dir : dirs) {
        Prepared prepared;
        Failure failure;
        Installed entry;
        if (!prepare(dir, registry_, prepared, failure)) {
            entry.info.id = utf8(dir.filename());
            entry.info.directory = utf8(dir);
            entry.info.effectKey = effect_key(entry.info.id);
            entry.info.effectTypeId = effect_type_id(entry.info.effectKey);
            if (!prepared.manifest.id.empty()) entry.info = info_of(prepared.manifest, dir);
            entry.info.ok = false;
            entry.info.errorCode = failure.errorCode;
            entry.info.error = failure.message;
            AUREA_LOG_WARN("plugin '%s' nao carregou: %s", entry.info.id.c_str(), failure.message.c_str());
            plugins_.push_back(std::move(entry));
            continue;
        }
        entry.info = info_of(prepared.manifest, dir);
        const bool duplicate = std::any_of(plugins_.begin(), plugins_.end(),
                                           [&](const Installed& p) { return p.info.id == entry.info.id && p.registered; });
        if (duplicate) {
            entry.info.ok = false;
            entry.info.errorCode = "duplicate";
            entry.info.error = "outra pasta ja instalou o plugin \"" + entry.info.id + "\"";
            plugins_.push_back(std::move(entry));
            continue;
        }
        entry.shaders = register_shaders(prepared);
        if (entry.shaders.size() != prepared.spirv.size()) {
            entry.info.ok = false;
            entry.info.errorCode = "shader_compile";
            entry.info.error = "sem espaco para os shaders do plugin nesta sessao";
            plugins_.push_back(std::move(entry));
            continue;
        }
        const Status added = registry_.add(make_effect(prepared, entry.shaders));
        if (!added.ok()) {
            for (const ShaderId id : entry.shaders) runtime_shaders::unregister_shader(id);
            entry.shaders.clear();
            entry.info.ok = false;
            entry.info.errorCode = "registry";
            entry.info.error = added.code() == Errc::AlreadyExists ? "a chave do efeito ja existe no catalogo"
                                                                   : "o catalogo de efeitos esta cheio";
            plugins_.push_back(std::move(entry));
            continue;
        }
        entry.registered = true;
        entry.info.ok = true;
        ++loaded;
        plugins_.push_back(std::move(entry));
    }
    if (!plugins_.empty()) AUREA_LOG_INFO("plugins: %u de %zu carregados de %s", loaded, plugins_.size(), directory_.c_str());
    return loaded;
}

Result<std::string> PluginManager::install(const std::string& path, const InstallHooks& hooks) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto reject = [&](Errc code, std::string message) -> Result<std::string> {
        AUREA_LOG_WARN("plugin recusado: %s", message.c_str());
        return Status{code, intern(std::move(message))};
    };
    if (directory_.empty()) return reject(Errc::InvalidState, "pasta de plugins nao configurada");
    const fs::path source = path_of(path);
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) return reject(Errc::NotFound, "arquivo nao encontrado: " + path);
    const auto bytes = fs::file_size(source, ec);
    if (ec) return reject(Errc::IoError, "nao foi possivel ler o arquivo");
    if (bytes > kMaxPackageBytes) return reject(Errc::BudgetExceeded, "pacote maior que 16 MB");

    const fs::path root = path_of(directory_);
    fs::create_directories(root, ec);
    if (ec) return reject(Errc::IoError, "nao foi possivel criar a pasta de plugins");
    const fs::path staging = root / path_of(".instalando-" + std::to_string(monotonic_ns()) + "-" + std::to_string(++stagingCounter_));
    remove_tree(staging);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() { if (!dir.empty()) remove_tree(dir); }
    } cleanup{staging};

    // ISF (.fs) ou pacote ZIP.
    std::string ext = utf8(source.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    fs::path pluginRoot = staging;
    if (ext == ".fs" || ext == ".isf") {
        std::string text;
        if (!read_text_file(source, kMaxShaderBytes, text)) return reject(Errc::BudgetExceeded, "ISF: arquivo ilegivel ou maior que 64 KB");
        Manifest manifest;
        std::string shaderFile, shaderText, error;
        if (!isf_to_plugin(utf8(source.filename()), text, manifest, shaderFile, shaderText, error))
            return reject(Errc::UnsupportedFormat, error);
        fs::create_directories(staging, ec);
        if (ec) return reject(Errc::IoError, "nao foi possivel criar a pasta temporaria");
        const std::string json = manifest_json(manifest);
        if (!fileio::write_atomic(utf8(staging / "plugin.json"), json.data(), json.size()).ok()
            || !fileio::write_atomic(utf8(staging / path_of(shaderFile)), shaderText.data(), shaderText.size()).ok())
            return reject(Errc::IoError, "nao foi possivel gravar o plugin convertido");
    } else {
        std::vector<std::string> files;
        const Status extracted = package::extract_model_archive(path, utf8(staging), files);
        if (!extracted.ok()) return reject(extracted.code(), package_error(extracted.code()));
        // ZIP feito da PASTA (plugin.json dentro de uma subpasta única): aceito.
        if (!fs::is_regular_file(staging / "plugin.json", ec)) {
            fs::path only;
            u32 dirs = 0;
            for (fs::directory_iterator it(staging, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                if (it->is_directory(e2)) { only = it->path(); ++dirs; }
            }
            if (dirs == 1 && fs::is_regular_file(only / "plugin.json", ec)) pluginRoot = only;
            else return reject(Errc::UnsupportedFormat, "plugin.json ausente no pacote");
        }
    }

    Prepared prepared;
    Failure failure;
    if (!prepare(pluginRoot, registry_, prepared, failure)) return reject(failure.code, failure.message);
    const Manifest& m = prepared.manifest;

    // Mesma identidade já instalada: só versão igual ou mais nova a substitui.
    auto existing = std::find_if(plugins_.begin(), plugins_.end(), [&](const Installed& p) { return p.info.id == m.id; });
    if (existing != plugins_.end() && existing->info.ok && compare_versions(m.version, existing->info.version) < 0)
        return reject(Errc::AlreadyExists, "ja esta instalada a versao " + existing->info.version + " de \"" + m.id
                                           + "\", mais nova que " + m.version);

    // O backend ativo compila cada passe (tradução para ESSL/MSL + driver).
    if (hooks.compileOnBackend) {
        for (usize k = 0; k < prepared.spirv.size(); ++k) {
            runtime_shaders::Source s;
            s.stage = ShaderStage::Fragment;
            s.spirv = prepared.spirv[k];
            s.name = "plugin " + m.id + " passe " + std::to_string(k + 1);
            const std::string error = hooks.compileOnBackend(s);
            if (!error.empty())
                return reject(Errc::ShaderCompileFailed, m.passes[k].shader + ": a GPU recusou o shader: " + error);
        }
    }

    std::vector<ShaderId> shaders = register_shaders(prepared);
    if (shaders.size() != prepared.spirv.size()) return reject(Errc::BudgetExceeded, "sem espaco para os shaders do plugin nesta sessao");
    auto dropShaders = [&] { for (const ShaderId id : shaders) runtime_shaders::unregister_shader(id); };

    // Quadro de teste: o efeito sozinho, com os padrões, em 256x256.
    f32 testMs = -1.0f;
    if (hooks.testRender) {
        EffectRegistry probe;
        if (!probe.add(make_effect(prepared, shaders)).ok()) { dropShaders(); return reject(Errc::InvalidState, "efeito de teste nao registrou"); }
        std::string error;
        if (!hooks.testRender(probe, effect_type_id(effect_key(m.id)), testMs, error)) {
            dropShaders();
            return reject(Errc::PipelineCompileFailed, "o quadro de teste falhou: " + error);
        }
        if (testMs > kTestRenderBudgetMs) {
            dropShaders();
            char buf[160];
            std::snprintf(buf, sizeof(buf), "o quadro de teste levou %.0f ms (limite %.0f ms em %ux%u): "
                          "simplifique o shader (lacos, amostras por pixel, passes)",
                          static_cast<double>(testMs), static_cast<double>(kTestRenderBudgetMs), kTestRenderSize, kTestRenderSize);
            return reject(Errc::Timeout, buf);
        }
    }

    // Toma o lugar da versão anterior.
    const fs::path final = root / path_of(m.id);
    if (existing != plugins_.end()) {
        unregister_locked(*existing);
        plugins_.erase(existing);
    }
    remove_tree(final);
    fs::rename(pluginRoot, final, ec);
    if (ec) {
        dropShaders();
        return reject(Errc::IoError, "nao foi possivel mover o plugin para a pasta final");
    }
    Installed entry;
    entry.info = info_of(m, final);
    entry.info.testRenderMs = testMs;
    entry.shaders = std::move(shaders);
    const Status added = registry_.add(make_effect(prepared, entry.shaders));
    if (!added.ok()) {
        for (const ShaderId id : entry.shaders) runtime_shaders::unregister_shader(id);
        entry.shaders.clear();
        entry.info.ok = false;
        entry.info.errorCode = "registry";
        entry.info.error = "o catalogo de efeitos recusou o plugin";
        plugins_.push_back(std::move(entry));
        return reject(Errc::AlreadyExists, "o catalogo de efeitos recusou o plugin");
    }
    entry.registered = true;
    entry.info.ok = true;
    AUREA_LOG_INFO("plugin instalado: %s %s (%s, %zu passe(s), teste %.1f ms)", m.id.c_str(), m.version.c_str(),
                   kind_name(m.kind), m.passes.size(), static_cast<double>(testMs));
    plugins_.push_back(std::move(entry));
    return m.id;
}

bool PluginManager::remove(std::string_view id) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(plugins_.begin(), plugins_.end(), [&](const Installed& p) { return p.info.id == id; });
    if (it == plugins_.end()) return false;
    unregister_locked(*it);
    if (!it->info.directory.empty()) {
        // Só apaga dentro da pasta de plugins.
        const fs::path dir = path_of(it->info.directory);
        const fs::path root = path_of(directory_);
        std::error_code ec;
        const fs::path rel = fs::relative(dir, root, ec);
        if (!ec && !rel.empty() && utf8(rel).find("..") == std::string::npos) remove_tree(dir);
    }
    plugins_.erase(it);
    return true;
}

std::vector<PluginInfo> PluginManager::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PluginInfo> out;
    out.reserve(plugins_.size());
    for (const Installed& p : plugins_) out.push_back(p.info);
    std::sort(out.begin(), out.end(), [](const PluginInfo& a, const PluginInfo& b) { return a.id < b.id; });
    return out;
}

} // namespace aurea::plugins
