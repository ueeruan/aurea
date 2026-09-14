import 'dart:math' as math;

/// APRIMORAMENTO POR IA NA EXPORTACAO — as contas, sem tela e sem motor.
///
/// A rede (Real-ESRGAN compacto, escala nativa 4, native/enhance; o modelo
/// sai do [PerfilDoAprimoramento]) recebe o
/// quadro na resolucao da PROPRIA fonte e o devolve ampliado; a exportacao
/// leva o resultado ao tamanho em que o clipe aparece na composicao. O
/// palco continua mostrando o original: o efeito existe no arquivo.
///
/// Onde entra na ordem: extracao da fonte (sem ampliar) -> camera lenta
/// (RIFE, na resolucao pequena, que custa menos) -> IA -> composicao
/// (efeitos, cor, texto por cima). Texto e grafico do editor nunca passam
/// pela rede: ela so ve os quadros do video.

/// QUE MODELO a rede usa. Filmagem e desenho pedem redes diferentes: o
/// modelo de animacao achata textura de pele e folhagem, e o de video real
/// tenta inventar grao em traco limpo.
enum PerfilDoAprimoramento {
  /// Camera de verdade: realesr-general-x4v3, com a reducao de ruido feita
  /// pela mistura com o realesr-general-wdn-x4v3 (ver native/enhance).
  videoReal,

  /// Desenho, anime e grafico: realesr-animevideov3.
  animacao;

  String get emPalavras => switch (this) {
    PerfilDoAprimoramento.videoReal => 'Vídeo real',
    PerfilDoAprimoramento.animacao => 'Animação',
  };
}

/// A reducao de ruido padrao do Real-ESRGAN (denoise_strength 0,5).
const reducaoDeRuidoPadrao = 0.5;

/// A maior entrada que a rede recebe, em pixels: 960x540. Uma fonte maior
/// e reduzida antes (na extracao) — acima disto o custo por quadro no
/// celular deixa de caber numa exportacao.
const areaMaximaDaEntradaDaIa = 960 * 540;

/// A fonte ja tem ao menos esta fracao da area em que aparece: nao ha o
/// que ampliar e a IA nao entra (reconstruir pixels que ja existem
/// inventa textura e custa caro).
const fracaoQueDispensaIa = 0.9;

/// Folga de arredondamento na escolha da escala: 959x540 ainda cobre
/// 1920x1080 com x2 (o encaixe do FFmpeg arredonda um pixel).
const _folgaDeEscala = 0.98;

/// Encaixa w x h em cw x ch mantendo a proporcao (o
/// `force_original_aspect_ratio=decrease` do FFmpeg, que tambem amplia).
(int, int) encaixar(int w, int h, int cw, int ch) {
  if (w <= 0 || h <= 0 || cw <= 0 || ch <= 0) return (0, 0);
  final k = math.min(cw / w, ch / h);
  return (math.max(1, (w * k).round()), math.max(1, (h * k).round()));
}

/// As dimensoes como o video aparece: rotacao de 90 ou 270 graus troca
/// largura e altura (o FFmpeg gira na extracao).
(int, int) dimensoesExibidas(int w, int h, int rotacao) {
  final r = ((rotacao % 360) + 360) % 360;
  return r == 90 || r == 270 ? (h, w) : (w, h);
}

/// A entrada da rede para uma fonte w x h: a propria fonte, reduzida so
/// se passar de [areaMaximaDaEntradaDaIa] (a mesma conta do filtro da
/// extracao, ver `receitasDeExtracao`).
(int, int) entradaDaIa(int w, int h) {
  if (w <= 0 || h <= 0) return (0, 0);
  final k = math.min(1.0, math.sqrt(areaMaximaDaEntradaDaIa / (w * h)));
  return (math.max(1, (w * k).floor()), math.max(1, (h * k).floor()));
}

/// A menor escala do modelo (1, 2 ou 4) que leva w x h a cobrir fw x fh.
int escalaDaIa(int w, int h, int fw, int fh) {
  for (final s in const [1, 2, 4]) {
    if (w * s >= fw * _folgaDeEscala && h * s >= fh * _folgaDeEscala) return s;
  }
  return 4;
}

/// Por que a IA entra ou nao num clipe.
enum MotivoDoAprimoramento {
  aplica,
  desligado,

  /// O aparelho nao tem o motor (hoje: iOS).
  semMotor,

  /// O ffprobe nao disse o tamanho da fonte.
  fonteDesconhecida,

  /// A fonte ja tem a resolucao em que aparece.
  jaTemResolucao,
}

class PlanoDeAprimoramento {
  const PlanoDeAprimoramento(
    this.motivo, {
    this.entrada = (0, 0),
    this.saida = (0, 0),
    this.escala = 1,
  });

  final MotivoDoAprimoramento motivo;

  /// O que a rede recebe (a fonte, reduzida so se for grande).
  final (int, int) entrada;

  /// O tamanho em que o clipe aparece na composicao.
  final (int, int) saida;

  /// A escala do modelo que cobre a saida.
  final int escala;

  bool get aplica => motivo == MotivoDoAprimoramento.aplica;

  /// A frase da tela e do relatorio da exportacao.
  String get emPalavras => switch (motivo) {
    MotivoDoAprimoramento.aplica =>
      'IA ${entrada.$1}x${entrada.$2} -> ${saida.$1}x${saida.$2} (x$escala)',
    MotivoDoAprimoramento.desligado => 'desligado',
    MotivoDoAprimoramento.semMotor => 'motor de IA indisponível neste aparelho',
    MotivoDoAprimoramento.fonteDesconhecida =>
      'resolução da fonte desconhecida',
    MotivoDoAprimoramento.jaTemResolucao =>
      'o vídeo já tem a resolução da composição',
  };
}

/// O PLANO de um clipe. [larguraDaFonte]/[alturaDaFonte] sao as do
/// arquivo e [rotacao] a de exibicao (ffprobe); a composicao e a de saida
/// da exportacao.
PlanoDeAprimoramento planoDeAprimoramento({
  required bool ligado,
  required bool motorDisponivel,
  required int larguraDaFonte,
  required int alturaDaFonte,
  int rotacao = 0,
  required int larguraDaComposicao,
  required int alturaDaComposicao,
}) {
  if (!ligado) return const PlanoDeAprimoramento(MotivoDoAprimoramento.desligado);
  if (!motorDisponivel) {
    return const PlanoDeAprimoramento(MotivoDoAprimoramento.semMotor);
  }
  if (larguraDaFonte <= 0 || alturaDaFonte <= 0) {
    return const PlanoDeAprimoramento(MotivoDoAprimoramento.fonteDesconhecida);
  }
  final (w, h) = dimensoesExibidas(larguraDaFonte, alturaDaFonte, rotacao);
  final saida = encaixar(w, h, larguraDaComposicao, alturaDaComposicao);
  if (w * h >= saida.$1 * saida.$2 * fracaoQueDispensaIa) {
    return PlanoDeAprimoramento(
      MotivoDoAprimoramento.jaTemResolucao,
      entrada: (w, h),
      saida: saida,
    );
  }
  final entrada = entradaDaIa(w, h);
  return PlanoDeAprimoramento(
    MotivoDoAprimoramento.aplica,
    entrada: entrada,
    saida: saida,
    escala: escalaDaIa(entrada.$1, entrada.$2, saida.$1, saida.$2),
  );
}
