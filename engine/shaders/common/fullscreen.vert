#version 450
// =============================================================================
//  Aurea / shaders / common / fullscreen.vert
//
//  Um triângulo que cobre o alvo inteiro, sem vertex buffer.
//
//  Um triângulo e não dois: dois triângulos dividem uma diagonal, e a
//  interpolação ao longo dela pode diferir em um ulp de um lado para o outro —
//  o que num efeito de vizinhança vira uma costura visível.
//
//  Convenção: v_uv (0,0) é o canto SUPERIOR ESQUERDO do alvo e da textura. No
//  Vulkan o NDC y=-1 é o topo, então não há inversão aqui. O Metal (NDC y=+1 no
//  topo) recebe a inversão do SPIRV-Cross no build, não do shader.
// =============================================================================

layout(location = 0) out vec2 v_uv;

void main() {
    vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
