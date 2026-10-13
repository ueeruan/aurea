#version 450
// =============================================================================
//  Aurea / shaders / scene3d / plane_tile.frag
//
//  Camada 2D no espaço 3D desenhada DENTRO da cena: a imagem da camada (com
//  efeitos, pré-multiplicada, linear) num plano com teste e escrita de
//  profundidade — ela passa por trás e pela frente dos modelos e das outras
//  camadas 3D de verdade, e se cruza com elas. Transparente não escreve
//  profundidade (não tapa o que está atrás).
//
//  LUZES ("aceita luzes" do After Effects): com luz na composição, a cor da
//  camada é multiplicada pelo Lambert da normal do plano (N·L) de cada luz —
//  cor × intensidade, janela do alcance, cone do spot — mais as luzes
//  ambiente. A face iluminada é a que a câmera vê (luz por trás = escuro).
//  Sem luz nenhuma (cameraPos.w = 0) a camada sai como no 2D.
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;   // x = opacidade; y = 0 opacos com depth, 1 transparência sem escrita de depth
} pc;

layout(location = 0) in vec3 v_clip;
// MRT do passe 3D: a camada é cor de EXIBIÇÃO (alvo 0) — não passa pelo tone
// map nem pelo bloom da cena, sai exatamente como no 2D; no alvo HDR (1) ela
// só tapa, pelo alfa, a cena que está atrás.
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_scene;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tex2;
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_tex3;
layout(set = 0, binding = AUREA_TEX4) uniform sampler2D u_tex4;
layout(set = 0, binding = AUREA_TEX5) uniform sampler2D u_tex5;
layout(set = 0, binding = AUREA_TEX6) uniform sampler2D u_tex6;
layout(set = 0, binding = AUREA_TEX7) uniform sampler2D u_tex7;
layout(set = 0, binding = AUREA_TEX8) uniform sampler2D u_tex8;
layout(set = 0, binding = AUREA_TEX9) uniform sampler2D u_tex9;
layout(set = 0, binding = AUREA_TEX10) uniform sampler2D u_tex10;
layout(set = 0, binding = AUREA_TEX11) uniform sampler2D u_tex11;

#define PLANE_MAX_LIGHTS 8
// Espelho de scene3d::PlaneLightBlock (SceneRenderer.hpp).
layout(set = 0, binding = AUREA_PARAMS, std140) uniform PlaneLights {
    mat4 worldFromLayer;                 // mundo (px) ← px da camada
    vec4 cameraPos;                      // xyz = câmera; w = 1 iluminado, 0 sem luz
    vec4 ambient;                        // rgb = luzes ambiente somadas
    vec4 lightCount;                     // x = luzes ativas
    vec4 lightPos[PLANE_MAX_LIGHTS];     // xyz = posição ou direção PARA a luz; w = tipo (0 dir, 1 ponto, 2 spot)
    vec4 lightColor[PLANE_MAX_LIGHTS];   // rgb × intensidade; w = alcance (0 = infinito)
    vec4 lightSpot[PLANE_MAX_LIGHTS];    // xyz = direção do feixe; w = cos do cone externo
    vec4 lightSpot2[PLANE_MAX_LIGHTS];   // x = cos do cone interno
    vec4 periodicInverseX; vec4 periodicInverseY; vec4 periodicInverseW;
    vec4 periodicBox; vec4 periodicTile; vec4 periodicFlags; vec4 periodicMips; vec4 periodicWindow;
} u;

vec3 plane_light(vec2 layerPos) {
    // Posição no mundo: o mesmo ponto que o layer.vert projeta (uvRect = 0..1).

    vec3 P = (u.worldFromLayer * vec4(layerPos, 0.0, 1.0)).xyz;
    vec3 n = cross(u.worldFromLayer[0].xyz, u.worldFromLayer[1].xyz);
    vec3 toCam = u.cameraPos.xyz - P;
    vec3 N = dot(n, n) > 1e-20 ? normalize(n) : normalize(toCam + vec3(0.0, 0.0, -1e-6));
    if (dot(N, toCam) < 0.0) N = -N;   // a face vista
    vec3 sum = u.ambient.rgb;
    int count = int(u.lightCount.x + 0.5);
    for (int i = 0; i < PLANE_MAX_LIGHTS; ++i) {
        if (i >= count) break;
        int type = int(u.lightPos[i].w + 0.5);
        vec3 L;
        float atten = 1.0;
        if (type == 0) {
            L = normalize(u.lightPos[i].xyz);
        } else {
            vec3 d = u.lightPos[i].xyz - P;
            float dist = max(length(d), 1e-3);
            L = d / dist;
            float range = u.lightColor[i].w;
            if (range > 0.0) {
                // A janela suave do alcance (a mesma dos modelos), sem 1/d².
                float r = dist / range;
                float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
                atten = w * w;
            }
            if (type == 2) {
                float cd = dot(-L, normalize(u.lightSpot[i].xyz));
                float outer = u.lightSpot[i].w, inner = u.lightSpot2[i].x;
                float t = clamp((cd - outer) / max(inner - outer, 1e-4), 0.0, 1.0);
                atten *= t * t;
            }
        }
        sum += u.lightColor[i].rgb * (max(dot(N, L), 0.0) * atten);
    }
    return sum;
}

// Opaque samplers are always accessed through explicit constant bindings.
// No nonuniform sampler array/indexing extension is required on ES3/Metal.
vec4 source_read(sampler2D source, vec2 local, bool minified) {
    vec2 inset = 0.5 / vec2(textureSize(source, 0));
    // Magnification retains the original half-texel lock. Minified repeat
    // samples cross the periodic seam; mirrored tiles retain their edge.
    if (u.periodicFlags.x > 0.5 || !minified) local = clamp(local, inset, 1.0 - inset);
    return textureLod(source, local, 0.0);
}
vec4 mip_read(int level, vec2 local, bool minified) {
    if (level <= 0) return source_read(u_tex0, local, minified);
    if (level == 1) return source_read(u_tex1, local, minified);
    if (level == 2) return source_read(u_tex2, local, minified);
    if (level == 3) return source_read(u_tex3, local, minified);
    if (level == 4) return source_read(u_tex4, local, minified);
    if (level == 5) return source_read(u_tex5, local, minified);
    if (level == 6) return source_read(u_tex6, local, minified);
    if (level == 7) return source_read(u_tex7, local, minified);
    if (level == 8) return source_read(u_tex8, local, minified);
    if (level == 9) return source_read(u_tex9, local, minified);
    if (level == 10) return source_read(u_tex10, local, minified);
    return source_read(u_tex11, local, minified);
}
float phase_turns() {
    float period = u.periodicFlags.x > 0.5 ? 2.0 : 1.0;
    return mod(u.periodicFlags.y + period * 0.5, period) - period * 0.5;
}
vec2 tile_lookup(vec2 source) {
    vec2 cell = (source - u.periodicTile.zw) / u.periodicTile.xy;
    if (u.periodicFlags.w > 0.5) return clamp(cell + 0.5, 0.0, 1.0);
    float phase = phase_turns();
    if (u.periodicFlags.z > 0.5) cell.y += phase * floor(cell.x + 0.5);
    else cell.x += phase * floor(cell.y + 0.5);
    vec2 index = floor(cell + 0.5);
    vec2 within = cell - index;
    if (u.periodicFlags.x > 0.5) within = mix(within, -within, step(0.5, mod(abs(index), 2.0)));
    return within + 0.5;
}
vec4 tile_lod(vec2 source, float lod, bool minified) {
    vec2 local = tile_lookup(source);
    // Sources up to2048 retain every level. Larger bounded sources keep the
    // finest11 levels and the1x1 mean in the last hardware texture slot.
    float index = lod;
    if (u.periodicMips.w >= 12.0 && u.periodicMips.z > 11.0 && lod > 10.0)
        index = 10.0 + (lod - 10.0) / (u.periodicMips.z - 10.0);
    int first = int(floor(index));
    int second = min(first + 1, int(u.periodicMips.w) - 1);
    return mix(mip_read(first, local, minified), mip_read(second, local, minified), fract(index));
}
vec4 filtered_tile(vec2 source, vec2 dx, vec2 dy) {
    vec2 gx = clamp(dx / u.periodicTile.xy, -1e10, 1e10);
    vec2 gy = clamp(dy / u.periodicTile.xy, -1e10, 1e10);
    float phase = phase_turns();
    if (u.periodicFlags.z > 0.5) { gx.y += phase * gx.x; gy.y += phase * gy.x; }
    else { gx.x += phase * gx.y; gy.x += phase * gy.y; }
    float lx = length(gx * u.periodicMips.xy), ly = length(gy * u.periodicMips.xy);
    float major = max(lx, ly), minor = max(1.0, min(lx, ly));
    // build_source may already reduce128 logical pixels to16 working texels.
    // A1-texel working footprint can still cover8 source pixels: choose the
    // periodic seam rule in either domain, while LOD stays in working texels.
    float logicalMajor = max(length(gx * u.periodicBox.zw), length(gy * u.periodicBox.zw));
    bool minified = major > 1.0 || logicalMajor > 1.0;
    int taps = int(clamp(ceil(major / minor), 1.0, 8.0));
    float lod = clamp(log2(max(1.0, max(major / float(taps), minor))), 0.0, u.periodicMips.z);
    vec2 along = lx >= ly ? dx : dy;
    vec4 result = vec4(0.0);
    for (int n = 0; n < 8; ++n) {
        if (n >= taps) break;
        float offset = (float(n) + 0.5) / float(taps) - 0.5;
        result += tile_lod(source + along * offset, lod, minified);
    }
    return result / float(taps);
}
void main() {
    vec2 ndc = v_clip.xy / v_clip.z;
    vec3 q = vec3(ndc, 1.0);
    vec3 local = vec3(dot(u.periodicInverseX.xyz, q), dot(u.periodicInverseY.xyz, q), dot(u.periodicInverseW.xyz, q));
    float denominator = abs(local.z) >= 1e-20 ? local.z : (local.z < 0.0 ? -1e-20 : 1e-20);
    vec2 layerPos = local.xy / denominator;
    vec2 source = (layerPos - u.periodicBox.xy) / u.periodicBox.zw;
    // Derivatives are resolved for the whole quad before divergent frustum
    // discards. Neighboring rays behind the camera cannot invalidate the filter.
    vec2 dx = dFdx(source), dy = dFdy(source);
    vec4 physical = pc.clipFromLayer * vec4(layerPos, 0.0, 1.0);
    if (physical.w <= 0.0 || physical.z < 0.0 || physical.z > physical.w
        || abs(ndc.x) > u.periodicWindow.x || abs(ndc.y) > u.periodicWindow.y) discard;
    gl_FragDepth = clamp(physical.z / physical.w, 0.0, 1.0);
    vec4 c = filtered_tile(source, dx, dy) * pc.params.x;
    if (u.cameraPos.w > 0.5) c.rgb *= plane_light(layerPos);
    if (pc.params.y < 0.5) { if (c.a < 0.999) discard; }
    else { if (c.a <= 0.000001 || c.a >= 0.999) discard; }
    o_color = c;
    o_scene = vec4(0.0, 0.0, 0.0, c.a);
}
