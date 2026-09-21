#version 450
// =============================================================================
//  Aurea / shaders / composite / layer.vert
//
//  O quad de uma layer, JÁ TRANSFORMADO.
//
//  É aqui que o Transform da layer (posição, escala, rotação, âncora) entra —
//  na rasterização da composição, e não num passe que renderiza a layer
//  transformada numa textura para depois compor. A matriz clip←layer é
//  montada no C++ uma vez por layer por frame; a GPU só multiplica.
//
//  `region` é o retângulo, em pixels da layer, que a textura cobre. Numa layer
//  sem efeito de domínio é (0,0,largura,altura); com Motion Tile é a área
//  ladrilhada, maior que a layer, centrada nela.
// =============================================================================

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;    // x,y,largura,altura em pixels da layer
    vec4 uvRect;    // uv da textura que corresponde à região
    vec4 params;    // x=opacidade
} pc;

layout(location = 0) out vec2 v_uv;

const vec2 kCorners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main() {
    vec2 c = kCorners[gl_VertexIndex];
    vec2 layerPos = pc.region.xy + c * pc.region.zw;
    v_uv = pc.uvRect.xy + c * pc.uvRect.zw;
    gl_Position = pc.clipFromLayer * vec4(layerPos, 0.0, 1.0);
}
