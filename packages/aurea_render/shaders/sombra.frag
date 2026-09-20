// O FRAGMENTO DO PASSE DE SOMBRA — GUARDA A PROFUNDIDADE, E SO ISSO.
//
// ========================= A DECISAO DESTE ARQUIVO =====================
// O MAPA DE SOMBRA E UM ALVO DE COR DE UM CANAL (R32_FLOAT), E NAO UM
// ALVO DE PROFUNDIDADE LIDO COMO TEXTURA.
//
// A troca e proposital. Um alvo de profundidade lido como textura e mais
// economico, e o formato, a ordem dos canais e o estado de layout mudam em
// cada API: na Vulkan e um `D32_SFLOAT` com a leitura exigindo um layout
// diferente do de escrita, na Metal e um `depth32Float` com regra propria.
// Quem escreve isso acerta na API que testou e erra calado na outra — e o
// sintoma nao e um erro, e uma sombra preta cobrindo a tela inteira num
// aparelho so.
//
// Um canal de cor custa quatro bytes por pixel em vez de quatro, funciona
// igual nos tres destinos, e a comparacao e um `if` no shader do PBR. Numa
// janela de 1024 ou 2048 isso cabe no orcamento (§21).
//
// O QUE ESTE SHADER NAO FAZ, de proposito:
//   - NAO ESCREVE COR. Nenhum `layout(location = ...) out` de tinta: o
//     alvo inteiro e profundidade.
//   - NAO DESCARTA POR ALFA. Uma folha de arvore mascarada projetaria a
//     sombra do retangulo da textura em vez da copa. Corrigir isso pede o
//     mapa de alfa ligado aqui, e o preco e um segundo pipeline por
//     material mascarado; fica declarado como limitacao em vez de
//     silenciosamente errado.
//   - NAO TEM OCLUSAO AMBIENTAL NEM EMISSIVO. Nao ha luz nenhuma aqui.
#version 450

// A PROFUNDIDADE VAI PARA O CANAL VERMELHO. O formato e de um canal so, e
// escrever um `vec4` nele nao daria erro — o driver guardaria o vermelho e
// jogaria o resto fora, o que esconderia um engano de formato.
layout(location = 0) out float profundidade;

void main() {
  // `gl_FragCoord.z` JA ESTA EM [0,1], E NAO EM [-1,1].
  //
  // Duas razoes: a projecao do Aurea segue a convencao do D3D (a mesma do
  // Diligent), e a Vulkan nativamente ja tem a faixa de profundidade em
  // [0,1]. Nao ha remapeamento a fazer aqui — fazer um (o classico
  // `* 0.5 + 0.5` de quem vem da OpenGL) empurraria o mapa inteiro para a
  // metade errada da faixa e a sombra sumiria.
  profundidade = gl_FragCoord.z;
}
