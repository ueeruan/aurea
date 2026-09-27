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
#include "../common/hdr.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv0;
layout(location = 4) in vec2 v_uv1;
layout(location = 5) in vec4 v_color;

// MRT do passe 3D (ver common/hdr.glsl): 0 = 2D de exibição — aqui só o
// alfa, que tapa planos/partículas atrás; 1 = a cena, linear HDR sem teto.
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_scene;

layout(set = 0, binding = TEX_BASE) uniform sampler2D t_base;
layout(set = 0, binding = TEX_MR) uniform sampler2D t_mr;
layout(set = 0, binding = TEX_NORMAL) uniform sampler2D t_normal;
layout(set = 0, binding = TEX_OCCLUSION) uniform sampler2D t_occlusion;
layout(set = 0, binding = TEX_EMISSIVE) uniform sampler2D t_emissive;
layout(set = 0, binding = TEX_IRRADIANCE) uniform samplerCube t_irradiance;
layout(set = 0, binding = TEX_PREFILTER) uniform samplerCube t_prefilter;
layout(set = 0, binding = TEX_BRDF) uniform sampler2D t_brdf;
layout(set = 0, binding = TEX_SHADOW) uniform sampler2DShadow t_shadow;
layout(set = 0, binding = TEX_SHADOW_DEPTH) uniform sampler2D t_shadowDepth;

#include "../common/shadow.glsl"

// Sombra da luz principal: PCSS (common/shadow.glsl). 1 = iluminado, 0 = na sombra.
float shadow_factor(vec3 world, vec3 Ng, vec3 L) {
    return aurea_shadow(t_shadow, t_shadowDepth, u.shadowMatrix, u.shadowParams, u.shadowParams2,
                        world, Ng, L, gl_FragCoord.xy);
}

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

vec3 F_Schlick90(vec3 f0, float f90, float VdotH) {
    return f0 + (vec3(f90) - f0) * pow(1.0 - VdotH, 5.0);
}

// Visibilidade de Kelemen para o verniz (camada fina e lisa, barata).
float V_Kelemen(float LdotH) {
    return 0.25 / max(LdotH * LdotH, 1e-4);
}

// AA especular geométrico (Tokuyoshi & Kaplanyan): onde a normal muda rápido
// dentro do pixel, a rugosidade sobe o bastante para o brilho não virar
// cintilação/serrilhado (quinas, curvas finas, malha densa ao longe).
float specular_aa(vec3 n, float a) {
    vec3 du = dFdx(n), dv = dFdy(n);
    float variance = 0.25 * (dot(du, du) + dot(dv, dv));
    float kernel = min(2.0 * variance, 0.18);
    return sqrt(clamp(a * a + kernel, 0.0, 1.0));
}

// Oclusão especular (Lagarde): o AO da textura escurece o reflexo só onde a
// cavidade o bloquearia, sem apagar o brilho de superfícies abertas.
float specular_occlusion(float NdotV, float ao, float roughness) {
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
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

// Mapeamento rugosidade → mip do especular pré-filtrado (dono: construção do IBL).
// Rugosidade perceptual → LOD (Frostbite/Unity): lod = (mips−1)·r·(1,7−0,7r).
// Os mips são gerados com a inversa (Environment.cpp, specular_lod).
float env_lod(float roughness) { float r = clamp(roughness, 0.0, 1.0); return max(u.envParams.z - 1.0, 0.0) * r * (1.7 - 0.7 * r); }
vec3 sample_prefilter(vec3 worldDir, float roughness) {
    return textureLod(t_prefilter, env_dir(worldDir), env_lod(roughness)).rgb;
}

// Tone mapping "PBR Neutral" (Khronos): leva a cena (linear, sem teto) para o
// espaço de trabalho do compositor (linear, 0..1) preservando a cor base até
// ~0,76 e comprimindo só os realces. É a transformação de SAÍDA do grupo 3D —
// aplicada uma vez, aqui; o 2D nunca passa por ela.
vec3 pbr_neutral(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

void main() {
    // --- Cor base e alfa -------------------------------------------------------
    vec4 base = u.baseColor * v_color;
    if (has_tex(0)) base *= texture(t_base, uv_for(0));
    int mode = int(u.alpha.y + 0.5);
    // Modo 3 = MASK com MSAA: recorte por COBERTURA (alpha-to-coverage +
    // alpha-to-one no pipeline). O alfa vira uma rampa de ~1 pixel em volta do
    // corte (derivada de tela) e as amostras do pixel fazem o degradê — folha,
    // grade e cabelo sem escada. Sem MSAA (modo 1), o descarte de sempre.
    float coverage = 1.0;
    if (mode == 3) {
        coverage = clamp((base.a - u.alpha.x) / max(fwidth(base.a), 1e-4) + 0.5, 0.0, 1.0);
        if (coverage <= 0.0) discard;
    } else if (mode == 1 && base.a < u.alpha.x) discard;
    float alpha = mode == 2 ? base.a : 1.0;

    // --- Sem iluminação (KHR_materials_unlit) ------------------------------------
    if (u.alpha.z > 0.5) {
        // Unlit é cor de EXIBIÇÃO (texto, logotipo): vai no alvo 2D, fora do
        // tone map e do bloom — a cor escolhida é a cor que aparece.
        vec3 c = base.rgb * u.cameraPos.w;
        o_color = vec4(c * alpha, mode == 3 ? coverage : alpha);
        o_scene = vec4(0.0, 0.0, 0.0, mode == 3 ? coverage : alpha);
        return;
    }

    // --- Normal (mapa em espaço tangente, sinal da bitangente em w) ------------
    vec3 N = normalize(v_normal);
    vec3 T = v_tangent.xyz;
    bool twoSided = u.alpha.w > 0.5;
    bool back = twoSided && !gl_FrontFacing;
    if (back) {
        N = -N;
        T = -T;
    }
    // Normal geométrica: o verniz é liso por cima do relevo (pintura de carro).
    vec3 Ng = N;
    if (has_tex(2) && dot(T, T) > 1e-12) {
        T = normalize(T - N * dot(N, T));
        // Face de trás: N, T E B invertem juntos (antes o verde do mapa invertia).
        vec3 B = cross(N, T) * (v_tangent.w < 0.0 ? -1.0 : 1.0) * (back ? -1.0 : 1.0);
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
    // AA especular: a rugosidade efetiva cobre a variação da normal no pixel.
    float a = specular_aa(N, roughness * roughness);
    roughness = sqrt(a);
    vec3 diffuseColor = base.rgb * (1.0 - metallic);
    // F0 do dielétrico pelo IOR (1,5 → 0,04 como antes) e KHR_materials_specular.
    float iorF0 = (u.clearcoat.z - 1.0) / (u.clearcoat.z + 1.0);
    float specStrength = u.clearcoat.w;
    vec3 dielectricF0 = min(vec3(iorF0 * iorF0) * u.specularColor.rgb, vec3(1.0)) * specStrength;
    vec3 f0 = mix(dielectricF0, base.rgb, metallic);
    float f90 = mix(specStrength, 1.0, metallic);
    // Verniz (KHR_materials_clearcoat).
    float cc = u.clearcoat.x;
    float ccRough = clamp(u.clearcoat.y, 0.045, 1.0);
    float ccA = specular_aa(Ng, ccRough * ccRough);
    ccRough = sqrt(ccA);
    float NcdotV = clamp(dot(Ng, V), 1e-4, 1.0);
    float ccFv = cc > 0.0 ? cc * (0.04 + 0.96 * pow(1.0 - NcdotV, 5.0)) : 0.0;
    // Transmissão fina (vidro): o que passa pela superfície não é difuso.
    float transmission = u.specularColor.w;
    diffuseColor *= 1.0 - transmission;
    // Compensação de energia multi-espalhamento (Fdez-Agüera): metal rugoso
    // deixa de escurecer por perder a luz que quica entre as microfacetas.
    vec2 dfg = texture(t_brdf, vec2(NdotV, roughness)).rg;
    float Ess = max(dfg.x + dfg.y, 1e-3);
    vec3 energyComp = 1.0 + f0 * (1.0 / Ess - 1.0);

    // --- Luzes pontuais ----------------------------------------------------------
    // Difuso e especular separados: na transparência só o difuso é coberto.
    vec3 colorD = vec3(0.0), colorS = vec3(0.0);
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
        vec3 F = F_Schlick90(f0, f90, VdotH);
        vec3 spec = F * (D_GGX(NdotH, a) * V_SmithGGXCorrelated(NdotV, NdotL, a)) * energyComp;
        vec3 diff = (1.0 - F) * diffuseColor / PI;
        float base_ = 1.0;
        vec3 ccLobe = vec3(0.0);
        if (cc > 0.0) {
            // Verniz por cima: brilho próprio e atenuação da base pelo seu Fresnel.
            float NcdotL = clamp(dot(Ng, L), 0.0, 1.0);
            float NcdotH = clamp(dot(Ng, H), 0.0, 1.0);
            float Fc = cc * (0.04 + 0.96 * pow(1.0 - VdotH, 5.0));
            base_ = 1.0 - Fc;
            ccLobe = vec3(D_GGX(NcdotH, ccA) * V_Kelemen(VdotH) * Fc * NcdotL / max(NdotL, 1e-4));
        }
        float sh = i == int(u.shadowParams.w + 0.5) ? shadow_factor(v_world, Ng, L) : 1.0;
        vec3 radiance = u.lightColor[i].rgb * (NdotL * atten * sh);
        colorD += diff * base_ * radiance;
        colorS += (spec * base_ + ccLobe) * radiance;
    }

    // --- Ambiente --------------------------------------------------------------
    float ao = 1.0;
    if (has_tex(3)) ao = mix(1.0, texture(t_occlusion, uv_for(3)).r, u.mr.w);
    vec3 R = reflect(-V, N);
    float specAO = specular_occlusion(NdotV, ao, roughness);
    vec3 diffAmb, specAmb, ccAmb = vec3(0.0);
    if (u.envParams.y > 0.5) {
        vec3 irradiance = texture(t_irradiance, env_dir(N)).rgb;
        vec3 prefiltered = sample_prefilter(R, roughness);
        // Split-sum com Fresnel dependente da rugosidade + multi-espalhamento.
        vec3 Fr = max(vec3(1.0 - roughness), f0) - f0;
        vec3 kS = f0 + Fr * pow(1.0 - NdotV, 5.0);
        vec3 FssEss = kS * dfg.x + dfg.y * f90;
        float Ems = 1.0 - Ess;
        vec3 Favg = f0 + (1.0 - f0) / 21.0;
        vec3 Fms = FssEss * Favg / (1.0 - Ems * Favg);
        specAmb = prefiltered * FssEss * specAO + Fms * Ems * irradiance * ao;
        diffAmb = irradiance * diffuseColor * (1.0 - FssEss - Fms * Ems) * ao;
        if (cc > 0.0) {
            vec3 Rc = reflect(-V, Ng);
            vec2 dfgc = texture(t_brdf, vec2(NcdotV, ccRough)).rg;
            ccAmb = sample_prefilter(Rc, ccRough) * (cc * (0.04 * dfgc.x + dfgc.y)) * specAO;
        }
    } else {
        vec2 brdf = env_brdf_approx(NdotV, roughness);
        specAmb = analytic_env(R, roughness) * (f0 * brdf.x + brdf.y * f90) * specAO;
        diffAmb = analytic_env(N, 1.0) * diffuseColor * ao;
        if (cc > 0.0) {
            vec2 brdfc = env_brdf_approx(NcdotV, ccRough);
            ccAmb = analytic_env(reflect(-V, Ng), ccRough) * (cc * (0.04 * brdfc.x + brdfc.y)) * specAO;
        }
    }
    // O verniz reflete por cima e tira da base o que ele mesmo reflete.
    colorD += diffAmb * (1.0 - ccFv) * u.envParams.x;
    colorS += (specAmb * (1.0 - ccFv) + ccAmb) * u.envParams.x;

    // --- Emissiva ----------------------------------------------------------------
    vec3 emissive = u.emissive.rgb;
    if (has_tex(4)) emissive *= texture(t_emissive, uv_for(4)).rgb;
    colorD += emissive;

    // Transparência física (pré-multiplicada): o difuso é coberto pelo alfa,
    // a luz REFLETIDA não — vidro transparente continua espelhando (antes o
    // especular era multiplicado pelo alfa e o vidro perdia o reflexo). A
    // transmissão abre o material para o que está atrás.
    float coverageAlpha = alpha * (1.0 - transmission);
    vec3 color = colorD * coverageAlpha + colorS;
    // Linear HDR: exposição DO OBJETO (ambiente próprio, v22) aqui; a do grupo,
    // o bloom e o tone map ficam no pós (SceneRenderer), depois do resolve.
    color *= u.cameraPos.w;
    float outAlpha = mode == 3 ? coverage : coverageAlpha;
    o_color = vec4(0.0, 0.0, 0.0, outAlpha);
    o_scene = vec4(aurea_hdr_encode(color), outAlpha);
}
