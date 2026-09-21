// =============================================================================
//  Aurea / shaders / Composite.h
//
//  FONTE ÚNICA dos shaders do compositor.
//
//  Escritos em GLSL (o dialeto que o Vulkan aceita via SPIR-V). O iOS NÃO
//  recebe uma versão escrita à mão em MSL: o mesmo SPIR-V é traduzido para
//  Metal Shading Language no BUILD, por SPIRV-Cross. Assim é impossível os dois
//  lados divergirem — e é por isso que preview e export são visualmente
//  idênticos, mesmo com backends diferentes.
//
//  Convenções que valem para todos os shaders daqui:
//
//   - Espaço de cor LINEAR. Todo valor que entra já foi convertido de sRGB na
//     importação; todo valor que sai é convertido para o espaço de saída no
//     passe final (ColorOutput). Um shader de efeito nunca converte sozinho.
//
//   - Precisão média (mediump) em mobile por padrão: a diferença visual contra
//     highp é nula em cor de 8 bits e o custo de banda cai. Onde a precisão
//     importa (coordenadas de textura grandes, tempo), o shader marca highp.
//
//   - Sem ramificação dependente de dado quando o mesmo resultado sai por
//     aritmética: em GPU mobile, um `if` divergente custa mais que uma
//     multiplicação por zero.
// =============================================================================
#pragma once

namespace aurea {

/// Vertex: um triângulo de tela cheia gerado sem vertex buffer.
///
/// Sem VBO porque o compositor desenha UM quad por passe. Criar e amarrar um
/// buffer de 4 vértices seria uma chamada de driver a mais por passe, por
/// frame, sem nenhum ganho — a posição sai de gl_VertexIndex.
inline constexpr const char* kCompositeVertexSource = R"GLSL(
#version 450

layout(location = 0) out vec2 v_uv;

void main() {
    // Triangulo que cobre a tela: (-1,-1) (3,-1) (-1,3).
    // Desenhar um triangulo em vez de um quad evita a costura diagonal, onde
    // dois triangulos compartilham a mesma aresta e a interpolacao pode
    // produzir um pixel de diferenca.
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);

    // Vulkan: Y aponta para baixo no framebuffer e para cima em NDC.
    // A inversao e feita aqui, uma vez, em vez de em cada passe.
    gl_Position.y = -gl_Position.y;
}
)GLSL";

/// Fragment base do compositor: amostra a textura da layer e aplica o blend.
///
/// O modo de blend NÃO é escolhido por ramificação em runtime: cada BlendMode
/// compila uma variante (o driver escolhe o blend de hardware onde há estado de
/// blend fixo). Este shader é o caso `Normal`; as variantes acrescentam o
/// cálculo do modo.
inline constexpr const char* kCompositeFragmentSource = R"GLSL(
#version 450

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 0) uniform sampler2D u_texture;

layout(push_constant) uniform Push {
    // Transform da layer em espaco de composicao, empacotado.
    vec4  uvScaleOffset;   // xy = escala, zw = deslocamento
    vec4  tint;            // rgba multiplicativo (opacidade no alfa)
    float opacity;
    float _pad0, _pad1, _pad2;
} pc;

void main() {
    vec2 uv = v_uv * pc.uvScaleOffset.xy + pc.uvScaleOffset.zw;

    // Fora da textura: transparente, não clampado. Clampar esticaria a borda
    // da imagem e o usuario veria uma faixa de cor em vez de fundo.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        o_color = vec4(0.0);
        return;
    }

    vec4 c = texture(u_texture, uv);
    c *= pc.tint;
    c.a *= pc.opacity;
    o_color = c;
}
)GLSL";

/// Fragment de blend por variante. O `#define` é injetado na compilação, então
/// o driver enxerga um caminho reto e consegue agendar sem divergência.
inline constexpr const char* kBlendVariantPreamble = R"GLSL(
#version 450
#define AUREA_BLEND_VARIANT 1
)GLSL";

} // namespace aurea
