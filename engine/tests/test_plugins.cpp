// =============================================================================
//  Plugins de efeito (docs/plugins/README.md).
//
//  CPU: plugin.json, expressões das pilhas, conversão do ISF, mensagens de erro
//  com arquivo:linha, tradução dos shaders de plugin para ESSL e MSL (o mesmo
//  tradutor dos backends GLES e Metal) e as recusas da instalação.
//
//  GPU (Vulkan do host): os exemplos instalam e desenham EXATAMENTE o que
//  prometem (pixel a pixel, não "mudou"), parâmetros e keyframes valem, o
//  passe intermediário roda na escala pedida, o plugin de pilha é igual aos
//  embutidos aplicados à mão, e o projeto guarda o efeito do plugin: salva,
//  reabre, reabre sem o plugin (efeito desconhecido, valores intactos, sem
//  queda) e volta ao reinstalar.
// =============================================================================
#include "TestFramework.hpp"

#include "../src/plugins/PluginInternal.hpp"
#include "ShaderTranslate.hpp"
#include "aurea/Engine.hpp"
#include "aurea/plugins/PluginManager.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/render/RuntimeShaders.hpp"

#if defined(AUREA_TEST_VULKAN)
#include "VulkanBackend.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace aurea;
namespace fs = std::filesystem;

namespace {

std::string examples() { return AUREA_PLUGIN_EXAMPLES_DIR; }

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

fs::path fresh_dir(const char* name) {
    static std::atomic<u32> counter{0};
    const fs::path p = fs::temp_directory_path() / ("aurea-plugins-" + std::string(name) + "-"
                                                    + std::to_string(++counter) + "-" + std::to_string(std::time(nullptr)));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

std::string str(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

// ZIP sem compressão, só para montar pacotes de teste.
u32 crc32(const std::string& data) {
    static u32 table[256];
    static bool ready = false;
    if (!ready) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    u32 c = 0xFFFFFFFFu;
    for (const unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put16(std::string& s, u32 v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>((v >> 8) & 0xFF); }
void put32(std::string& s, u32 v) { put16(s, v & 0xFFFF); put16(s, v >> 16); }

void write_zip(const fs::path& out, const std::vector<std::pair<std::string, std::string>>& files) {
    std::string zip, central;
    for (const auto& [name, data] : files) {
        const u32 offset = static_cast<u32>(zip.size());
        const u32 crc = crc32(data);
        put32(zip, 0x04034b50); put16(zip, 20); put16(zip, 0); put16(zip, 0); put16(zip, 0); put16(zip, 0x21);
        put32(zip, crc); put32(zip, static_cast<u32>(data.size())); put32(zip, static_cast<u32>(data.size()));
        put16(zip, static_cast<u32>(name.size())); put16(zip, 0);
        zip += name;
        zip += data;
        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0x21); put32(central, crc); put32(central, static_cast<u32>(data.size()));
        put32(central, static_cast<u32>(data.size())); put16(central, static_cast<u32>(name.size()));
        put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0); put32(central, 0);
        put32(central, offset);
        central += name;
    }
    const u32 cdOffset = static_cast<u32>(zip.size());
    zip += central;
    put32(zip, 0x06054b50); put16(zip, 0); put16(zip, 0); put16(zip, static_cast<u32>(files.size()));
    put16(zip, static_cast<u32>(files.size())); put32(zip, static_cast<u32>(central.size())); put32(zip, cdOffset);
    put16(zip, 0);
    std::ofstream f(out, std::ios::binary);
    f.write(zip.data(), static_cast<std::streamsize>(zip.size()));
}

/// plugin.json mínimo de um passe, com o shader dado.
std::string one_pass_manifest(const std::string& id, const std::string& version = "1.0.0",
                              const std::string& file = "main.frag", const std::string& params = "") {
    return "{\"aureaPlugin\":1,\"id\":\"" + id + "\",\"version\":\"" + version + "\",\"name\":\"Teste\","
           "\"kind\":\"shader\",\"params\":[" + params + "],\"passes\":[{\"shader\":\"" + file + "\"}]}";
}

const char* kIdentityShader = "vec4 aurea_main(vec2 uv) { return texture(aurea_source, uv); }\n";

/// Gerenciador com os embutidos e sem GPU (a instalação para no glslang).
struct CpuManager {
    EffectRegistry effects;
    plugins::PluginManager manager{effects};
    fs::path dir = fresh_dir("cpu");
    CpuManager() {
        register_builtin_effects(effects);
        manager.set_directory(str(dir));
    }
    ~CpuManager() {
        manager.shutdown();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

std::string detail_of(const Result<std::string>& r) { return std::string(r.status().detail()); }

} // namespace

// =============================================================================
// CPU
// =============================================================================
AUREA_TEST(Plugins, ExpressionEvaluatorIsClosedAndCorrect) {
    using plugins::Expr;
    const std::vector<plugins::ExprSymbol> syms{{"amount", 0, 0}, {"p.x", 1, 0}, {"p.y", 1, 1}};
    ParamValue vals[2] = {ParamValue::scalar(0.5f), ParamValue::vec2(2.0f, 3.0f)};
    auto eval = [&](const char* text) {
        Expr e;
        std::string err;
        const bool ok = e.compile(text, syms, err);
        AUREA_CHECK_MSG(ok, err.c_str());
        return ok ? e.evaluate(vals, 2) : -999.0f;
    };
    AUREA_CHECK_NEAR(eval("amount * 40 + p.y"), 23.0f, 1e-5f);
    AUREA_CHECK_NEAR(eval("clamp(amount * 4, 0, 1)"), 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(eval("max(1, p.x, 3)"), 3.0f, 1e-6f);
    AUREA_CHECK_NEAR(eval("2 ^ 3 ^ 2"), 512.0f, 1e-3f);
    AUREA_CHECK_NEAR(eval("-amount"), -0.5f, 1e-6f);
    AUREA_CHECK_NEAR(eval("mix(0, 10, amount)"), 5.0f, 1e-6f);
    AUREA_CHECK_NEAR(eval("10 % 3"), 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(eval("1 / 0"), 0.0f, 1e-6f);   // divisão por zero não vira inf
    AUREA_CHECK_NEAR(eval("sin(pi / 2) * (p.x + 1)"), 3.0f, 1e-5f);
    AUREA_CHECK_NEAR(eval("smoothstep(0, 1, amount)"), 0.5f, 1e-6f);
    AUREA_CHECK_NEAR(eval("1.5e1"), 15.0f, 1e-6f);
    for (const char* bad : {"amount +", "foo * 2", "sin(1, 2)", "(1 + 2", "amount.q", "1 $ 2", ""}) {
        Expr e;
        std::string err;
        AUREA_CHECK_MSG(!e.compile(bad, syms, err) && !err.empty(), bad);
    }
}

AUREA_TEST(Plugins, ManifestValidationNamesFieldAndReason) {
    plugins::Manifest m;
    std::string err;
    AUREA_CHECK(plugins::parse_manifest(one_pass_manifest("com.exemplo.ok"), m, err));
    AUREA_CHECK_EQ(m.passes.size(), static_cast<usize>(1));
    struct Case { std::string json; const char* expect; };
    std::string nine = "{\"aureaPlugin\":1,\"id\":\"com.x.nine\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader\",\"passes\":[";
    for (int i = 0; i < 9; ++i) nine += std::string(i ? "," : "") + "{\"shader\":\"p" + std::to_string(i) + ".frag\""
                                    + (i < 8 ? ",\"target\":\"t" + std::to_string(i) + "\"" : "") + "}";
    nine += "]}";
    std::string many = "{\"aureaPlugin\":1,\"id\":\"com.x.many\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader\",\"params\":[";
    for (int i = 0; i < 17; ++i) many += std::string(i ? "," : "") + "{\"id\":\"p" + std::to_string(i) + "\",\"type\":\"float\"}";
    many += "],\"passes\":[{\"shader\":\"a.frag\"}]}";
    const Case cases[] = {
        {one_pass_manifest("AB"), "\"id\" invalido"},
        {one_pass_manifest("Com.Upper.Case"), "\"id\" invalido"},
        {one_pass_manifest("com.x.ver", "1.0"), "\"version\""},
        {nine, "9 passes (limite 8)"},
        {many, "17 parametros (limite 16)"},
        {one_pass_manifest("com.x.res", "1.0.0", "a.frag", "{\"id\":\"mix\",\"type\":\"float\"}"), "id invalido"},
        {one_pass_manifest("com.x.enum", "1.0.0", "a.frag", "{\"id\":\"mode\",\"type\":\"enum\"}"), "options"},
        {one_pass_manifest("com.x.type", "1.0.0", "a.frag", "{\"id\":\"q\",\"type\":\"vec9\"}"), "tipo desconhecido"},
        {"{\"aureaPlugin\":2,\"id\":\"com.x.fmt\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader\"}", "versao 1 do formato"},
        {"{\"aureaPlugin\":1,\"id\":\"com.x.kind\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"compute\"}", "\"kind\""},
        {"{\"aureaPlugin\":1,\"id\":\"com.x.tgt\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader\","
         "\"passes\":[{\"shader\":\"a.frag\",\"target\":\"x\"}]}", "ultimo passe"},
        {"{\"aureaPlugin\":1,\"id\":\"com.x.in\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader\","
         "\"passes\":[{\"shader\":\"a.frag\",\"inputs\":{\"blur\":\"nada\"}}]}", "nao e \"source\""},
        {"{ isto nao e json", "plugin.json invalido"},
    };
    for (const Case& c : cases) {
        plugins::Manifest out;
        std::string why;
        const bool ok = plugins::parse_manifest(c.json, out, why);
        AUREA_CHECK_MSG(!ok, c.expect);
        if (why.find(c.expect) == std::string::npos) std::printf("\n    esperado '%s', veio '%s'", c.expect, why.c_str());
        AUREA_CHECK(why.find(c.expect) != std::string::npos);
    }
    // Cor declarada em sRGB vira linear; texto por idioma com fallback en → pt-BR.
    plugins::Manifest c;
    AUREA_CHECK(plugins::parse_manifest(
        "{\"aureaPlugin\":1,\"id\":\"com.x.color\",\"version\":\"2.10.3\",\"name\":{\"pt-BR\":\"Cor\",\"en\":\"Colour\"},"
        "\"kind\":\"shader\",\"params\":[{\"id\":\"tint\",\"type\":\"color\",\"default\":\"#808080\"}],"
        "\"passes\":[{\"shader\":\"a.frag\"}]}", c, err));
    AUREA_CHECK_NEAR(c.params[0].def[0], plugins::srgb_to_linear(128.0f / 255.0f), 1e-6f);
    AUREA_CHECK_EQ(plugins::localized(c.name, "ru"), std::string("Colour"));
    AUREA_CHECK_EQ(plugins::localized(c.name, "pt-BR"), std::string("Cor"));
    AUREA_CHECK(plugins::compare_versions("2.10.3", "2.9.9") > 0);
    AUREA_CHECK(plugins::compare_versions("1.0.0", "1.0.0") == 0);
}

AUREA_TEST(Plugins, IsfConvertsToSingleSrgbPassAndCompiles) {
    const std::string text = read_file(fs::path(examples()) / "pulse-vignette.fs");
    AUREA_CHECK(!text.empty());
    plugins::Manifest m;
    std::string file, shader, err;
    AUREA_CHECK_MSG(plugins::isf_to_plugin("pulse-vignette.fs", text, m, file, shader, err), err.c_str());
    AUREA_CHECK_EQ(m.id, std::string("isf.pulse-vignette"));
    AUREA_CHECK(m.colorSpace == plugins::ColorSpace::Srgb);
    AUREA_CHECK_EQ(m.passes.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(m.params.size(), static_cast<usize>(5));
    if (m.params.size() == 5) {
        AUREA_CHECK(m.params[0].kind == plugins::ParamKind::Float && m.params[0].id == "strength");
        AUREA_CHECK(m.params[2].kind == plugins::ParamKind::Point);
        AUREA_CHECK_NEAR(m.params[2].def[1], 0.5f, 1e-6f);   // y invertido: 0,5 continua 0,5
        AUREA_CHECK(m.params[3].kind == plugins::ParamKind::Color);
        AUREA_CHECK(m.params[4].kind == plugins::ParamKind::Bool);
    }
    std::vector<u32> spirv;
    AUREA_CHECK_MSG(plugins::compile_pass(m, 0, shader, spirv, err), err.c_str());
    AUREA_CHECK(spirv.size() > 100);
    // O que o motor não reproduz fielmente é recusado com motivo.
    const std::string multi = "/*{\"INPUTS\":[],\"PASSES\":[{\"TARGET\":\"a\"},{}]}*/ void main(){gl_FragColor=vec4(1.0);}";
    AUREA_CHECK(!plugins::isf_to_plugin("multi.fs", multi, m, file, shader, err) && err.find("PASSES") != std::string::npos);
    const std::string audio = "/*{\"INPUTS\":[{\"NAME\":\"snd\",\"TYPE\":\"audio\"}]}*/ void main(){gl_FragColor=vec4(1.0);}";
    AUREA_CHECK(!plugins::isf_to_plugin("audio.fs", audio, m, file, shader, err) && err.find("audio") != std::string::npos);
    AUREA_CHECK(!plugins::isf_to_plugin("x.fs", "void main(){}", m, file, shader, err) && err.find("cabecalho") != std::string::npos);
}

AUREA_TEST(Plugins, CompileErrorReportsFileAndLine) {
    plugins::Manifest m;
    std::string err;
    AUREA_CHECK(plugins::parse_manifest(one_pass_manifest("com.x.erro", "1.0.0", "quebrado.frag"), m, err));
    const char* source = "vec4 aurea_main(vec2 uv) {\n"
                         "    float x = 1.0;\n"
                         "    return vec4(y);\n"
                         "}\n";
    std::vector<u32> spirv;
    AUREA_CHECK(!plugins::compile_pass(m, 0, source, spirv, err));
    std::printf("\n    erro: %s", err.c_str());
    AUREA_CHECK_MSG(err.find("quebrado.frag:3:") != std::string::npos, "o erro deve apontar quebrado.frag:3");
    AUREA_CHECK(err.find("aurea_header") == std::string::npos);
    // ISF: a linha é a do .fs original (o #line do conversor).
    const std::string isf = "/*{\n\"INPUTS\":[{\"NAME\":\"inputImage\",\"TYPE\":\"image\"}]\n}*/\n"
                            "void main() {\n"
                            "    gl_FragColor = IMG_NORM_PIXEL(inputImage, isf_FragNormCoord) * naoExiste;\n"
                            "}\n";
    std::string file, shader;
    AUREA_CHECK(plugins::isf_to_plugin("linha.fs", isf, m, file, shader, err));
    AUREA_CHECK(!plugins::compile_pass(m, 0, shader, spirv, err));
    std::printf("\n    erro ISF: %s", err.c_str());
    AUREA_CHECK_MSG(err.find("linha.fs:5:") != std::string::npos, "o erro do ISF deve apontar linha.fs:5");
}

AUREA_TEST(Plugins, ShadersTranslateForGlesAndMetal) {
    // O mesmo tradutor dos backends GLES (ESSL 3.10) e Metal (MSL 2.1).
    for (const char* folder : {"rgb-wave", "soft-glow", "target-3d"}) {
        const fs::path root = fs::path(examples()) / folder;
        plugins::Manifest m;
        std::string err;
        AUREA_CHECK(plugins::parse_manifest(read_file(root / "plugin.json"), m, err));
        for (u32 p = 0; p < m.passes.size(); ++p) {
            std::vector<u32> spirv;
            AUREA_CHECK_MSG(plugins::compile_pass(m, p, read_file(root / m.passes[p].shader), spirv, err), err.c_str());
            std::string essl, msl;
            AUREA_CHECK_MSG(xlate::spirv_to_essl(spirv.data(), spirv.size(), essl, err), err.c_str());
            AUREA_CHECK(essl.find("#version 310 es") != std::string::npos);
            AUREA_CHECK(essl.find("aurea_sampler_fs0") != std::string::npos);   // binding 0 = a fonte
            AUREA_CHECK(essl.find("aurea_ubo_fs0") != std::string::npos);       // binding 12 → bloco 0
            if (xlate::msl_available()) {
                u32 group[3]{};
                AUREA_CHECK_MSG(xlate::spirv_to_msl(spirv.data(), spirv.size(), "fs_main", 0xffffffffu, group, msl, err),
                                err.c_str());
                AUREA_CHECK(msl.find("fragment") != std::string::npos && msl.find("fs_main") != std::string::npos);
                AUREA_CHECK(!xlate::spirv_writes_color1_or_more(spirv.data(), spirv.size()));
            }
        }
    }
}

// 0.3.1: plugins "shader3d" — câmera da cena e camadas ligadas (nulo/câmera/luz).
AUREA_TEST(Plugins, Shader3dManifestLinksCameraAndNullLayers) {
    const std::string ok =
        "{\"aureaPlugin\":1,\"id\":\"com.x.mira\",\"version\":\"1.0.0\",\"name\":\"Mira\",\"kind\":\"shader3d\","
        "\"params\":[{\"id\":\"target\",\"type\":\"layer\",\"accept\":[\"null\",\"camera\"],\"label\":\"Alvo\"},"
        "{\"id\":\"size\",\"type\":\"float\",\"default\":0.2}],\"passes\":[{\"shader\":\"a.frag\"}]}";
    plugins::Manifest m;
    std::string err;
    AUREA_CHECK_MSG(plugins::parse_manifest(ok, m, err), err.c_str());
    AUREA_CHECK(m.scene3d);
    AUREA_CHECK(m.kind == plugins::Kind::Shader);
    AUREA_CHECK_EQ(m.params.size(), static_cast<usize>(2));
    if (m.params.size() == 2) {
        AUREA_CHECK(m.params[0].kind == plugins::ParamKind::Layer);
        AUREA_CHECK_EQ(m.params[0].accept, static_cast<u16>(plugins::kAcceptNull | plugins::kAcceptCamera));
        AUREA_CHECK(!m.params[0].animatable);
    }
    // Lido de volta igual (o manifesto que a instalação grava).
    plugins::Manifest again;
    AUREA_CHECK_MSG(plugins::parse_manifest(plugins::manifest_json(m), again, err), err.c_str());
    AUREA_CHECK(again.scene3d && again.params[0].accept == m.params[0].accept);
    // O que é recusado, com o motivo.
    struct Case { std::string json; const char* expect; };
    std::string five = "{\"aureaPlugin\":1,\"id\":\"com.x.cinco\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader3d\",\"params\":[";
    for (int i = 0; i < 5; ++i) five += std::string(i ? "," : "") + "{\"id\":\"l" + std::to_string(i) + "\",\"type\":\"layer\"}";
    five += "],\"passes\":[{\"shader\":\"a.frag\"}]}";
    const Case cases[] = {
        {"{\"aureaPlugin\":1,\"id\":\"com.x.flat\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader\","
         "\"params\":[{\"id\":\"alvo\",\"type\":\"layer\"}],\"passes\":[{\"shader\":\"a.frag\"}]}", "shader3d"},
        {five, "5 parametros \"layer\" (limite 4)"},
        {"{\"aureaPlugin\":1,\"id\":\"com.x.acc\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader3d\","
         "\"params\":[{\"id\":\"alvo\",\"type\":\"layer\",\"accept\":[\"video\"]}],\"passes\":[{\"shader\":\"a.frag\"}]}", "accept"},
    };
    for (const Case& c : cases) {
        plugins::Manifest out;
        std::string why;
        AUREA_CHECK_MSG(!plugins::parse_manifest(c.json, out, why), c.expect);
        if (why.find(c.expect) == std::string::npos) std::printf("\n    esperado '%s', veio '%s'", c.expect, why.c_str());
        AUREA_CHECK(why.find(c.expect) != std::string::npos);
    }
    // O cabeçalho 3D compila e traduz para GLES e Metal, usando cada função de apoio.
    const char* source =
        "vec4 aurea_main(vec2 uv) {\n"
        "    vec3 ro, rd; aurea_ray(uv, ro, rd);\n"
        "    vec3 p = aurea_layer_valid(target) ? aurea_layer_position(target) : vec3(0.0);\n"
        "    vec3 c = aurea_world_to_comp(p);\n"
        "    float d = length(cross(rd, p - ro)) + float(aurea_layer_kind(target)) * 0.0 + aurea_layer_fov(target) * 0.0;\n"
        "    vec3 v = aurea_world_to_view(p) + aurea_layer_axis(target, 0) + aurea_camera_position() * 0.0;\n"
        "    return vec4(vec3(step(d, size * 100.0)) + c.z * 0.0 + v.x * 0.0, 1.0) * texture(aurea_source, uv);\n"
        "}\n";
    std::vector<u32> spirv;
    AUREA_CHECK_MSG(plugins::compile_pass(m, 0, source, spirv, err), err.c_str());
    std::string essl, msl;
    AUREA_CHECK_MSG(xlate::spirv_to_essl(spirv.data(), spirv.size(), essl, err), err.c_str());
    if (xlate::msl_available()) {
        u32 group[3]{};
        AUREA_CHECK_MSG(xlate::spirv_to_msl(spirv.data(), spirv.size(), "fs_main", 0xffffffffu, group, msl, err), err.c_str());
    }
    // Plugin plano (shader) não recebe os símbolos 3D: o cabeçalho dele não mudou.
    plugins::Manifest flat;
    AUREA_CHECK(plugins::parse_manifest(one_pass_manifest("com.x.plano"), flat, err));
    AUREA_CHECK(plugins::shader_header(flat, 0).find("layerWorld") == std::string::npos);
    AUREA_CHECK(plugins::shader_header(m, 0).find("layerWorld") != std::string::npos);
}

AUREA_TEST(Plugins, InstallRejectsInvalidPackagesWithMessages) {
    CpuManager cpu;
    const fs::path work = fresh_dir("pacotes");
    plugins::InstallHooks none;
    auto install = [&](const char* name, const std::vector<std::pair<std::string, std::string>>& files) {
        const fs::path zip = work / name;
        write_zip(zip, files);
        return cpu.manager.install(str(zip), none);
    };
    // > 8 passes.
    {
        std::string json = "{\"aureaPlugin\":1,\"id\":\"com.x.nine\",\"version\":\"1.0.0\",\"name\":\"N\",\"kind\":\"shader\",\"passes\":[";
        std::vector<std::pair<std::string, std::string>> files;
        for (int i = 0; i < 9; ++i) {
            json += std::string(i ? "," : "") + "{\"shader\":\"p.frag\"" + (i < 8 ? ",\"target\":\"t" + std::to_string(i) + "\"" : "") + "}";
        }
        json += "]}";
        files.push_back({"plugin.json", json});
        files.push_back({"p.frag", kIdentityShader});
        const auto r = install("nove.aureaplugin", files);
        AUREA_CHECK(!r.ok() && detail_of(r).find("9 passes (limite 8)") != std::string::npos);
    }
    // Id inválido.
    {
        const auto r = install("id.aureaplugin", {{"plugin.json", one_pass_manifest("x")}, {"main.frag", kIdentityShader}});
        AUREA_CHECK(!r.ok() && detail_of(r).find("\"id\" invalido") != std::string::npos);
    }
    // Arquivo de shader faltando.
    {
        const auto r = install("falta.aureaplugin", {{"plugin.json", one_pass_manifest("com.x.falta", "1.0.0", "sumiu.frag")}});
        AUREA_CHECK(!r.ok() && detail_of(r).find("arquivo de shader ausente: sumiu.frag") != std::string::npos);
    }
    // Erro de compilação: arquivo:linha.
    {
        const auto r = install("erro.aureaplugin", {{"plugin.json", one_pass_manifest("com.x.erro", "1.0.0", "erro.frag")},
                                                    {"erro.frag", "vec4 aurea_main(vec2 uv) {\n  return naoExiste;\n}\n"}});
        std::printf("\n    recusa: %s", detail_of(r).c_str());
        AUREA_CHECK(!r.ok() && r.status().code() == Errc::ShaderCompileFailed);
        AUREA_CHECK(detail_of(r).find("erro.frag:2:") != std::string::npos);
    }
    // Não é ZIP.
    {
        const fs::path junk = work / "lixo.aureaplugin";
        std::ofstream(junk, std::ios::binary) << "isto nao e um zip";
        const auto r = cpu.manager.install(str(junk), none);
        AUREA_CHECK(!r.ok() && detail_of(r).find("nao e um pacote") != std::string::npos);
    }
    // Shader fora do pacote (caminho com "..").
    {
        const auto r = install("fora.aureaplugin", {{"plugin.json", one_pass_manifest("com.x.fora", "1.0.0", "../fora.frag")}});
        AUREA_CHECK(!r.ok() && detail_of(r).find("caminho de shader invalido") != std::string::npos);
    }
    // Shader maior que 64 KB.
    {
        std::string big = kIdentityShader;
        big += "// " + std::string(70 * 1024, 'x') + "\n";
        const auto r = install("grande.aureaplugin", {{"plugin.json", one_pass_manifest("com.x.grande")}, {"main.frag", big}});
        AUREA_CHECK(!r.ok() && detail_of(r).find("maior que 64 KB") != std::string::npos);
    }
    // Versão mais velha que a instalada.
    {
        const auto v2 = install("v2.aureaplugin", {{"plugin.json", one_pass_manifest("com.x.versao", "2.0.0")}, {"main.frag", kIdentityShader}});
        AUREA_CHECK_MSG(v2.ok(), detail_of(v2).c_str());
        const auto v1 = install("v1.aureaplugin", {{"plugin.json", one_pass_manifest("com.x.versao", "1.5.0")}, {"main.frag", kIdentityShader}});
        AUREA_CHECK(!v1.ok() && detail_of(v1).find("ja esta instalada a versao 2.0.0") != std::string::npos);
        const auto v3 = install("v3.aureaplugin", {{"plugin.json", one_pass_manifest("com.x.versao", "2.1.0")}, {"main.frag", kIdentityShader}});
        AUREA_CHECK(v3.ok());
        const auto list = cpu.manager.list();
        AUREA_CHECK_EQ(list.size(), static_cast<usize>(1));
        if (!list.empty()) AUREA_CHECK_EQ(list[0].version, std::string("2.1.0"));
    }
    // Quadro de teste lento: recusado pelo teto de 250 ms, com o tempo na mensagem.
    {
        plugins::InstallHooks slow;
        slow.testRender = [](const EffectRegistry&, EffectTypeId, f32& ms, std::string&) { ms = 400.0f; return true; };
        write_zip(work / "lento.aureaplugin", {{"plugin.json", one_pass_manifest("com.x.lento")}, {"main.frag", kIdentityShader}});
        const auto r = cpu.manager.install(str(work / "lento.aureaplugin"), slow);
        AUREA_CHECK(!r.ok() && r.status().code() == Errc::Timeout);
        AUREA_CHECK(detail_of(r).find("400 ms (limite 250 ms") != std::string::npos);
        AUREA_CHECK(!cpu.effects.find(effect_type_id("plugin.com.x.lento")));
    }
    // Nada das recusas ficou na pasta nem no catálogo; os shaders saíram do registro.
    u32 dirs = 0;
    for (const auto& entry : fs::directory_iterator(cpu.dir)) dirs += entry.is_directory() ? 1u : 0u;
    AUREA_CHECK_EQ(dirs, 1u);   // só com.x.versao
    AUREA_CHECK(!cpu.effects.find(effect_type_id("plugin.com.x.erro")));
    // Remover: catálogo e pasta.
    AUREA_CHECK(cpu.manager.remove("com.x.versao"));
    AUREA_CHECK(!cpu.manager.remove("com.x.versao"));
    AUREA_CHECK(!cpu.effects.find(effect_type_id("plugin.com.x.versao")));
    AUREA_CHECK(!fs::exists(cpu.dir / "com.x.versao"));
    std::error_code ec;
    fs::remove_all(work, ec);
}

AUREA_TEST(Plugins, StackPluginExpandsIntoBuiltinsInThePlan) {
    CpuManager cpu;
    const auto r = cpu.manager.install(examples() + "/dream-look.aureaplugin", {});
    AUREA_CHECK_MSG(r.ok(), detail_of(r).c_str());
    const EffectTypeId type = effect_type_id("plugin.com.aurea.examples.dreamlook");
    const Effect* fx = cpu.effects.find(type);
    AUREA_CHECK(fx && fx->expands_stack());
    if (!fx) return;
    std::vector<ParamValue> values{ParamValue::scalar(0.5f), ParamValue::scalar(0.7f), ParamValue::color(1, 0.5f, 0.25f, 1)};
    std::vector<StackStep> steps;
    AUREA_CHECK(fx->expand_stack(values.data(), 3, steps));
    AUREA_CHECK_EQ(steps.size(), static_cast<usize>(3));
    if (steps.size() == 3) {
        AUREA_CHECK_EQ(steps[0].type, effect_type_id(effect_keys::kGaussianBlur));
        AUREA_CHECK_NEAR(steps[0].values[0].second.v[0], 6.0f, 1e-5f);
        AUREA_CHECK_NEAR(steps[1].values[0].second.v[0], 39.0f, 1e-4f);   // 60 - 0,7*30
        AUREA_CHECK_NEAR(steps[1].values[3].second.v[1], 0.5f, 1e-6f);   // cópia da cor
        AUREA_CHECK_NEAR(steps[2].values[0].second.v[0], -12.5f, 1e-5f);
    }
    // Pilha com efeito que não pode entrar, parâmetro desconhecido e lista curta.
    const fs::path work = fresh_dir("pilhas");
    auto stack = [&](const char* name, const std::string& body) {
        write_zip(work / name, {{"plugin.json", "{\"aureaPlugin\":1,\"id\":\"com.x.pilha\",\"version\":\"1.0.0\",\"name\":\"P\","
                                                "\"kind\":\"stack\",\"params\":[{\"id\":\"a\",\"type\":\"float\"}],\"stack\":[" + body + "]}"}});
        return cpu.manager.install(str(work / name), {});
    };
    auto r1 = stack("t.aureaplugin", "{\"effect\":\"aurea.time.echo\"}");
    AUREA_CHECK(!r1.ok() && detail_of(r1).find("nao pode entrar numa pilha") != std::string::npos);
    auto r2 = stack("p.aureaplugin", "{\"effect\":\"aurea.blur.gaussian\",\"set\":{\"raio\":1}}");
    AUREA_CHECK(!r2.ok() && detail_of(r2).find("parametro desconhecido \"raio\"") != std::string::npos);
    auto r3 = stack("c.aureaplugin", "{\"effect\":\"aurea.light.glow\",\"set\":{\"color\":\"a\"}}");
    AUREA_CHECK(!r3.ok() && detail_of(r3).find("4 componentes") != std::string::npos);
    auto r4 = stack("e.aureaplugin", "{\"effect\":\"aurea.blur.gaussian\",\"set\":{\"blurriness\":\"a * \"}}");
    AUREA_CHECK(!r4.ok() && detail_of(r4).find("blurriness") != std::string::npos);
    auto r5 = stack("x.aureaplugin", "{\"effect\":\"aurea.nao.existe\"}");
    AUREA_CHECK(!r5.ok() && detail_of(r5).find("efeito embutido desconhecido") != std::string::npos);
    std::error_code ec;
    fs::remove_all(work, ec);
}

AUREA_TEST(Plugins, RuntimeShaderIdsAreDynamicAndLibrariesDropRemovedOnes) {
    // ShaderLibrary + MockBackend: o id dinâmico vira módulo sob demanda e sai
    // (com os pipelines) quando o registro o tira.
    std::vector<u32> spirv;
    std::string err;
    plugins::Manifest m;
    AUREA_CHECK(plugins::parse_manifest(one_pass_manifest("com.x.lib"), m, err));
    AUREA_CHECK(plugins::compile_pass(m, 0, kIdentityShader, spirv, err));
    const ShaderId id = runtime_shaders::register_shader(ShaderStage::Fragment, spirv, "teste");
    AUREA_CHECK(is_runtime_shader(id));
    AUREA_CHECK(static_cast<u32>(id) >= kRuntimeShaderBase);
    AUREA_CHECK(runtime_shaders::find(id) != nullptr);
    runtime_shaders::unregister_shader(id);
    AUREA_CHECK(runtime_shaders::find(id) == nullptr);
    const ShaderId again = runtime_shaders::register_shader(ShaderStage::Fragment, spirv, "teste 2");
    AUREA_CHECK(again != id);   // nunca reaproveitado no processo
    runtime_shaders::unregister_shader(again);
}

// =============================================================================
// GPU
// =============================================================================
#if defined(AUREA_TEST_VULKAN)

namespace {

f32 half_to_float(u16 h) {
    const u32 sign = (h & 0x8000u) << 16;
    u32 exp = (h >> 10) & 0x1F, mant = h & 0x3FF, bits;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else { exp = 127 - 15 + 1; while (!(mant & 0x400)) { mant <<= 1; --exp; } mant &= 0x3FF; bits = sign | (exp << 23) | (mant << 13); }
    } else if (exp == 31) bits = sign | 0x7F800000u | (mant << 13);
    else bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    f32 f;
    std::memcpy(&f, &bits, 4);
    return f;
}

struct Image {
    u32 width = 0, height = 0;
    std::vector<f32> px;   // RGBA linear pré-multiplicado
    [[nodiscard]] Vec4 at(u32 x, u32 y) const {
        const f32* p = &px[(static_cast<usize>(y) * width + x) * 4];
        return Vec4{p[0], p[1], p[2], p[3]};
    }
};

f32 max_diff(const Image& a, const Image& b) {
    if (a.px.size() != b.px.size()) return 1e9f;
    f32 m = 0.0f;
    for (usize i = 0; i < a.px.size(); ++i) m = std::max(m, std::fabs(a.px[i] - b.px[i]));
    return m;
}

bool near4(Vec4 a, Vec4 b, f32 tol, const char* what) {
    const bool ok = std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol && std::fabs(a.z - b.z) <= tol
                 && std::fabs(a.w - b.w) <= tol;
    if (!ok) std::printf("\n    %s: obtido (%.4f %.4f %.4f %.4f) esperado (%.4f %.4f %.4f %.4f)", what, a.x, a.y, a.z, a.w,
                         b.x, b.y, b.z, b.w);
    return ok;
}

/// GPU dos testes de plugin: backend, embutidos + plugins, renderer.
struct Rig {
    vk::Backend backend;
    EffectRegistry effects;
    Renderer renderer;
    std::unique_ptr<plugins::PluginManager> manager;
    fs::path dir;
    bool ok = false;
    std::unordered_map<u64, TextureHandle> targets;

    Rig() {
        BackendConfig cfg;
        cfg.enableValidation = true;
        cfg.framesInFlight = 2;
        if (!backend.initialize(cfg).ok()) return;
        register_builtin_effects(effects);
        ok = renderer.initialize(backend, effects).ok();
        manager = std::make_unique<plugins::PluginManager>(effects);
        dir = fresh_dir("gpu");
        manager->set_directory(str(dir));
    }
    ~Rig() {
        backend.wait_idle();
        for (auto& [k, t] : targets) backend.destroy_texture(t);
        manager->shutdown();
        renderer.shutdown();
        backend.shutdown();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    plugins::InstallHooks hooks() {
        plugins::InstallHooks h;
        h.compileOnBackend = [this](const runtime_shaders::Source& s) -> std::string {
            ShaderDesc d;
            d.stage = s.stage;
            d.spirv = s.spirv.data();
            d.spirvBytes = s.spirv.size() * 4;
            d.debugName = s.name.c_str();
            std::string error;
            auto created = backend.create_runtime_shader(d, &error);
            if (!created.ok()) return error.empty() ? std::string("recusado") : error;
            backend.destroy_shader(*created);
            return {};
        };
        h.testRender = [this](const EffectRegistry& reg, EffectTypeId type, f32& ms, std::string& error) {
            std::vector<u8> out;
            Status s = renderer.render_effect_preview(reg, type, plugins::kTestRenderSize, plugins::kTestRenderSize, out);
            const auto t0 = std::chrono::steady_clock::now();
            if (s.ok()) s = renderer.render_effect_preview(reg, type, plugins::kTestRenderSize, plugins::kTestRenderSize, out);
            ms = std::chrono::duration<f32, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!s.ok()) error = std::string(s.message()) + " " + renderer.shaders().last_error();
            return s.ok();
        };
        return h;
    }
    Result<std::string> install(const std::string& path) { return manager->install(path, hooks()); }
    TextureHandle target(u32 w, u32 h) {
        const u64 key = (static_cast<u64>(w) << 32) | h;
        if (auto it = targets.find(key); it != targets.end()) return it->second;
        TextureDesc d;
        d.width = w; d.height = h; d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true; d.sampled = true; d.transferSrc = true;
        auto t = backend.create_texture(d);
        targets[key] = *t;
        return *t;
    }
};

Rig& rig() {
    static Rig r;
    return r;
}

#define AUREA_REQUIRE_PLUGIN_GPU()                                     \
    do {                                                               \
        if (!rig().ok) { std::printf("(sem GPU Vulkan: pulado) "); return; } \
    } while (0)

struct Scene {
    Project project;
    Composition* comp = nullptr;
    std::unordered_map<u64, ImagePixels> images;
    MediaManager media;

    Scene(u32 w, u32 h) {
        auto p = Project::create_new(w, h, 30.0, "plugins");
        project = std::move(*p);
        comp = project.timeline().composition(project.timeline().root());
        comp->set_background(Color{0, 0, 0, 0});
        comp->set_duration(FrameIndex{120});
        rig().renderer.release_project_resources();
    }
    static const ImagePixels* lookup(void* self, AssetId id) {
        auto* s = static_cast<Scene*>(self);
        auto it = s->images.find(id.pack());
        return it == s->images.end() ? nullptr : &it->second;
    }
    LayerId image(ImagePixels px) {
        Asset a;
        a.kind = AssetKind::Image;
        const AssetId aid = project.add_asset(std::move(a));
        const u32 w = px.width, h = px.height;
        images[aid.pack()] = std::move(px);
        const LayerId id = comp->add_layer(LayerKind::Image, "img");
        Layer* l = comp->layer(id);
        l->source = aid;
        l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0};
        l->transform.position = Vec3{comp->width() * 0.5f, comp->height() * 0.5f, 0};
        return id;
    }
    EffectInstance& add_effect(LayerId id, std::string_view key) {
        Layer* l = comp->layer(id);
        EffectInstance e;
        e.id = l->alloc_effect_id();
        e.type = effect_type_id(key);
        const ParameterRegistry* params = rig().effects.params(e.type);
        if (params) initialize_instance(e, *params);
        l->effects.push_back(std::move(e));
        return l->effects.back();
    }
    Image render(FrameIndex t = FrameIndex{0}) {
        Rig& g = rig();
        RenderSettings rs;
        rs.previewDenominator = 1;
        rs.dither = false;
        rs.finalQuality = true;
        FrameSnapshot snap;
        static u64 frame = 0;
        g.renderer.prepare(*comp, project, t, &media, &Scene::lookup, this, rs, ++frame, 0, DecodeMode::Still, 1.0f, snap);
        const u32 w = comp->width(), h = comp->height();
        OffscreenTarget off{g.target(w, h), w, h};
        FrameStats stats;
        RenderTimings timings;
        const Status s = g.renderer.render(snap, rs, &off, stats, timings);
        AUREA_CHECK_MSG(s.ok(), "render offscreen falhou");
        g.backend.wait_idle();
        std::vector<u16> half(static_cast<usize>(w) * h * 4);
        AUREA_CHECK(g.backend.read_texture(off.texture, half.data(), w * 8).ok());
        Image img;
        img.width = w;
        img.height = h;
        img.px.resize(half.size());
        for (usize i = 0; i < half.size(); ++i) img.px[i] = half_to_float(half[i]);
        return img;
    }
};

ImagePixels uniform(u32 w, u32 h, u8 v) {
    ImagePixels px;
    px.width = w;
    px.height = h;
    px.rgba.assign(static_cast<usize>(w) * h * 4, v);
    for (usize i = 3; i < px.rgba.size(); i += 4) px.rgba[i] = 255;
    return px;
}

/// Metade esquerda preta, direita branca (opaca).
ImagePixels half_black_white(u32 w, u32 h) {
    ImagePixels px = uniform(w, h, 0);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = w / 2; x < w; ++x)
            for (int c = 0; c < 3; ++c) px.rgba[(static_cast<usize>(y) * w + x) * 4 + c] = 255;
    return px;
}

/// Degradê com disco claro: dá borda e área clara aos efeitos de luz.
ImagePixels reference(u32 w, u32 h) {
    ImagePixels px = uniform(w, h, 0);
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
        u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
        p[0] = static_cast<u8>(x * 255 / (w - 1));
        p[1] = static_cast<u8>(y * 255 / (h - 1));
        p[2] = 64;
        const f32 dx = static_cast<f32>(x) - w * 0.5f, dy = static_cast<f32>(y) - h * 0.5f;
        if (dx * dx + dy * dy < (h * 0.25f) * (h * 0.25f)) p[0] = p[1] = p[2] = 255;
    }
    return px;
}

void ensure_installed(const char* package) {
    const auto r = rig().install(examples() + "/" + package);
    AUREA_CHECK_MSG(r.ok(), detail_of(r).c_str());
}

} // namespace

AUREA_TEST(PluginsGpu, RgbWaveShiftsChannelsByTheDeclaredPixels) {
    AUREA_REQUIRE_PLUGIN_GPU();
    ensure_installed("rgb-wave.aureaplugin");
    Scene s(64, 32);
    const LayerId id = s.image(half_black_white(64, 32));
    const Image plain = s.render();
    EffectInstance& e = s.add_effect(id, "plugin.com.aurea.examples.rgbwave");
    AUREA_CHECK_EQ(e.params.size(), static_cast<usize>(6));
    if (e.params.size() != 6) return;
    e.params[0].constant = ParamValue::scalar(8.0f);   // amount: 8 px
    e.params[1].constant = ParamValue::scalar(0.0f);   // wave 0: deslocamento constante
    e.params[4].constant = ParamValue::scalar(0.0f);   // direção 0 = para a direita
    const Image shifted = s.render();
    // Borda em x = 32. O vermelho lê x+8, o verde x, o azul x-8 (centros de pixel exatos).
    AUREA_CHECK(near4(shifted.at(10, 16), Vec4{0, 0, 0, 1}, 2e-3f, "preto longe da borda"));
    AUREA_CHECK(near4(shifted.at(27, 16), Vec4{1, 0, 0, 1}, 2e-3f, "x=27: so o vermelho ve o branco"));
    AUREA_CHECK(near4(shifted.at(36, 16), Vec4{1, 1, 0, 1}, 2e-3f, "x=36: o azul ainda ve o preto"));
    AUREA_CHECK(near4(shifted.at(50, 16), Vec4{1, 1, 1, 1}, 2e-3f, "branco longe da borda"));
    // Cor: tinta vermelha (linear) zera verde e azul.
    s.comp->layer(id)->effects.back().params[5].constant = ParamValue::color(1, 0, 0, 1);
    const Image tinted = s.render();
    AUREA_CHECK(near4(tinted.at(50, 16), Vec4{1, 0, 0, 1}, 2e-3f, "tinta vermelha"));
    s.comp->layer(id)->effects.back().params[5].constant = ParamValue::color(1, 1, 1, 1);
    // Keyframes: 0 px no quadro 0, 8 px no quadro 10.
    Layer* l = s.comp->layer(id);
    Track& t = l->tracks.get_or_create(TrackProperty::EffectParam, l->effects.back().id, param_track_key(0, 0));
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{10}, 8.0f);
    AUREA_CHECK_MSG(max_diff(s.render(FrameIndex{0}), plain) < 2e-3f, "quadro 0: deslocamento 0 = a imagem original");
    const Image keyed = s.render(FrameIndex{10});
    AUREA_CHECK_MSG(max_diff(keyed, shifted) < 2e-3f, "quadro 10: o keyframe de 8 px");
    const Image middle = s.render(FrameIndex{5});   // 4 px
    AUREA_CHECK(near4(middle.at(29, 16), Vec4{1, 0, 0, 1}, 2e-3f, "quadro 5: 4 px"));
    AUREA_CHECK(near4(middle.at(27, 16), Vec4{0, 0, 0, 1}, 2e-3f, "quadro 5: x=27 fora do alcance de 4 px"));
}

// 0.3.1: o plugin 3D lê o mundo do nulo ligado (e a câmera padrão sem câmera).
AUREA_TEST(PluginsGpu, Shader3dReadsTheLinkedNullAndTheSceneCamera) {
    AUREA_REQUIRE_PLUGIN_GPU();
    const fs::path work = fresh_dir("cena3d");
    write_zip(work / "cena3d.aureaplugin", {
        {"plugin.json", "{\"aureaPlugin\":1,\"id\":\"com.x.cena3d\",\"version\":\"1.0.0\",\"name\":\"Cena\",\"kind\":\"shader3d\","
                        "\"params\":[{\"id\":\"alvo\",\"type\":\"layer\",\"accept\":[\"null\"]}],"
                        "\"passes\":[{\"shader\":\"a.frag\"}]}"},
        {"a.frag", "vec4 aurea_main(vec2 uv) {\n"
                   "    float linked = aurea_layer_valid(alvo) ? 1.0 : 0.0;\n"
                   "    vec3 p = aurea_layer_position(alvo);\n"
                   "    return vec4(linked, clamp(p.x / aurea.comp.x, 0.0, 1.0), clamp(p.y / aurea.comp.y, 0.0, 1.0), 1.0);\n"
                   "}\n"},
    });
    const auto r = rig().install(str(work / "cena3d.aureaplugin"));
    AUREA_CHECK_MSG(r.ok(), detail_of(r).c_str());
    Scene s(64, 32);
    const LayerId img = s.image(uniform(64, 32, 128));
    const LayerId null = s.comp->add_layer(LayerKind::Null, "alvo");
    s.comp->layer(null)->transform.position = Vec3{16, 24, 0};
    EffectInstance& e = s.add_effect(img, "plugin.com.x.cena3d");
    AUREA_CHECK_EQ(e.params.size(), static_cast<usize>(1));
    if (e.params.size() != 1) return;
    // Sem camada ligada: vermelho 0.
    AUREA_CHECK(near4(s.render().at(10, 10), Vec4{0, 0, 0, 1}, 2e-3f, "sem ligacao"));
    Layer* l = s.comp->layer(img);
    l->effects.back().params[0].constant.ref = null.pack();
    l->effects.back().params[0].constant.v[0] = static_cast<f32>(null.index);
    // Ligado: a posição do nulo em px da composição (16/64, 24/32).
    AUREA_CHECK(near4(s.render().at(10, 10), Vec4{1, 0.25f, 0.75f, 1}, 2e-3f, "nulo ligado"));
    // O nulo anda: o plugin acompanha.
    s.comp->layer(null)->transform.position = Vec3{48, 8, 0};
    AUREA_CHECK(near4(s.render().at(10, 10), Vec4{1, 0.75f, 0.25f, 1}, 2e-3f, "nulo movido"));
    std::error_code ec;
    fs::remove_all(work, ec);
}

AUREA_TEST(PluginsGpu, MultiPassIntermediateRunsAtTheDeclaredScale) {
    AUREA_REQUIRE_PLUGIN_GPU();
    // Passe 1 em 1/4 escreve a PRÓPRIA resolução; o passe 2 lê e acrescenta a dele.
    const fs::path work = fresh_dir("escala");
    write_zip(work / "escala.aureaplugin", {
        {"plugin.json", "{\"aureaPlugin\":1,\"id\":\"com.x.escala\",\"version\":\"1.0.0\",\"name\":\"Escala\",\"kind\":\"shader\","
                        "\"passes\":[{\"shader\":\"a.frag\",\"target\":\"small\",\"scale\":0.25},"
                        "{\"shader\":\"b.frag\",\"inputs\":{\"small\":\"small\"}}]}"},
        {"a.frag", "vec4 aurea_main(vec2 uv) { return vec4(aurea.resolution.x / 256.0, aurea.resolution.y / 256.0 + aurea.layer.z, 0.0, 1.0); }\n"},
        {"b.frag", "vec4 aurea_main(vec2 uv) { vec4 s = texture(small, uv); return vec4(s.rg, aurea.resolution.x / 256.0, 1.0); }\n"},
    });
    const auto r = rig().install(str(work / "escala.aureaplugin"));
    AUREA_CHECK_MSG(r.ok(), detail_of(r).c_str());
    Scene s(64, 32);
    const LayerId id = s.image(uniform(64, 32, 128));
    s.add_effect(id, "plugin.com.x.escala");
    const Image img = s.render();
    // Passe 1: 16x8 texels e 0,25 texel por px da camada (g = 8/256 + 0,25);
    // passe 2: 64 texels. (A saída da composição é opaca: o alfa não conta.)
    AUREA_CHECK(near4(img.at(20, 10), Vec4{16.0f / 256.0f, 8.0f / 256.0f + 0.25f, 64.0f / 256.0f, 1.0f}, 1e-3f, "escala dos passes"));
    std::error_code ec;
    fs::remove_all(work, ec);
}

AUREA_TEST(PluginsGpu, SoftGlowAddsLightAroundBrightAreas) {
    AUREA_REQUIRE_PLUGIN_GPU();
    ensure_installed("soft-glow.aureaplugin");
    Scene s(96, 64);
    // Quadrado branco no meio, o resto preto: o brilho tem de acender em volta.
    ImagePixels px = uniform(96, 64, 0);
    for (u32 y = 24; y < 40; ++y) for (u32 x = 40; x < 56; ++x)
        for (int c = 0; c < 3; ++c) px.rgba[(static_cast<usize>(y) * 96 + x) * 4 + c] = 255;
    const LayerId id = s.image(std::move(px));
    const Image plain = s.render();
    EffectInstance& e = s.add_effect(id, "plugin.com.aurea.examples.softglow");
    e.params[3].constant = ParamValue::color(1, 1, 1, 1);   // brilho branco
    const Image glow = s.render();
    // Fora do quadrado (6 px à esquerda) era preto e agora tem luz; longe continua preto.
    AUREA_CHECK(plain.at(34, 32).x < 1e-3f);
    AUREA_CHECK_MSG(glow.at(34, 32).x > 0.05f, "o brilho deve passar da borda do quadrado");
    AUREA_CHECK_MSG(glow.at(2, 2).x < 0.02f, "longe do quadrado continua escuro");
    // Dentro: a imagem original MAIS o brilho (nunca menos).
    AUREA_CHECK(glow.at(48, 32).x >= plain.at(48, 32).x - 1e-3f);
    // Intensidade 0: a imagem original.
    s.comp->layer(id)->effects.back().params[2].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK_MSG(max_diff(s.render(), plain) < 2e-3f, "intensidade 0 = original");
}

AUREA_TEST(PluginsGpu, StackPluginEqualsTheBuiltinsAppliedByHand) {
    AUREA_REQUIRE_PLUGIN_GPU();
    ensure_installed("dream-look.aureaplugin");
    const Vec4 warmth{1.0f, 0.6f, 0.3f, 1.0f};
    Image viaPlugin, byHand;
    {
        Scene s(96, 64);
        const LayerId id = s.image(reference(96, 64));
        EffectInstance& e = s.add_effect(id, "plugin.com.aurea.examples.dreamlook");
        e.params[0].constant = ParamValue::scalar(0.5f);
        e.params[1].constant = ParamValue::scalar(0.7f);
        e.params[2].constant = ParamValue::color(warmth.x, warmth.y, warmth.z, warmth.w);
        viaPlugin = s.render();
    }
    {
        Scene s(96, 64);
        const LayerId id = s.image(reference(96, 64));
        const ParameterRegistry* blur = rig().effects.params(effect_type_id(effect_keys::kGaussianBlur));
        const ParameterRegistry* glow = rig().effects.params(effect_type_id(effect_keys::kGlow));
        const ParameterRegistry* sat = rig().effects.params(effect_type_id(effect_keys::kSaturation));
        s.add_effect(id, effect_keys::kGaussianBlur).params[blur->find("blurriness")].constant = ParamValue::scalar(6.0f);
        EffectInstance& g = s.add_effect(id, effect_keys::kGlow);
        g.params[glow->find("threshold")].constant = ParamValue::scalar(39.0f);
        g.params[glow->find("radius")].constant = ParamValue::scalar(62.0f);
        g.params[glow->find("intensity")].constant = ParamValue::scalar(1.55f);
        g.params[glow->find("color")].constant = ParamValue::color(warmth.x, warmth.y, warmth.z, warmth.w);
        s.add_effect(id, effect_keys::kSaturation).params[sat->find("saturation")].constant = ParamValue::scalar(-12.5f);
        byHand = s.render();
    }
    const f32 diff = max_diff(viaPlugin, byHand);
    std::printf("\n    pilha x embutidos: diferenca maxima %.6f", static_cast<double>(diff));
    AUREA_CHECK_MSG(diff < 1e-3f, "o plugin de pilha deve dar o mesmo quadro dos embutidos");
    // E de fato mudou a imagem.
    Scene plain(96, 64);
    plain.image(reference(96, 64));
    AUREA_CHECK(max_diff(plain.render(), viaPlugin) > 0.05f);
}

AUREA_TEST(PluginsGpu, IsfVignetteKeepsIsfOrientationAndSrgbMath) {
    AUREA_REQUIRE_PLUGIN_GPU();
    ensure_installed("pulse-vignette.fs");
    Scene s(64, 32);
    const LayerId id = s.image(uniform(64, 32, 128));
    const Image plain = s.render();
    EffectInstance& e = s.add_effect(id, "plugin.isf.pulse-vignette");
    AUREA_CHECK_EQ(e.params.size(), static_cast<usize>(5));
    if (e.params.size() != 5) return;
    e.params[0].constant = ParamValue::scalar(1.0f);          // strength
    e.params[1].constant = ParamValue::scalar(0.0f);          // rate 0: pulso parado em 0,5 → fator 0,8
    e.params[2].constant = ParamValue::vec2(0.5f, 0.0f);      // centro no TOPO (y do motor para baixo)
    const Image img = s.render();
    // Topo, no centro: distância ~0 → sem vinheta.
    AUREA_CHECK(near4(img.at(32, 0), plain.at(32, 0), 2e-3f, "topo central intacto"));
    // Base: distância ~0,98 → v = 0,8 em sRGB: 0,2 * 128/255, linearizado.
    const f32 expected = plugins::srgb_to_linear(0.2f * plugins::linear_to_srgb(plain.at(32, 31).x));
    AUREA_CHECK(near4(img.at(32, 31), Vec4{expected, expected, expected, 1.0f}, 3e-3f, "base escurecida em sRGB"));
    // Strength 0: a imagem, depois da ida e volta sRGB.
    s.comp->layer(id)->effects.back().params[0].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK_MSG(max_diff(s.render(), plain) < 3e-3f, "strength 0 = original");
}

AUREA_TEST(PluginsGpu, ProjectKeepsPluginEffectWithoutThePluginAndRestoresOnReinstall) {
    AUREA_REQUIRE_PLUGIN_GPU();
    const fs::path pluginDir = fresh_dir("engine");
    const fs::path project = pluginDir.parent_path() / (pluginDir.filename().string() + ".aurea");
    auto make_engine = [&](Engine& e) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        ec.pluginDirectory = str(pluginDir);
        return e.initialize(ec).ok();
    };
    auto capture = [](Engine& e) {
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        const Status s = e.capture_frame_rgba(64, rgba, w, h);
        AUREA_CHECK_MSG(s.ok(), "captura falhou");
        return rgba;
    };
    auto diff = [](const std::vector<u8>& a, const std::vector<u8>& b) {
        if (a.size() != b.size() || a.empty()) return 255;
        int m = 0;
        for (usize i = 0; i < a.size(); ++i) m = std::max(m, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
        return m;
    };
    const char* key = "plugin.com.aurea.examples.rgbwave";
    std::vector<u8> withEffect, withoutEffect;
    {
        Engine e;
        AUREA_CHECK(make_engine(e));
        if (!e.gpu()) { e.shutdown(); return; }
        const auto installed = e.install_plugin((examples() + "/rgb-wave.aureaplugin").c_str());
        AUREA_CHECK_MSG(installed.ok(), std::string(installed.status().detail()).c_str());
        AUREA_CHECK(installed.ok() && *installed == "com.aurea.examples.rgbwave");
        // Aparece no catálogo, na categoria Plugins.
        std::vector<bridge::EffectCatalogRow> rows(1024);
        std::vector<char> blob(1 << 20);
        const u32 n = e.query_effect_catalog(rows.data(), static_cast<u32>(rows.size()), blob.data(), static_cast<u32>(blob.size()));
        bool listed = false;
        for (u32 i = 0; i < n; ++i) {
            if (rows[i].typeId != effect_type_id(key)) continue;
            listed = std::string(blob.data() + rows[i].categoryOffset, rows[i].categoryLength) == "Plugins"
                  && std::string(blob.data() + rows[i].nameOffset, rows[i].nameLength) == "Onda RGB";
        }
        AUREA_CHECK_MSG(listed, "o plugin deve aparecer no catalogo em Plugins com o nome pt-BR");
        const std::string json = e.plugins_json();
        AUREA_CHECK(json.find("\"effectKey\":\"plugin.com.aurea.examples.rgbwave\"") != std::string::npos);
        AUREA_CHECK(json.find("\"en\":\"RGB Wave\"") != std::string::npos);
        AUREA_CHECK(json.find("\"ok\":true") != std::string::npos);

        AUREA_CHECK(e.new_project(64, 32, 30.0, "plugin").ok());
        const auto shape = e.add_shape(0);
        AUREA_CHECK(shape.ok());
        if (!shape.ok()) { e.shutdown(); return; }
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().root());
        Layer* l = comp->layer(LayerId::unpack(*shape));
        l->shape.bounds = Rect{0, 0, 32, 32};
        l->shape.fillColor = Vec4{1, 1, 1, 1};
        l->transform.anchor = Vec3{16, 16, 0};
        l->transform.position = Vec3{32, 16, 0};
        withoutEffect = capture(e);
        EffectInstance fx;
        fx.id = l->alloc_effect_id();
        fx.type = effect_type_id(key);
        initialize_instance(fx, *e.effects().params(fx.type));
        fx.params[0].constant = ParamValue::scalar(6.0f);
        fx.params[1].constant = ParamValue::scalar(0.0f);
        l->effects.push_back(fx);
        e.project()->mark_dirty();
        withEffect = capture(e);
        AUREA_CHECK_MSG(diff(withEffect, withoutEffect) > 100, "o efeito do plugin deve aparecer no quadro");
        AUREA_CHECK(e.save_project(str(project).c_str()).ok());

        // Reabrir COM o plugin: o mesmo quadro.
        AUREA_CHECK(e.load_project(str(project).c_str()).ok());
        AUREA_CHECK_MSG(diff(capture(e), withEffect) <= 1, "reaberto com o plugin = o mesmo quadro");

        // Remover: o catálogo esquece; reabrir mantém a instância e os valores.
        AUREA_CHECK(e.remove_plugin("com.aurea.examples.rgbwave"));
        AUREA_CHECK(e.list_plugins().empty());
        AUREA_CHECK(!e.effects().find(effect_type_id(key)));
        AUREA_CHECK(e.load_project(str(project).c_str()).ok());
        comp = e.project()->timeline().composition(e.project()->timeline().root());
        Layer* reopened = nullptr;
        comp->layers().for_each([&](LayerId, Layer& layer) { if (!layer.effects.empty()) reopened = &layer; });
        AUREA_CHECK(reopened != nullptr);
        if (reopened) {
            AUREA_CHECK_EQ(reopened->effects[0].type, effect_type_id(key));
            AUREA_CHECK_NEAR(reopened->effects[0].params[0].constant.v[0], 6.0f, 1e-6f);
            bridge::LayerEffectRow row[4];
            char names[256];
            u64 layerId = 0;
            comp->layers().for_each([&](LayerId lid, Layer& layer) { if (&layer == reopened) layerId = lid.pack(); });
            AUREA_CHECK_EQ(e.query_layer_effects(layerId, row, 4, names, sizeof(names)), 1u);
            AUREA_CHECK_EQ(row[0].known, 0u);
        }
        AUREA_CHECK_MSG(diff(capture(e), withoutEffect) <= 1, "sem o plugin: desenha sem o efeito, sem queda");
        // Salvar sem o plugin não perde nada.
        AUREA_CHECK(e.save_project(str(project).c_str()).ok());

        // Reinstalar: o efeito volta, com os valores do projeto.
        const auto again = e.install_plugin((examples() + "/rgb-wave.aureaplugin").c_str());
        AUREA_CHECK(again.ok());
        AUREA_CHECK_MSG(diff(capture(e), withEffect) <= 1, "reinstalado: o efeito volta igual");
        e.shutdown();
    }
    {
        // Outro motor com a mesma pasta: o plugin carrega na inicialização.
        Engine e;
        AUREA_CHECK(make_engine(e));
        const auto list = e.list_plugins();
        AUREA_CHECK_EQ(list.size(), static_cast<usize>(1));
        if (!list.empty()) AUREA_CHECK(list[0].ok && list[0].id == "com.aurea.examples.rgbwave");
        AUREA_CHECK(e.load_project(str(project).c_str()).ok());
        AUREA_CHECK_MSG(diff(capture(e), withEffect) <= 1, "motor novo: o plugin da pasta desenha o projeto");
        e.shutdown();
    }
    std::error_code ec;
    fs::remove(project, ec);
    fs::remove_all(pluginDir, ec);
}

AUREA_TEST(PluginsGpu, EveryExamplePluginRendersFiniteInTheCatalogPreview) {
    AUREA_REQUIRE_PLUGIN_GPU();
    for (const char* p : {"rgb-wave.aureaplugin", "soft-glow.aureaplugin", "dream-look.aureaplugin", "pulse-vignette.fs",
                          "target-3d.aureaplugin"})
        ensure_installed(p);
    const auto list = rig().manager->list();
    AUREA_CHECK(list.size() >= 4u);
    for (const plugins::PluginInfo& info : list) {
        AUREA_CHECK_MSG(info.ok, info.error.c_str());
        AUREA_CHECK_MSG(info.testRenderMs >= 0.0f, info.id.c_str());
        std::vector<u8> out;
        const Status s = rig().renderer.render_effect_preview(rig().effects, info.effectTypeId, 128, 96, out);
        AUREA_CHECK_MSG(s.ok(), info.id.c_str());
        AUREA_CHECK_EQ(out.size(), static_cast<usize>(128 * 96 * 4));
    }
}

#endif // AUREA_TEST_VULKAN
