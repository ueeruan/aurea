// =============================================================================
//  Aurea / shaders / scene3d / pbr / mesh_vertex.glsl
//
//  Vértice de malha 3D. Incluído por mesh.vert (estática) e mesh_skinned.vert
//  (com SKINNED definido): uma fonte, duas variantes — nada de combinação
//  livre de #defines.
//
//  Push constants: matriz de mundo da instância e a matriz de normais
//  (inversa transposta, 3×3 em colunas vec4; normalCol0.w = início das juntas
//  da skin no SSBO do frame). Com skin, as juntas já estão no
//  espaço da cena do modelo; `model` leva da cena ao mundo (a layer).
// =============================================================================
#include "../common/scene.glsl"

layout(push_constant) uniform Push {
    mat4 model;
    vec4 normalCol0;
    vec4 normalCol1;
    vec4 normalCol2;
} pc;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
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

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec4 v_tangent;
layout(location = 3) out vec2 v_uv0;
layout(location = 4) out vec2 v_uv1;
layout(location = 5) out vec4 v_color;

void main() {
    vec4 local = vec4(a_position, 1.0);
    vec3 n = a_normal;
    vec3 t = a_tangent.xyz;
#ifdef SKINNED
    // Deslocamento da skin desta instância no SSBO do frame (normalCol0.w).
    uint base = uint(pc.normalCol0.w + 0.5);
    mat4 skin = a_weights.x * j.joints[base + a_joints.x] + a_weights.y * j.joints[base + a_joints.y]
              + a_weights.z * j.joints[base + a_joints.z] + a_weights.w * j.joints[base + a_joints.w];
    local = skin * local;
    mat3 s3 = mat3(skin);
    n = s3 * n;
    t = s3 * t;
#endif
    vec4 world = pc.model * local;
    mat3 nm = mat3(pc.normalCol0.xyz, pc.normalCol1.xyz, pc.normalCol2.xyz);
    v_world = world.xyz;
    v_normal = nm * n;
    v_tangent = vec4(mat3(pc.model) * t, a_tangent.w);
    v_uv0 = a_uv0;
    v_uv1 = a_uv1;
    v_color = a_color;
    gl_Position = u.viewProj * world;
}
