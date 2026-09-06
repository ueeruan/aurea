import 'dart:math' as math;

/// PIRAMIDE DE BLOOM — a conta que faz o glow ILUMINAR em vez de LAVAR.
///
/// "Conservacao de energia" tem um significado exato aqui: a soma dos
/// pesos dos niveis e 1. Sem isso, acrescentar um nivel para deixar o
/// glow mais suave tambem o deixa mais claro, e a pessoa fica ajustando
/// intensidade para compensar uma suavidade que nao pediu.
///
/// A piramide tambem e o que separa glow de raio grande bom de glow de
/// raio grande ruim: um passe unico com sigma enorme sai quadrado e com
/// banda; varios niveis com sigma dobrando saem lisos.

/// Quantos niveis a qualidade pede.
int bloomLevels(int quality) => switch (quality) {
      0 => 2, // Draft
      2 => 5, // High
      _ => 3, // Normal
    };

/// Pesos NORMALIZADOS de cada nivel: IGUAIS, e a soma e sempre 1.
///
/// A primeira versao dava ao nivel estreito quatro vezes o peso do
/// largo, "para o nucleo brilhar". O resultado era um halo curto: o
/// nivel largo, que e o unico que alcanca longe, ficava com 6% da luz e
/// sumia a um palmo da forma. O Deep Glow de referencia soma cada
/// oitava com o MESMO peso — e por isso o halo dele vai longe com
/// degrade suave sem lavar o nucleo. A energia continua conservada:
/// acrescentar nivel espalha, nao clareia.
List<double> bloomWeights(int levels) {
  if (levels <= 0) return const [];
  return List<double>.filled(levels, 1.0 / levels);
}

/// O sigma de cada nivel, em pixels, para um raio pedido.
///
/// DOBRA PARA CIMA a partir do raio: o primeiro nivel tem o sigma do
/// raio pedido e cada seguinte tem o dobro. A versao anterior dividia
/// por dois a partir do raio, entao o raio da ficha era o MAIOR sigma —
/// e um Deep Glow de raio 40 nao passava de 40 px, quando no plugin de
/// referencia 40 e a base de uma piramide que chega a centenas.
List<double> bloomSigmas(double radiusPx, int levels) {
  if (levels <= 0 || radiusPx <= 0) return const [];
  final out = <double>[];
  for (var i = 0; i < levels; i++) {
    out.add(radiusPx * math.pow(2, i));
  }
  return out;
}

/// LIMIAR SUAVE: o quanto de um pixel de luminancia [l] entra no glow.
///
/// Um limiar duro cria uma borda visivel onde o brilho cruza o valor —
/// o glow "liga" de repente numa linha reta no meio do degrade. A
/// suavidade transforma o degrau numa rampa.
double bloomThreshold(double l, double threshold, double softness) {
  final duro = math.max(0.0, l - threshold);
  final s = softness.clamp(0.0, 1.0);
  if (s < 1e-6) return duro;

  // JOELHO SUAVE: a curva quadratica classica de bloom. Ela encosta em
  // zero antes do limiar e encosta na reta depois dele — por isso a
  // funcao inteira cresce sem degrau. A primeira versao que escrevi
  // dava um PULO PARA BAIXO na emenda, e o teste de monotonia pegou.
  final joelho = threshold * s;
  var macio = l - threshold + joelho;
  macio = macio.clamp(0.0, 2 * joelho);
  macio = macio * macio / (4 * joelho + 1e-9);
  return math.max(macio, duro);
}

/// EXPOSICAO em paradas (stops), como em fotografia: +1 dobra a luz.
double exposureGain(double stops) => math.pow(2, stops).toDouble();

/// Mapeamento de tom: comprime a luz que passou de 1 sem estourar num
/// branco chapado.
///
/// 0 = ACES Filmic, 1 = Reinhard, 2 = Reinhard 2, 3 = Clamp.
double tonemap(double x, int mode) {
  final v = math.max(0.0, x);
  switch (mode) {
    case 1:
      return v / (1 + v);
    case 2:
      // Reinhard estendido: o branco de referencia mapeia para 1, e
      // acima dele satura. Sem o aparo, a curva passa de 1 e o "tone
      // mapping" deixa de mapear tom.
      const branco = 4.0;
      return (v * (1 + v / (branco * branco)) / (1 + v)).clamp(0.0, 1.0);
    case 3:
      return v.clamp(0.0, 1.0);
    default:
      // ACES aproximado (Narkowicz): a curva de cinema.
      const a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
      return ((v * (a * v + b)) / (v * (c * v + d) + e)).clamp(0.0, 1.0);
  }
}

/// O LIMIAR COMO MATRIZ DE COR: (escala, deslocamento em 0..255).
///
/// Matriz de cor e linear por definicao, entao o limiar vira um
/// remapeamento: o valor do limiar cai em zero e o branco continua
/// branco — `(x - limiar) / (1 - limiar)`. A suavidade nao cabe numa
/// reta; ela entra puxando o ponto de corte para baixo, de modo que a
/// rampa comece antes do limiar.
///
/// A conta anterior tirava a escala de [bloomThreshold] e chegava a
/// multiplicar por vinte: com o limiar no padrao, um circulo de meia
/// luminancia saturava em branco e brilhava — o oposto do que um limiar
/// alto promete.
/// A EXPOSICAO ENTRA AQUI, antes do limiar, e nao depois.
///
/// E o que o limiar de 1,0 — o padrao — quer dizer: no plugin de
/// referencia a exposicao levanta a imagem para a escala HDR e o limiar
/// corta o que passou de 1. Com uma parada de exposicao, meia
/// luminancia vira 1,1 e brilha. Aplicando a exposicao DEPOIS, como
/// estava, nada jamais passava de 1 e o efeito nascia invisivel.
///
/// A conta e `(x * ganho - limiar) / (ganho - limiar)`: o limiar cai em
/// zero e o valor mais alto possivel depois do ganho continua no teto.
(double, double) glowThresholdMatrix(
    double threshold, double softness, double gain) {
  final g = math.max(0.01, gain);
  final efetivo = (threshold * (1 - softness.clamp(0.0, 1.0) * 0.5))
      .clamp(0.0, g * 0.98);
  final escala = g / math.max(0.02, g - efetivo);
  return (escala, -efetivo * 255 * (escala / g));
}

/// O quanto de um pixel de luminancia [l] (0..1) sobra depois do ganho e
/// do limiar, pela mesma conta que a matriz faz. So para teste.
double glowAfterThreshold(double l, double threshold, double softness,
        [double gain = 1]) {
  final (escala, desl) = glowThresholdMatrix(threshold, softness, gain);
  return ((l * 255 * escala + desl) / 255).clamp(0.0, 1.0);
}

/// TETO DE SIGMA para uma composicao de [compWidth] x [compHeight].
///
/// Um desfoque gaussiano de sigma S nao e desenhado em S pixels: o motor
/// precisa de tres sigmas de margem de cada lado para o halo nao sair
/// cortado, entao ele pinta num alvo de (L + 6S) x (A + 6S) — e faz isso
/// na razao de pixels da tela. Com S = 688, numa composicao 1080x1920 a
/// 3x, esse alvo tem 284 megapixels: 1,1 GB de textura. O aparelho nao
/// desenha isso; ele fecha o app. Foi o que os testadores encontraram
/// abrindo o Deep Glow e o preset Neon do Glow.
///
/// O teto e um quinto do menor lado. Um halo de sigma maior que isso ja
/// cobre a composicao inteira num lavado uniforme — dobrar de novo nao
/// muda um pixel do que se ve, so a conta.
double sigmaTeto(int compWidth, int compHeight) =>
    math.max(8.0, math.min(compWidth, compHeight) * 0.2);

/// Um nivel da piramide depois do teto.
typedef NivelDeBloom = ({double sigma, double peso});

/// A PIRAMIDE CORTADA NO TETO, com a luz preservada.
///
/// Os niveis que passariam de [teto] teriam todos o mesmo sigma — o
/// mesmo lavado, desenhado varias vezes. Entao eles nao entram: o peso
/// deles e somado ao ultimo nivel que coube. A soma dos pesos continua
/// a mesma, entao o glow nao clareia nem escurece por causa do corte —
/// so para de pagar por copias do mesmo borrao.
List<NivelDeBloom> piramideAteOTeto(
  List<double> sigmas,
  List<double> pesos,
  double teto,
) {
  final out = <NivelDeBloom>[];
  final n = math.min(sigmas.length, pesos.length);
  for (var i = 0; i < n; i++) {
    if (out.isNotEmpty && out.last.sigma >= teto && sigmas[i] >= teto) {
      out[out.length - 1] = (
        sigma: out.last.sigma,
        peso: out.last.peso + pesos[i],
      );
      continue;
    }
    out.add((sigma: math.min(sigmas[i], teto), peso: pesos[i]));
  }
  return out;
}
