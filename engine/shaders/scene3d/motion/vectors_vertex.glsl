// =============================================================================
//  Aurea / shaders / scene3d / motion / vectors_vertex.glsl
//
//  Passe de VELOCIDADE do motion blur 3D rápido (0.3.2, MotionBlurSettings::
//  vectorBlur). Incluído por vectors.vert (estática) e vectors_skinned.vert
//  (SKINNED). Só geometria: nenhuma luz, nenhuma textura além do recorte MASK.
//
//  Três matrizes clip ← cena do modelo por desenho:
//    • o push constant (128 B, o mínimo garantido do Vulkan) leva as duas do
//      OBTURADOR — começo e fim, câmera e objeto em cada ponta;
//    • o bloco de uniform leva a do CENTRO, a mesma do passe PBR, que é a que
//      rasteriza (profundidade e silhueta iguais às da imagem).
//  As posições de clip das pontas viajam como varyings: a interpolação com
//  correção de perspectiva entrega, no fragmento, exatamente o ponto da
//  superfície nos dois instantes (elas são lineares no espaço do objeto).
// =============================================================================
#include "../../common/bindings.glsl"
#include "vectors_block.glsl"

layout(push_constant) uniform Push {
    mat4 clipBegin;
    mat4 clipEnd;
} pc;

layout(location = 0) in vec3 a_position;
layout(location = 3) in vec2 a_uv0;
layout(location = 4) in vec2 a_uv1;
layout(location = 5) in vec4 a_color;
#ifdef SKINNED
layout(location = 6) in uvec4 a_joints;
layout(location = 7) in vec4 a_weights;

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Joints {
    mat4 joints[];
} j;
#endif

layout(location = 0) out vec4 v_begin;
layout(location = 1) out vec4 v_end;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out float v_alpha;

void main() {
    vec4 local = vec4(a_position, 1.0);
#ifdef SKINNED
    uint base = uint(u.alpha.z + 0.5);
    mat4 skin = a_weights.x * j.joints[base + a_joints.x] + a_weights.y * j.joints[base + a_joints.y]
              + a_weights.z * j.joints[base + a_joints.z] + a_weights.w * j.joints[base + a_joints.w];
    local = skin * local;
#endif
    gl_Position = u.clipFromLocal * local;
    v_begin = pc.clipBegin * local;
    v_end = pc.clipEnd * local;
    // KHR_texture_transform da cor base (escala, rotação, deslocamento) — só
    // o recorte MASK lê a textura.
    vec2 uv = u.alpha.w > 0.5 ? a_uv1 : a_uv0;
    uv *= u.uvXform.zw;
    float r = u.uvRot.x;
    if (r != 0.0) {
        float c = cos(r), s = sin(r);
        uv = vec2(c * uv.x + s * uv.y, -s * uv.x + c * uv.y);
    }
    v_uv = uv + u.uvXform.xy;
    v_alpha = a_color.a;
}
