#version 450
// =============================================================================
//  Aurea / shaders / scene3d / pbr / pbr.frag
//
//  PBR metal-rugosidade do glTF: GGX (Trowbridge-Reitz) + Smith height-
//  correlated + Fresnel de Schlick, difusa de Lambert. Luzes pontuais
//  (direcional, ponto, spot com queda do KHR_lights_punctual) e o ambiente.
//
//  Ambiente: com IBL (irradiância + especular pré-filtrado + LUT da BRDF),
//  ou — enquanto não há mapa — um céu/chão analítico com a aproximação de
//  BRDF de Karis. Nos dois casos um metal importado REFLETE alguma coisa: não
//  aparece preto só porque a cena não tem luz.
//
//  Texturas: base e emissiva chegam em sRGB e a GPU lineariza (formato
//  _SRGB); metal/rugosidade, normal e oclusão são dados lineares.
// =============================================================================
#include "../common/scene.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv0;
layout(location = 4) in vec2 v_uv1;
layout(location = 5) in vec4 v_color;

layout(location = 0) out vec4 o_color;

layout(set = 0, binding = TEX_BASE) uniform sampler2D t_base;
layout(set = 0, binding = TEX_MR) uniform sampler2D t_mr;
layout(set = 0, binding = TEX_NORMAL) uniform sampler2D t_normal;
layout(set = 0, binding = TEX_OCCLUSION) uniform sampler2D t_occlusion;
layout(set = 0, binding = TEX_EMISSIVE) uniform sampler2D t_emissive;
layout(set = 0, binding = TEX_IRRADIANCE) uniform samplerCube t_irradiance;
layout(set = 0, binding = TEX_PREFILTER) uniform samplerCube t_prefilter;
layout(set = 0, binding = TEX_BRDF) uniform sampler2D t_brdf;

vec2 uv_for(int slot) {
    int set = slot < 4 ? u.uvSet0[slot] : u.uvSet1.x;
    vec2 uv = set == 1 ? v_uv1 : v_uv0;
    vec4 x = u.uvXform[slot];
    float r = slot < 4 ? u.uvRot0[slot] : u.uvRot1.x;
    // KHR_texture_transform: escala, rotação, deslocamento (nessa ordem).
    uv *= x.zw;
    if (r != 0.0) {
        float c = cos(r), s = sin(r);
        uv = vec2(c * uv.x + s * uv.y, -s * uv.x + c * uv.y);
    }
    return uv + x.xy;
}

bool has_tex(int slot) { return (slot < 4 ? u.texMask0[slot] : u.texMask1.x) != 0; }

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float a) {
    float a2 = a * a;
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 F_Schlick(vec3 f0, float VdotH) {
    return f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0);
}

// Karis, "Physically Based Shading on Mobile" — ajuste analítico da LUT.
vec2 env_brdf_approx(float NdotV, float roughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// Ambiente analítico: gradiente céu → horizonte → chão. "Para cima" = −Y.
vec3 analytic_env(vec3 dir, float blur) {
    float up = -dir.y;
    vec3 horizon = mix(u.groundColor.rgb, u.skyColor.rgb, 0.5);
    float k = mix(0.25, 1.0, blur);
    vec3 c = up >= 0.0 ? mix(horizon, u.skyColor.rgb, pow(clamp(up, 0.0, 1.0), k))
                       : mix(horizon, u.groundColor.rgb, pow(clamp(-up, 0.0, 1.0), k));
    return c;
}

vec3 rotate_env(vec3 d) {
    float r = u.envParams.w;
    if (r == 0.0) return d;
    float c = cos(r), s = sin(r);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

// O cubemap do ambiente é "Y para cima" (convenção de HDRI); o mundo do Aurea é
// Y para baixo. Converte a direção antes de amostrar.
vec3 env_dir(vec3 worldDir) { return rotate_env(vec3(worldDir.x, -worldDir.y, -worldDir.z)); }

void main() {
    // --- Cor base e alfa -------------------------------------------------------
    vec4 base = u.baseColor * v_color;
    if (has_tex(0)) base *= texture(t_base, uv_for(0));
    int mode = int(u.alpha.y + 0.5);
    if (mode == 1 && base.a < u.alpha.x) discard;
    float alpha = mode == 2 ? base.a : 1.0;

    // --- Sem iluminação (KHR_materials_unlit) ------------------------------------
    if (u.alpha.z > 0.5) {
        vec3 c = base.rgb * u.cameraPos.w;
        o_color = vec4(c * alpha, alpha);
        return;
    }

    // --- Normal (mapa em espaço tangente, sinal da bitangente em w) ------------
    vec3 N = normalize(v_normal);
    vec3 T = v_tangent.xyz;
    bool twoSided = u.alpha.w > 0.5;
    if (twoSided && !gl_FrontFacing) {
        N = -N;
        T = -T;
    }
    if (has_tex(2) && dot(T, T) > 1e-12) {
        T = normalize(T - N * dot(N, T));
        vec3 B = cross(N, T) * (v_tangent.w < 0.0 ? -1.0 : 1.0);
        vec3 tn = texture(t_normal, uv_for(2)).xyz * 2.0 - 1.0;
        tn.xy *= u.mr.z;
        N = normalize(mat3(T, B, N) * tn);
    }
    vec3 V = normalize(u.cameraPos.xyz - v_world);
    float NdotV = clamp(dot(N, V), 1e-4, 1.0);

    // --- Metal / rugosidade --------------------------------------------------
    float metallic = u.mr.x;
    float roughness = u.mr.y;
    if (has_tex(1)) {
        vec4 mrs = texture(t_mr, uv_for(1));
        roughness *= mrs.g;
        metallic *= mrs.b;
    }
    roughness = clamp(roughness, 0.045, 1.0);   // abaixo disso o GGX some em fp16
    metallic = clamp(metallic, 0.0, 1.0);
    float a = roughness * roughness;
    vec3 diffuseColor = base.rgb * (1.0 - metallic);
    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);

    // --- Luzes pontuais ----------------------------------------------------------
    vec3 color = vec3(0.0);
    int count = int(u.lightCount.x + 0.5);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= count) break;
        int type = int(u.lightPos[i].w + 0.5);
        vec3 L;
        float atten = 1.0;
        if (type == 0) {
            L = normalize(u.lightPos[i].xyz);
        } else {
            vec3 d = u.lightPos[i].xyz - v_world;
            float dist2 = max(dot(d, d), 1e-4);
            L = d * inversesqrt(dist2);
            float range = u.lightColor[i].w;
            // Queda do KHR_lights_punctual: 1/d² com janela suave no alcance.
            atten = 1.0 / dist2;
            if (range > 0.0) {
                float r = sqrt(dist2) / range;
                float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
                atten *= w * w;
            }
            if (type == 2) {
                float cd = dot(-L, normalize(u.lightSpot[i].xyz));
                float outer = u.lightSpot[i].w, inner = u.lightSpot2[i].x;
                float t = clamp((cd - outer) / max(inner - outer, 1e-4), 0.0, 1.0);
                atten *= t * t;
            }
        }
        float NdotL = clamp(dot(N, L), 0.0, 1.0);
        if (NdotL <= 0.0 || atten <= 0.0) continue;
        vec3 H = normalize(L + V);
        float NdotH = clamp(dot(N, H), 0.0, 1.0);
        float VdotH = clamp(dot(V, H), 0.0, 1.0);
        vec3 F = F_Schlick(f0, VdotH);
        vec3 spec = F * (D_GGX(NdotH, a) * V_SmithGGXCorrelated(NdotV, NdotL, a));
        vec3 diff = (1.0 - F) * diffuseColor / PI;
        color += (diff + spec) * u.lightColor[i].rgb * (NdotL * atten);
    }

    // --- Ambiente --------------------------------------------------------------
    float ao = 1.0;
    if (has_tex(3)) ao = mix(1.0, texture(t_occlusion, uv_for(3)).r, u.mr.w);
    vec3 R = reflect(-V, N);
    vec3 ambient;
    if (u.envParams.y > 0.5) {
        vec3 irradiance = texture(t_irradiance, env_dir(N)).rgb;
        float lod = roughness * max(u.envParams.z - 1.0, 0.0);
        vec3 prefiltered = textureLod(t_prefilter, env_dir(R), lod).rgb;
        vec2 brdf = texture(t_brdf, vec2(NdotV, roughness)).rg;
        vec3 Fr = max(vec3(1.0 - roughness), f0) - f0;
        vec3 kS = f0 + Fr * pow(1.0 - NdotV, 5.0);
        vec3 specAmb = prefiltered * (kS * brdf.x + brdf.y);
        vec3 diffAmb = irradiance * diffuseColor * (1.0 - kS);
        ambient = diffAmb + specAmb;
    } else {
        vec2 brdf = env_brdf_approx(NdotV, roughness);
        vec3 specAmb = analytic_env(R, roughness) * (f0 * brdf.x + brdf.y);
        vec3 diffAmb = analytic_env(N, 1.0) * diffuseColor;
        ambient = diffAmb + specAmb;
    }
    color += ambient * ao * u.envParams.x;

    // --- Emissiva ----------------------------------------------------------------
    vec3 emissive = u.emissive.rgb;
    if (has_tex(4)) emissive *= texture(t_emissive, uv_for(4)).rgb;
    color += emissive;

    color *= u.cameraPos.w;
    o_color = vec4(color * alpha, alpha);
}
