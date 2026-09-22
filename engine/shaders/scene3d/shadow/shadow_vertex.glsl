// =============================================================================
//  Aurea / shaders / scene3d / shadow / shadow_vertex.glsl
//
//  Vértice do mapa de sombra: só posição (e skin, na variante SKINNED). A
//  matriz do push já é luz ← local; `extra.x` = início das juntas no SSBO.
// =============================================================================
#include "../../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 lightFromLocal;
    vec4 extra;
} pc;

layout(location = 0) in vec3 a_position;
#ifdef SKINNED
layout(location = 6) in uvec4 a_joints;
layout(location = 7) in vec4 a_weights;
layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Joints {
    mat4 joints[];
} j;
#endif

void main() {
    vec4 p = vec4(a_position, 1.0);
#ifdef SKINNED
    uint base = uint(pc.extra.x + 0.5);
    mat4 skin = a_weights.x * j.joints[base + a_joints.x] + a_weights.y * j.joints[base + a_joints.y]
              + a_weights.z * j.joints[base + a_joints.z] + a_weights.w * j.joints[base + a_joints.w];
    p = skin * p;
#endif
    gl_Position = pc.lightFromLocal * p;
}
