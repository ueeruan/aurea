// =============================================================================
//  Aurea / shaders / scene3d / shadow / shadow_vertex.glsl
//
//  Vértice do mapa de sombra: só posição (e skin, na variante SKINNED). A
//  matriz do push já é luz ← local; `extra.x` = início das juntas no SSBO.
//  Estática instanciada (8E): `extra.y` = 1 + início das matrizes luz ← local
//  das instâncias no SSBO do quadro (uma mat4 por instância).
// =============================================================================
#include "../../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 lightFromLocal;
    vec4 extra;
} pc;

layout(location = 0) in vec3 a_position;
#ifdef CUTOUT
layout(location = 3) in vec2 a_uv0;
layout(location = 4) in vec2 a_uv1;
layout(location = 5) in vec4 a_color;
layout(location = 0) out vec2 v_uv0;
layout(location = 1) out vec2 v_uv1;
layout(location = 2) out float v_alpha;
#endif
#ifdef SKINNED
layout(location = 6) in uvec4 a_joints;
layout(location = 7) in vec4 a_weights;
layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Joints {
    mat4 joints[];
} j;
#else
layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Instances {
    mat4 m[];
} inst;
#endif

void main() {
#ifdef CUTOUT
    v_uv0 = a_uv0;
    v_uv1 = a_uv1;
    v_alpha = a_color.a;
#endif
    vec4 p = vec4(a_position, 1.0);
#ifdef SKINNED
    uint base = uint(pc.extra.x + 0.5);
    mat4 skin = a_weights.x * j.joints[base + a_joints.x] + a_weights.y * j.joints[base + a_joints.y]
              + a_weights.z * j.joints[base + a_joints.z] + a_weights.w * j.joints[base + a_joints.w];
    p = skin * p;
    gl_Position = pc.lightFromLocal * p;
#else
    if (pc.extra.y > 0.5) {
        uint base = uint(pc.extra.y + 0.5) - 1u;
        gl_Position = inst.m[base + uint(gl_InstanceIndex)] * p;
    } else {
        gl_Position = pc.lightFromLocal * p;
    }
#endif
}
