import 'dart:math' as math;

/// MODOS DE MESCLA QUE O FLUTTER NAO TEM.
///
/// O `BlendMode` do Flutter para nos 17 do PDF. O Alight Motion e o
/// After Effects tem mais uns dez, e sao justamente os que dao o "look":
/// Linear Burn fecha a sombra sem lavar, Vivid Light e o contraste de
/// grade de cor, Hard Mix e o cartaz de duas cores, Dissolve granula de
/// um jeito que nenhum fade imita.
///
/// A conta mora em `shaders/blend.frag`, porque precisa das duas imagens
/// (o que ja estava embaixo e a camada). Aqui fica a MESMA matematica em
/// Dart: e o que os testes prendem, e o que a interface usa para
/// desenhar a amostra do modo sem subir para a GPU.
enum AureaBlend {
  linearBurn,
  linearLight,
  vividLight,
  pinLight,
  hardMix,
  divide,
  subtract,
  darkerColor,
  lighterColor,
  dissolve,
}

String aureaBlendLabel(AureaBlend b) => switch (b) {
      AureaBlend.linearBurn => 'Linear Burn',
      AureaBlend.linearLight => 'Linear Light',
      AureaBlend.vividLight => 'Vivid Light',
      AureaBlend.pinLight => 'Pin Light',
      AureaBlend.hardMix => 'Hard Mix',
      AureaBlend.divide => 'Dividir',
      AureaBlend.subtract => 'Subtrair',
      AureaBlend.darkerColor => 'Cor mais escura',
      AureaBlend.lighterColor => 'Cor mais clara',
      AureaBlend.dissolve => 'Dissolver',
    };

/// Modos que olham o PIXEL inteiro em vez de canal a canal.
bool aureaBlendIsPerPixel(AureaBlend b) =>
    b == AureaBlend.darkerColor ||
    b == AureaBlend.lighterColor ||
    b == AureaBlend.dissolve;

double _clamp01(double v) => v < 0 ? 0 : (v > 1 ? 1 : v);

double _burn(double b, double s) =>
    s <= 0 ? 0 : 1 - math.min(1.0, (1 - b) / s);

double _dodge(double b, double s) =>
    s >= 1 ? 1 : math.min(1.0, b / (1 - s));

/// Luminancia perceptual (Rec. 709) — a mesma do shader.
double aureaLuma(double r, double g, double b) =>
    0.2126 * r + 0.7152 * g + 0.0722 * b;

/// A conta de UM canal, com [backdrop] e [source] em 0..1.
///
/// Vale so para os modos separaveis; os de pixel inteiro nao passam por
/// aqui (nao da para decidir "qual pixel e mais escuro" olhando um canal
/// de cada vez).
double blendChannel(AureaBlend mode, double backdrop, double source) {
  final b = _clamp01(backdrop);
  final s = _clamp01(source);
  return switch (mode) {
    AureaBlend.linearBurn => _clamp01(b + s - 1),
    AureaBlend.linearLight => _clamp01(b + 2 * s - 1),
    AureaBlend.vividLight =>
      s <= 0.5 ? _burn(b, 2 * s) : _dodge(b, 2 * (s - 0.5)),
    AureaBlend.pinLight => s <= 0.5
        ? math.min(b, 2 * s)
        : math.max(b, 2 * (s - 0.5)),
    AureaBlend.hardMix => b + s >= 1 ? 1 : 0,
    AureaBlend.divide => s <= 0 ? 1 : math.min(1.0, b / s),
    AureaBlend.subtract => _clamp01(b - s),
    // Sem sentido canal a canal: devolve o topo, que e o que o pixel
    // inteiro faria no caso em que ele vence.
    AureaBlend.darkerColor ||
    AureaBlend.lighterColor ||
    AureaBlend.dissolve =>
      s,
  };
}

/// Um pixel RGB em 0..1.
typedef Rgb = (double, double, double);

/// A conta de um PIXEL inteiro, ja cobrindo os modos que comparam
/// luminancia. [dissolveDraw] e o sorteio de 0..1 daquele pixel — so o
/// Dissolve usa.
Rgb blendPixel(
  AureaBlend mode,
  Rgb backdrop,
  Rgb source, {
  double sourceAlpha = 1,
  double dissolveDraw = 0,
}) {
  if (mode == AureaBlend.dissolve) {
    return dissolveDraw < sourceAlpha ? source : backdrop;
  }
  if (mode == AureaBlend.darkerColor || mode == AureaBlend.lighterColor) {
    final lb = aureaLuma(backdrop.$1, backdrop.$2, backdrop.$3);
    final ls = aureaLuma(source.$1, source.$2, source.$3);
    final venceOTopo =
        mode == AureaBlend.darkerColor ? ls < lb : ls > lb;
    return venceOTopo ? source : backdrop;
  }
  return (
    blendChannel(mode, backdrop.$1, source.$1),
    blendChannel(mode, backdrop.$2, source.$2),
    blendChannel(mode, backdrop.$3, source.$3),
  );
}

/// Composicao completa (Porter-Duff sobre a mescla), em cor NAO
/// pre-multiplicada. Devolve a cor e o alfa do resultado.
///
/// E a mesma linha do shader — existe aqui para o teste poder provar que
/// camada transparente nao muda nada e que camada opaca manda.
(Rgb, double) composeBlend(
  AureaBlend mode,
  Rgb backdrop,
  double backdropAlpha,
  Rgb source,
  double sourceAlpha, {
  double dissolveDraw = 0,
}) {
  final ab = _clamp01(backdropAlpha);
  final as = _clamp01(sourceAlpha);

  if (mode == AureaBlend.dissolve) {
    return dissolveDraw < as ? (source, 1.0) : (backdrop, ab);
  }

  final m = blendPixel(mode, backdrop, source, sourceAlpha: as);
  double canal(double cs, double cb, double cm) =>
      (1 - ab) * as * cs + (1 - as) * ab * cb + as * ab * cm;

  return (
    (
      canal(source.$1, backdrop.$1, m.$1),
      canal(source.$2, backdrop.$2, m.$2),
      canal(source.$3, backdrop.$3, m.$3),
    ),
    as + ab * (1 - as),
  );
}
