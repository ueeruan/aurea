// =============================================================================
//  O cabeçalho GLSL dos plugins "shader" e a compilação de cada passe.
//
//  O autor escreve só `vec4 aurea_main(vec2 uv)`. O motor junta três trechos,
//  cada um com o próprio nome nas mensagens de erro:
//
//    "aurea_header"   #version 450, bindings, samplers, o bloco de uniforms,
//                     uma macro por parâmetro e as funções de apoio;
//    "<arquivo>"      o arquivo do autor (as linhas dele numeram de 1);
//    "aurea_main"     void main(){ o_color = aurea_main(v_uv); } (+ conversão
//                     de volta do sRGB no último passe, e NaN vira 0).
//
//  ESTE CABEÇALHO É CONTRATO. Mudar um nome ou o layout do bloco quebra
//  plugin publicado; acrescentar símbolo novo é seguro. A referência de cada
//  símbolo está em docs/plugins/README.md ("A interface do shader").
// =============================================================================
#include "PluginInternal.hpp"

#include <array>
#include <cstdio>

namespace aurea::plugins {

namespace {

// As funções de apoio (escritas para o motor; nenhuma vem de outro app).
constexpr const char* kHelpers = R"GLSL(
const float AUREA_PI = 3.14159265358979323846;
float aurea_srgb_to_linear(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float aurea_linear_to_srgb(float c) { c = max(c, 0.0); return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }
vec3 aurea_srgb_to_linear(vec3 c) { return vec3(aurea_srgb_to_linear(c.r), aurea_srgb_to_linear(c.g), aurea_srgb_to_linear(c.b)); }
vec3 aurea_linear_to_srgb(vec3 c) { return vec3(aurea_linear_to_srgb(c.r), aurea_linear_to_srgb(c.g), aurea_linear_to_srgb(c.b)); }
float aurea_luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec4 aurea_unpremultiply(vec4 c) { return c.a > 1e-5 ? vec4(c.rgb / c.a, c.a) : vec4(0.0); }
vec4 aurea_premultiply(vec4 c) { return vec4(c.rgb * c.a, c.a); }
float aurea_hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
vec2 aurea_hash2(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.xx + q.yz) * q.zy);
}
float aurea_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = aurea_hash(i), b = aurea_hash(i + vec2(1.0, 0.0));
    float c = aurea_hash(i + vec2(0.0, 1.0)), d = aurea_hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float aurea_fbm(vec2 p, int octaves) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    for (int k = 0; k < 8; ++k) {
        if (k >= octaves) break;
        sum += amp * aurea_noise(p);
        norm += amp;
        p = p * 2.03 + vec2(17.1, 9.7);
        amp *= 0.5;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}
vec2 aurea_px_to_uv(vec2 px) { return px * aurea.layer.zw * aurea.resolution.zw; }
vec2 aurea_uv_to_px(vec2 uv) { return uv / max(aurea.layer.zw * aurea.resolution.zw, vec2(1e-8)); }
vec2 aurea_layer_uv(vec2 uv) { return (aurea.region.xy + uv * aurea.region.zw) / max(aurea.layer.xy, vec2(1.0)); }
vec2 aurea_uv_from_layer(vec2 layerUv) { return (layerUv * aurea.layer.xy - aurea.region.xy) / max(aurea.region.zw, vec2(1e-6)); }
vec4 aurea_main(vec2 uv);
)GLSL";

// 0.3.1, plugins "shader3d": a câmera da cena e as camadas ligadas (nulo,
// câmera, luz). Só entram no cabeçalho dos plugins 3D: o dos outros não muda.
constexpr const char* kHelpers3D = R"GLSL(
bool aurea_layer_valid(int i) { return aurea.layerPos[i].w > 0.5; }
mat4 aurea_layer_matrix(int i) { return aurea.layerWorld[i]; }
vec3 aurea_layer_position(int i) { return aurea.layerPos[i].xyz; }
vec3 aurea_layer_axis(int i, int a) { return normalize(aurea.layerWorld[i][a].xyz); }
int aurea_layer_kind(int i) { return int(aurea.layerInfo[i].x + 0.5); }
float aurea_layer_fov(int i) { return aurea.layerInfo[i].y; }
vec3 aurea_camera_position() { return aurea.camera.xyz; }
vec3 aurea_camera_right() { return vec3(aurea.view[0].x, aurea.view[1].x, aurea.view[2].x); }
vec3 aurea_camera_down() { return vec3(aurea.view[0].y, aurea.view[1].y, aurea.view[2].y); }
vec3 aurea_camera_forward() { return vec3(aurea.view[0].z, aurea.view[1].z, aurea.view[2].z); }
vec2 aurea_comp_px(vec2 uv) {
    vec4 q = aurea.compFromLayer * vec4(aurea.region.xy + uv * aurea.region.zw, 0.0, 1.0);
    return q.xy / (abs(q.w) > 1e-6 ? q.w : 1.0);
}
void aurea_ray(vec2 uv, out vec3 ro, out vec3 rd) {
    vec2 n = aurea_comp_px(uv) * 2.0 * aurea.comp.zw - 1.0;
    float t = tan(aurea.lens.x * 0.5);
    ro = aurea.camera.xyz;
    rd = normalize(aurea_camera_right() * (n.x * aurea.lens.z * t) + aurea_camera_down() * (n.y * t) + aurea_camera_forward());
}
vec3 aurea_world_to_comp(vec3 p) {
    vec4 q = aurea.compFromWorld * vec4(p, 1.0);
    return vec3(q.xy / (abs(q.w) > 1e-6 ? q.w : 1e-6), q.w);
}
vec3 aurea_world_to_view(vec3 p) { return (aurea.view * vec4(p, 1.0)).xyz; }
)GLSL";

std::string param_macro(const ParamDecl& p, u32 slot) {
    const std::string ref = "aurea.params[" + std::to_string(slot) + "]";
    switch (p.kind) {
        case ParamKind::Color: return ref;
        case ParamKind::Point: return "(" + ref + ".xy)";
        default: return "(" + ref + ".x)";
    }
}

} // namespace

std::string shader_header(const Manifest& m, u32 pass) {
    const PassDecl& p = m.passes[pass];
    std::string h;
    h.reserve(4096);
    h += "#version 450\n";
    h += "// Cabecalho do motor AUREA para plugins (formato 1). Gerado; o autor nao o edita.\n";
    h += "#define AUREA_PLUGIN 1\n";
    for (u32 i = 0; i < 12; ++i) h += "#define AUREA_TEX" + std::to_string(i) + " " + std::to_string(i) + "\n";
    h += "#define AUREA_PARAMS 12\n#define AUREA_IMG0 13\n#define AUREA_IMG1 14\n#define AUREA_DATA 15\n#define AUREA_DATA1 16\n";
    h += "#define AUREA_PASS_INDEX " + std::to_string(pass) + "\n";
    h += "#define AUREA_PASS_COUNT " + std::to_string(m.passes.size()) + "\n";
    h += std::string("#define AUREA_COLOR_SRGB ") + (m.colorSpace == ColorSpace::Srgb ? "1" : "0") + "\n";
    h += "layout(location = 0) in vec2 v_uv;\n";
    h += "layout(location = 0) out vec4 o_color;\n";
    h += "layout(set = 0, binding = 0) uniform sampler2D aurea_source;\n";
    for (usize i = 0; i < p.inputs.size(); ++i)
        h += "layout(set = 0, binding = " + std::to_string(i + 1) + ") uniform sampler2D " + p.inputs[i].first + ";\n";
    h += "layout(set = 0, binding = 12, std140) uniform AureaPluginBlock {\n"
         "    vec4 resolution;   // xy = texels deste passe, zw = 1/xy\n"
         "    vec4 time;         // x = segundos, y = quadro, z = fps\n"
         "    vec4 layer;        // xy = tamanho da camada (px), zw = texels por px da camada neste passe\n"
         "    vec4 region;       // xy = origem da regiao (px da camada), zw = tamanho (px da camada)\n"
         "    vec4 params[16];   // um vec4 por parametro, na ordem de plugin.json\n";
    if (m.scene3d) {
        h += "    mat4 view;          // vista <- mundo (x direita, y baixo, z frente)\n"
             "    mat4 compFromWorld; // px da composicao (homogeneo) <- mundo\n"
             "    mat4 compFromLayer; // px da composicao (homogeneo) <- px da camada\n"
             "    vec4 camera;        // xyz posicao no mundo, w = 1 camera de camada\n"
             "    vec4 lens;          // x fovY (rad), y plano proximo, z aspecto, w focal (px)\n"
             "    vec4 comp;          // xy tamanho da composicao (px), zw = 1/xy\n"
             "    mat4 layerWorld[4]; // mundo <- px de cada camada ligada\n"
             "    vec4 layerPos[4];   // xyz ancora no mundo, w = 1 ligada\n"
             "    vec4 layerInfo[4];  // x tipo (6 nulo, 8 camera, 9 luz), y fovY, z 3D\n";
    }
    h += "} aurea;\n";
    u32 layerSlot = 0;
    for (u32 i = 0; i < m.params.size(); ++i) {
        // "layer": o nome vira a POSIÇÃO da camada ligada (aurea_layer_*(nome)).
        if (m.params[i].kind == ParamKind::Layer) h += "#define " + m.params[i].id + " " + std::to_string(layerSlot++) + "\n";
        else h += "#define " + m.params[i].id + " " + param_macro(m.params[i], i) + "\n";
    }
    if (m.scene3d) h += "#define AUREA_SCENE3D 1\n";
    h += kHelpers;
    if (m.scene3d) h += kHelpers3D;
    return h;
}

std::string shader_footer(const Manifest& m, u32 pass) {
    const bool last = pass + 1 == m.passes.size();
    std::string f = "\nvoid main() {\n    vec4 c = aurea_main(v_uv);\n";
    // Um NaN/inf do autor não contamina a composição inteira.
    f += "    if (any(isnan(c)) || any(isinf(c))) c = vec4(0.0);\n";
    if (last && m.colorSpace == ColorSpace::Srgb) {
        // sRGB de alfa reto (o que o shader viu) → linear pré-multiplicado do motor.
        f += "    c = clamp(c, 0.0, 1.0);\n";
        f += "    c = vec4(aurea_srgb_to_linear(c.rgb) * c.a, c.a);\n";
    }
    f += "    o_color = c;\n}\n";
    return f;
}

bool compile_pass(const Manifest& m, u32 pass, std::string_view userSource, std::vector<u32>& spirv,
                  std::string& error) {
    const std::string header = shader_header(m, pass);
    const std::string footer = shader_footer(m, pass);
    // O arquivo do autor termina sem quebra de linha: o main não pode colar nele.
    std::string user(userSource);
    if (user.empty() || user.back() != '\n') user += '\n';
    const runtime_shaders::SourcePart parts[] = {
        {"aurea_header", header},
        {m.passes[pass].shader, user},
        {"aurea_main", footer},
    };
    return runtime_shaders::compile_glsl(ShaderStage::Fragment, parts, spirv, error);
}

} // namespace aurea::plugins
