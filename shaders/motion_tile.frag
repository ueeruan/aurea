// O `#version` E O `uniform sampler2D uImage` EXPLICITOS SAO O PADRAO DO
// PROJETO (ver `dither.frag`, `blend.frag`, `correcao_de_cor.frag`).
//
// ESTE SHADER NAO OS TINHA, e funcionava por sorte: a compilacao ficava
// guardada em cache e o caminho antigo aceitava o sampler implicito. Na
// primeira compilacao de verdade o compilador do Impeller recusou com
// "uImage: undeclared identifier" — ou seja, o efeito so existia enquanto
// ninguem mexesse nele.
#version 460 core
#include <flutter/runtime_effect.glsl>
precision highp float;
uniform vec2 uSize;
uniform vec2 uOutput;
uniform vec2 uTile;
uniform vec2 uCenter;
uniform float uMirror;
uniform float uPhase;
uniform float uHorizontal;
uniform float uFilter;
// uClamp VAI NO FIM DE PROPOSITO. `setFloat` endereca por ORDEM DE
// DECLARACAO, e nao pelo byte do std140 — um uniforme novo no meio da lista
// deslocaria todos os que vem depois dele e o ladrilho sairia com a fase no
// lugar do espelho, sem erro nenhum na tela. (Mesma armadilha ja registrada
// para os shaders de filtro.)
uniform float uClamp;
uniform sampler2D uImage;

out vec4 fragColor;

void main() {
  vec2 uv=FlutterFragCoord().xy/uSize;
  // The source is centered in an enlarged transparent render target.
  vec2 p=(uv-0.5)*uOutput+0.5;
  vec2 q=(p-uCenter)/uTile+0.5;

  vec2 f;
  if(uClamp>0.5){
    // ------------------------------- CLAMP: NAO REPETE, ESTICA --------
    // A ultima coluna e a ultima linha de ladrilhos puxam a cor da BORDA da
    // fonte ate o fim. Serve quando repetir a imagem denuncia o truque (um
    // fundo com gradiente, por exemplo): em vez de uma parede de copias,
    // sai uma continuacao lisa — e, diferente de nao fazer nada, nao deixa
    // buraco nenhum no quadro.
    f=clamp(q,0.0,1.0);
  }else{
    if(uHorizontal>0.5) q.x-=mod(floor(q.y),2.0)*uPhase;
    else q.y-=mod(floor(q.x),2.0)*uPhase;
    vec2 cell=floor(q);
    f=fract(q);
    if(uMirror>0.5) f=mix(f,1.0-f,mod(cell,2.0));
  }

  // The input is now the source itself, never a padded transparent FBO.
  // Half-texel clamping prevents filtering a tile edge into transparency.
  vec2 halfPixel=0.5*uOutput/uSize;
  vec2 sampleUv=clamp(f,halfPixel,1.0-halfPixel);
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if(uFilter>0.5) sampleUv.y=1.0-sampleUv.y;
  #endif
  fragColor=texture(uImage,sampleUv);
}
