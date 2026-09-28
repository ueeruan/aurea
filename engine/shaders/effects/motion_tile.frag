#version 450
// =============================================================================
//  Aurea / shaders / effects / motion_tile.frag
//
//  MOTION TILE — o visual do app antigo.
//
//  Tudo acontece no espaço da FONTE (0..1 = a layer que entrou no efeito), então
//  a grade é presa à layer e gira/escala com ela:
//
//   - um ladrilho é a layer INTEIRA reduzida por Largura/Altura do ladrilho; em
//     unidades de ladrilho, 0 é o centro de um ladrilho e ±0,5 as bordas dele;
//   - a fase empurra cada linha em X por fase × índice da linha (com a opção
//     horizontal, cada coluna em Y por fase × índice da coluna);
//   - o espelho vira os ladrilhos de índice ímpar, e as emendas casam;
//   - a amostra fica meio texel da ENTRADA para dentro da borda, senão o filtro
//     linear traria a margem transparente para toda emenda;
//   - a janela de saída é medida no QUADRO (o afim windowX/windowY leva o uv
//     desta textura ao uv da composição, centrado em 0): fora dela, nada.
//
//  "Esticar bordas" (flags.w) só existe para projetos gravados com o Motion Tile
//  anterior do Aurea: sem repetir, a borda da layer se estende até o fim.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;    // coordenada na fonte = v_uv * xy + zw
    vec4 tile;     // xy = tamanho do ladrilho (fração da fonte), zw = centro (fração da fonte)
    vec4 flags;    // x = espelhar, y = fase (voltas), z = fase por coluna, w = esticar (legado)
    vec4 inset;    // xy = meio texel da textura de ENTRADA
    vec4 windowX;  // uv do quadro - 0.5: x = dot(xy, v_uv) + z;  w = meia janela
    vec4 windowY;  // idem em y
} p;

void main() {
    vec2 frame = vec2(dot(p.windowX.xy, v_uv) + p.windowX.z,
                      dot(p.windowY.xy, v_uv) + p.windowY.z);
    if (abs(frame.x) > p.windowX.w || abs(frame.y) > p.windowY.w) {
        o_color = vec4(0.0);
        return;
    }

    vec2 src = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 cellPos = (src - p.tile.zw) / p.tile.xy;   // unidades de ladrilho

    vec2 local;
    if (p.flags.w > 0.5) {
        local = clamp(cellPos + 0.5, 0.0, 1.0);
    } else {
        if (p.flags.z > 0.5) cellPos.y += p.flags.y * floor(cellPos.x + 0.5);
        else                 cellPos.x += p.flags.y * floor(cellPos.y + 0.5);
        vec2 index = floor(cellPos + 0.5);
        vec2 within = cellPos - index;                      // -0,5 .. 0,5
        if (p.flags.x > 0.5) {
            vec2 odd = step(0.5, mod(abs(index), 2.0));
            within = mix(within, -within, odd);
        }
        local = within + 0.5;
    }

    vec2 edge = min(p.inset.xy, vec2(0.5));
    o_color = texture(u_tex0, clamp(local, edge, 1.0 - edge));
}
