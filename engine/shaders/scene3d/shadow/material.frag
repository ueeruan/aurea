#version 450
// =============================================================================
//  Aurea / shaders / scene3d / shadow / material.frag
//
//  0.4.6: pré-passe de material dos reflexos de tela (post/ssr.frag). Os
//  vértices são os da sombra (só posição, skin e morph prontos) com a câmera
//  da cena; a cor guarda os fatores do material do desenho, que vêm no push:
//  `extra.z` = rugosidade, `extra.w` = metal.
// =============================================================================
layout(push_constant) uniform Push {
    mat4 lightFromLocal;
    vec4 extra;
} pc;

layout(location = 0) out vec4 o_material;

void main() {
    o_material = vec4(clamp(pc.extra.z, 0.0, 1.0), clamp(pc.extra.w, 0.0, 1.0), 0.0, 1.0);
}
