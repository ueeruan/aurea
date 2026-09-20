// O FRAGMENTO DO PBR — METALICO-RUGOSIDADE, COMO O glTF MANDA.
//
// ======================= POR QUE ESTA FORMULA ==========================
// O glTF define um material por quatro numeros (cor base, metalico,
// rugosidade, emissivo) mais os mapas. Reproduzir a formula do papel e o
// unico jeito de o arquivo sair no Aurea parecido com o que o autor dele
// viu no Blender — uma "iluminacao mais bonita" inventada aqui faria o
// mesmo arquivo parecer outro programa, e a culpa cairia no Aurea.
//
// AS QUATRO ESCOLHAS QUE MUDAM O RESULTADO:
//
//  1. O ESPECULAR DO DIELETRICO E FIXO EM 0,04. O modelo de metalico-
//     rugosidade nao tem "cor especular": o F0 de um nao-metal e sempre
//     4% de reflexao, e so o metal usa a cor base como F0. Um parametro a
//     mais aqui daria material diferente do glTF.
//
//  2. A RUGOSIDADE ENTRA LINEAR, e nao ao quadrado. A conversao para o
//     "alpha" da distribuicao (a = rugosidade^2) acontece dentro de cada
//     termo; faze-la duas vezes enceraria a superficie inteira.
//
//  3. A OCLUSAO AMBIENTAL MULTIPLICA SO O AMBIENTE, e nao a luz direta.
//     Ela diz "aqui chega menos luz do ceu", e nao "esta superficie e mais
//     escura": multiplicar a luz direta por ela apagaria o brilho de uma
//     lampada acesa na frente do objeto.
//
//  4. A COR DO VERTICE MULTIPLICA A COR BASE, e nao a substitui. E a
//     definicao de `COLOR_0` no glTF, e o contrario faria todo modelo
//     vindo de scanner sair preto.
#version 450

layout(location = 0) in vec3 v_posicao_mundo;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv0;
layout(location = 3) in vec2 v_uv1;
layout(location = 4) in vec4 v_tangente;
layout(location = 5) in vec4 v_cor;
layout(location = 6) in vec4 v_posicao_luz;

layout(location = 0) out vec4 frag_cor;

const float PI = 3.14159265359;

struct Luz {
  vec4 posicao_tipo;     // xyz posicao, w tipo (0 direcional, 1 pontual, 2 holofote)
  vec4 direcao_alcance;  // xyz direcao, w alcance
  vec4 cor_intensidade;  // rgb cor, w intensidade
  vec4 cone;             // x cos interno, y cos externo
};

layout(binding = 0) uniform Quadro {
  mat4 vista;
  mat4 projecao;
  mat4 vista_projecao;
  mat4 luz_espaco;
  vec4 olho;        // xyz = olho, w = sombra ligada (0/1)
  vec4 ambiente;    // rgb = ambiente linear
  vec4 ajustes;     // x = quantas luzes, y = PCF, z = tamanho do mapa, w = inclinacao
} u_quadro;

layout(binding = 1) uniform Luzes {
  Luz luzes[8];
} u_luz;

layout(binding = 2) uniform Desenho {
  mat4 mundo;
  mat4 normal;
  vec4 cor_base;      // rgb linear, a = alfa final (ja com a opacidade da camada)
  vec4 parametros;    // x metalico, y rugosidade, z forca emissiva, w alfa de corte
  vec4 emissivo;      // rgb linear, w = modo (0 opaco, 1 mascarado, 2 transparente)
  vec4 bandeiras;     // x cor, y normal, z metalico-rugosidade, w oclusao
  vec4 bandeiras2;    // x emissiva, y face dupla, z reservado, w base do osso
  vec4 tinta;         // rgb multiplicativo, a = 1 quando neutro
} u_desenho;

// AS TEXTURAS. A COR e a EMISSIVA entram marcadas como sRGB na GPU e saem
// lineares sozinhas — e o driver que faz a conversao, e nao uma conta aqui
// (§6: sem gama dobrada). As de DADO (normal, metalico-rugosidade,
// oclusao) entram cruas, porque nao sao cor e nao tem gama.
layout(binding = 4) uniform sampler2D tex_cor;
layout(binding = 5) uniform sampler2D tex_normal;
layout(binding = 6) uniform sampler2D tex_metalico_rugosidade;
layout(binding = 7) uniform sampler2D tex_emissiva;
layout(binding = 8) uniform sampler2D tex_oclusao;
layout(binding = 9) uniform sampler2D tex_sombra;

// -------------------------------------------------------------- a sombra

/// A SOMBRA DA LUZ DIRECIONAL, com PCF.
///
/// O MAPA GUARDA PROFUNDIDADE NUM CANAL DE COR, e nao e um alvo de
/// profundidade lido como textura. E o caminho que funciona igual na
/// Vulkan, na Metal e na GLES: ler profundidade exige formato e layout
/// diferentes em cada API, e errar isso nao da erro — da uma sombra preta
/// na tela inteira num aparelho e certa no outro.
float sombra_do_sol(vec4 posicao_luz) {
  if (u_quadro.olho.w < 0.5) return 1.0;

  vec3 projetado = posicao_luz.xyz / posicao_luz.w;
  // FORA DO MAPA NAO E SOMBRA. A luz cobre parte da cena; o que esta fora
  // e iluminado como se nao houvesse sombra, e nao como se estivesse na
  // penumbra — a alternativa seria uma parede escura na borda do mapa.
  if (projetado.x < 0.0 || projetado.x > 1.0 || projetado.y < 0.0 ||
      projetado.y > 1.0 || projetado.z < 0.0 || projetado.z > 1.0) {
    return 1.0;
  }

  float referencia = projetado.z - u_quadro.ajustes.w;
  float tamanho = u_quadro.ajustes.z;
  int pcf = int(u_quadro.ajustes.y);
  float raio = (pcf > 1 ? float(pcf - 1) * 0.5 : 0.0) / tamanho;
  int meio = pcf / 2;

  float aceso = 0.0;
  float total = 0.0;
  for (int y = -meio; y <= meio; ++y) {
    for (int x = -meio; x <= meio; ++x) {
      vec2 deslocamento = vec2(float(x), float(y)) * raio;
      float guardado = texture(tex_sombra, projetado.xy + deslocamento).r;
      // A COMPARACAO E MANUAL, e nao pelo `sampler2DShadow`: o comparador
      // de hardware e otimo, e o formato dele muda de API para API. Um `if`
      // custa nada e vale nos tres destinos.
      aceso += referencia <= guardado ? 1.0 : 0.0;
      total += 1.0;
    }
  }
  float duro = total > 0.0 ? aceso / total : 1.0;
  // UMA PENUMBRA MINIMA. Sem ela a borda do mapa de sombra e uma escada de
  // um pixel, e o objeto parece recortado com tesoura.
  return mix(0.35, 1.0, duro);
}

// -------------------------------------------------------------- o BRDF

/// A DISTRIBUICAO DE MICROFACES (GGX/Trowbridge-Reitz).
float distribuicao_ggx(float n_dot_h, float rugosidade) {
  float a = rugosidade * rugosidade;
  float a2 = a * a;
  float d = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
  return a2 / max(PI * d * d, 1e-7);
}

/// A GEOMETRIA (Smith com a aproximacao de Schlick-GGX). A luz rasante e
/// onde a superficie reflete demais quando este termo falta — o sintoma e
/// um anel branco na borda de uma esfera, o defeito classico de um shader
/// que esqueceu a oclusao geometrica.
float geometria_smith(float n_dot_v, float n_dot_l, float rugosidade) {
  float r = rugosidade + 1.0;
  float k = (r * r) / 8.0;
  float gv = n_dot_v / (n_dot_v * (1.0 - k) + k);
  float gl = n_dot_l / (n_dot_l * (1.0 - k) + k);
  return gv * gl;
}

vec3 fresnel_schlick(float cosseno, vec3 f0) {
  return f0 + (1.0 - f0) * pow(clamp(1.0 - cosseno, 0.0, 1.0), 5.0);
}

float oclusao_de_ambiente() {
  if (u_desenho.bandeiras.w < 0.5) return 1.0;
  // A OCLUSAO DO glTF E ESPALHADA: o vermelho e o valor. Ler so ele evita
  // que um arquivo com os outros canais estranhos escureca a cena inteira.
  return texture(tex_oclusao, v_uv0).r;
}

void main() {
  // ------------------------------------------------------- o material
  vec4 amostra = vec4(1.0);
  if (u_desenho.bandeiras.x > 0.5) amostra = texture(tex_cor, v_uv0);
  // A COR DO VERTICE MULTIPLICA, e o alfa tambem.
  amostra *= v_cor;

  vec4 base = u_desenho.cor_base * amostra;
  // A TINTA DA CAMADA multiplica a COR BASE, e nao o resultado final: um
  // modelo tingido de vermelho continua com o brilho especular branco,
  // porque um metal nao fica vermelho por causa de uma camada de cor.
  base.rgb *= u_desenho.tinta.rgb;

  int modo = int(u_desenho.emissivo.w + 0.5);
  // O MASCARADO DESCARTA O PIXEL, e nao o deixa translucido: e o que faz
  // uma folha de arvore ter buracos de verdade, com o fundo aparecendo em
  // vez de um borrado escuro.
  if (modo == 1 && base.a < u_desenho.parametros.w) discard;

  vec2 metalico_rugosidade = vec2(u_desenho.parametros.x, u_desenho.parametros.y);
  if (u_desenho.bandeiras.z > 0.5) {
    // O CANAL VERDE E A RUGOSIDADE E O AZUL E O METALICO — escolha do
    // glTF. O vermelho do arquivo e ignorado de proposito.
    vec4 mr = texture(tex_metalico_rugosidade, v_uv0);
    metalico_rugosidade.x *= mr.b;
    metalico_rugosidade.y *= mr.g;
  }
  float metalico = clamp(metalico_rugosidade.x, 0.0, 1.0);
  // O PISO DA RUGOSIDADE IMPEDE UM ESPELHO PERFEITO. Com rugosidade zero o
  // termo especular vira um ponto infinito, e o resultado e um pixel branco
  // estourando no meio do modelo em vez de um brilho.
  float rugosidade = clamp(metalico_rugosidade.y, 0.045, 1.0);

  // ---------------------------------------------------------- a normal
  vec3 n = normalize(v_normal);
  if (u_desenho.bandeiras.y > 0.5) {
    vec3 guardada = texture(tex_normal, v_uv0).xyz * 2.0 - 1.0;
    // A BASE TANGENTE SAI DA GEOMETRIA, e o sinal da quarta componente e o
    // que o importador calculou: sem ele, um lado do modelo fica com o
    // relevo invertido.
    vec3 t = normalize(v_tangente.xyz - n * dot(n, v_tangente.xyz));
    vec3 b = cross(n, t) * v_tangente.w;
    n = normalize(mat3(t, b, n) * guardada);
  }

  vec3 v = normalize(u_quadro.olho.xyz - v_posicao_mundo);
  float n_dot_v = max(dot(n, v), 1e-4);

  // AS TRES PARCELAS DA COR BASE, que o metalico separa: um metal nao tem
  // difusa (toda a luz vira reflexo) e um dieletrico tem 4% de especular.
  vec3 f0 = mix(vec3(0.04), base.rgb, metalico);
  vec3 difusa = base.rgb * (1.0 - metalico);

  vec3 direta = vec3(0.0);
  int quantas = int(u_quadro.ajustes.x);
  for (int i = 0; i < 8; ++i) {
    if (i >= quantas) break;
    Luz l = u_luz.luzes[i];

    vec3 para_luz;
    float atenuacao = 1.0;
    float tipo = l.posicao_tipo.w;
    if (tipo < 0.5) {
      para_luz = -normalize(l.direcao_alcance.xyz);
    } else {
      vec3 deslocamento = l.posicao_tipo.xyz - v_posicao_mundo;
      float distancia = length(deslocamento);
      para_luz = distancia > 1e-5 ? deslocamento / distancia : vec3(0.0, 1.0, 0.0);
      // O ALCANCE CORTA A LUZ, e nao a suaviza ate o fim: uma luz que chega
      // fraquinha no limite deixa uma borda visivel na parede, e a borda
      // denuncia a caixa da luz.
      if (l.direcao_alcance.w > 0.0 && distancia > l.direcao_alcance.w) continue;
      if (tipo > 1.5) {
        float cosseno = dot(-para_luz, normalize(l.direcao_alcance.xyz));
        float corte = smoothstep(l.cone.y, l.cone.x, cosseno);
        if (corte <= 0.0) continue;
        atenuacao = corte;
      }
      // A QUEDA E A INVERSA DO QUADRADO, com um piso: sem o piso, a
      // superficie a um milimetro da luz estoura para branco.
      atenuacao /= max(distancia * distancia, 0.01);
    }

    vec3 h = normalize(v + para_luz);
    float n_dot_l = max(dot(n, para_luz), 1e-4);
    float n_dot_h = max(dot(n, h), 0.0);
    float v_dot_h = max(dot(v, h), 0.0);

    vec3 especular = fresnel_schlick(v_dot_h, f0);
    float d = distribuicao_ggx(n_dot_h, rugosidade);
    float g = geometria_smith(n_dot_v, n_dot_l, rugosidade);
    vec3 brdf = (d * g * especular) / max(4.0 * n_dot_v * n_dot_l, 1e-4);

    // A ENERGIA NAO SE CRIA: o que vira reflexo sai da difusa. Sem este
    // fator, um metal claro fica mais brilhante do que a luz que recebe, e
    // o modelo parece lavado.
    vec3 kd = (vec3(1.0) - especular) * (1.0 - metalico);

    vec3 cor_da_luz = l.cor_intensidade.rgb * l.cor_intensidade.w;
    // SO A DIRECIONAL PROJETA SOMBRA. Sombrear cada luz custaria um passe
    // e um mapa por luz, e num celular isso e o quadro inteiro.
    if (tipo < 0.5) cor_da_luz *= sombra_do_sol(v_posicao_luz);

    direta += (kd * difusa / PI + brdf) * cor_da_luz * n_dot_l * atenuacao;
  }

  // O AMBIENTE. Nao e uma imagem de ambiente — isso e um degrau posterior,
  // e esta declarado como tal. O que existe resolve o necessario: o lado
  // escuro nao fica preto, e um metal sem ambiente pareceria papelao.
  float oclusao = oclusao_de_ambiente();
  vec3 difusa_ambiente = difusa * u_quadro.ambiente.rgb * oclusao;
  vec3 especular_ambiente = f0 * u_quadro.ambiente.rgb * mix(1.0, oclusao, 0.5);

  vec3 emissivo = u_desenho.emissivo.rgb;
  if (u_desenho.bandeiras2.x > 0.5) emissivo *= texture(tex_emissiva, v_uv0).rgb;

  vec3 cor = direta + difusa_ambiente + especular_ambiente + emissivo;
  // A FORCA EMISSIVA multiplica DEPOIS e pode passar de 1 de proposito: e o
  // estouro que o Bloom das camadas de efeito agarra em seguida (§35).
  cor *= u_desenho.parametros.z;

  // A SAIDA E PREMULTIPLICADA, e nao "cor e alfa lado a lado".
  //
  // O alvo da 3D entra na composicao 2D como uma textura pronta, e a
  // composicao mistura textura sobre textura esperando a cor ja multiplicada
  // pelo alfa (§16). Entregar alfa reto obrigaria a composicao a saber que
  // esta textura, e so esta, vem diferente — e a partir dai todo efeito
  // novo teria de lembrar disso.
  //
  // NO MODO OPACO E NO MASCARADO O ALFA VAI A 1. Um material que o arquivo
  // declara opaco nao pode deixar ver atraves dele porque a textura de cor
  // tinha um canal alfa esquecido no canto — e esse canal existe em arquivo
  // exportado de qualquer programa que guarde RGBA. O mascarado ja resolveu
  // o recorte no `discard` acima; o que sobra dele e solido.
  //
  // A SAIDA E RGBA8, e nao HDR: um valor acima de 1 seria cortado adiante
  // de qualquer forma, e cortar aqui deixa o comportamento previsivel em
  // vez de depender do driver.
  float alfa = clamp(base.a, 0.0, 1.0);
  if (modo < 2) alfa = 1.0;
  frag_cor = vec4(clamp(cor, 0.0, 1.0) * alfa, alfa);
}
