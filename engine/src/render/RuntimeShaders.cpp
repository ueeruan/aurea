#include "aurea/render/RuntimeShaders.hpp"
#include "aurea/core/Log.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>

#if defined(AUREA_RUNTIME_SHADERS)
#include "glslang/Public/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#include "SPIRV/GlslangToSpv.h"
#endif

namespace aurea::runtime_shaders {

// =============================================================================
// Compilador (glslang)
// =============================================================================
#if defined(AUREA_RUNTIME_SHADERS)

namespace {

std::mutex& compile_mutex() {
    static std::mutex m;
    return m;
}

/// O glslang conta clientes (o ncnn também é um): o motor entra uma vez e
/// nunca sai — as tabelas de símbolos ficam prontas para o próximo plugin.
void ensure_glslang() {
    static const bool initialized = glslang::InitializeProcess();
    (void)initialized;
}

/// "ERROR: rgb_wave.frag:12: 'x' : undeclared identifier" → "rgb_wave.frag:12: 'x' : undeclared identifier".
/// As linhas de resumo do glslang ("1 compilation errors.  No code generated.") saem.
void append_log(std::string& out, const char* raw) {
    if (!raw) return;
    std::string_view text(raw);
    while (!text.empty()) {
        const usize nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.empty()) continue;
        if (line.find("compilation errors.") != std::string_view::npos
            || line.find("No code generated") != std::string_view::npos
            || line.find("compilation terminated") != std::string_view::npos) continue;
        bool warning = false;
        if (line.starts_with("ERROR: ")) line.remove_prefix(7);
        else if (line.starts_with("WARNING: ")) { line.remove_prefix(9); warning = true; }
        if (warning) continue;   // aviso não recusa o plugin
        if (!out.empty()) out += '\n';
        out.append(line.data(), line.size());
    }
}

EShLanguage language_of(ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex: return EShLangVertex;
        case ShaderStage::Fragment: return EShLangFragment;
        case ShaderStage::Compute: return EShLangCompute;
    }
    return EShLangFragment;
}

} // namespace

bool compiler_available() noexcept { return true; }

bool compile_glsl(ShaderStage stage, std::span<const SourcePart> parts, std::vector<u32>& spirv,
                  std::string& log) noexcept {
    spirv.clear();
    log.clear();
    if (parts.empty()) { log = "shader vazio"; return false; }
    std::lock_guard<std::mutex> lock(compile_mutex());
    ensure_glslang();
    std::vector<const char*> strings;
    std::vector<int> lengths;
    std::vector<std::string> names;   // terminados em zero para o glslang
    std::vector<const char*> namePtrs;
    strings.reserve(parts.size());
    for (const SourcePart& p : parts) {
        strings.push_back(p.text.data());
        lengths.push_back(static_cast<int>(p.text.size()));
        names.emplace_back(p.name);
    }
    for (const std::string& n : names) namePtrs.push_back(n.c_str());

    const EShLanguage lang = language_of(stage);
    glslang::TShader shader(lang);
    shader.setStringsWithLengthsAndNames(strings.data(), lengths.data(), namePtrs.data(), static_cast<int>(strings.size()));
    shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setEntryPoint("main");
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
        append_log(log, shader.getInfoLog());
        if (log.empty()) log = "o shader nao compilou";
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        append_log(log, program.getInfoLog());
        if (log.empty()) log = "o shader nao ligou";
        return false;
    }
    const glslang::TIntermediate* intermediate = program.getIntermediate(lang);
    if (!intermediate) { log = "o shader nao gerou codigo"; return false; }
    std::vector<unsigned int> words;
    spv::SpvBuildLogger logger;
    glslang::SpvOptions options;
    options.generateDebugInfo = false;
    options.stripDebugInfo = true;
    options.disableOptimizer = true;
    options.validate = false;
    glslang::GlslangToSpv(*intermediate, words, &logger, &options);
    const std::string messagesOut = logger.getAllMessages();
    if (words.size() < 5) {
        log = messagesOut.empty() ? "SPIR-V vazio" : messagesOut;
        return false;
    }
    spirv.assign(words.begin(), words.end());
    return true;
}

#else

bool compiler_available() noexcept { return false; }

bool compile_glsl(ShaderStage, std::span<const SourcePart>, std::vector<u32>& spirv, std::string& log) noexcept {
    spirv.clear();
    log = "este build do motor nao compila shaders de plugin (AUREA_RUNTIME_SHADERS desligado)";
    return false;
}

#endif

// =============================================================================
// Registro global
// =============================================================================
namespace {

struct Registry {
    std::mutex mutex;
    std::unordered_map<u16, std::shared_ptr<const Source>> shaders;
    u32 next = kRuntimeShaderBase;
    std::atomic<u64> generation{1};
};

Registry& registry() {
    // Nunca destruído: bibliotecas de shader podem consultar no encerramento
    // estático do processo (o motor de teste morre depois do main).
    static Registry* r = new Registry();
    return *r;
}

} // namespace

ShaderId register_shader(ShaderStage stage, std::vector<u32> spirv, std::string name) noexcept {
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.next >= 0xFFFFu) {
        AUREA_LOG_ERROR("shaders de runtime: ids esgotados neste processo");
        return ShaderId::Count;
    }
    auto source = std::make_shared<Source>();
    source->stage = stage;
    source->spirv = std::move(spirv);
    source->name = std::move(name);
    const u16 id = static_cast<u16>(r.next++);
    r.shaders.emplace(id, std::move(source));
    r.generation.fetch_add(1, std::memory_order_acq_rel);
    return static_cast<ShaderId>(id);
}

void unregister_shader(ShaderId id) noexcept {
    if (!is_runtime_shader(id)) return;
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.shaders.erase(static_cast<u16>(id)) > 0) r.generation.fetch_add(1, std::memory_order_acq_rel);
}

std::shared_ptr<const Source> find(ShaderId id) noexcept {
    if (!is_runtime_shader(id)) return nullptr;
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    const auto it = r.shaders.find(static_cast<u16>(id));
    return it == r.shaders.end() ? nullptr : it->second;
}

u64 generation() noexcept { return registry().generation.load(std::memory_order_acquire); }

u32 registered_count() noexcept {
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    return static_cast<u32>(r.shaders.size());
}

} // namespace aurea::runtime_shaders
