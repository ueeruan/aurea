import 'dart:math' as math;
import 'dart:ui';

/// TEXTO EM CAMINHO — selo circular, arco, texto acompanhando uma forma.
///
/// O caminho vem de um destes tres lugares: um circulo, um arco ou o
/// caminho de OUTRA camada de forma do projeto. O terceiro e o que
/// importa de verdade: desenha-se a curva com os operadores de forma e
/// o texto acompanha, em vez de ter de escolher entre duas curvas
/// prontas.
enum TextPathKind { none, circle, arc, layer }

String textPathKindLabel(TextPathKind k) => switch (k) {
      TextPathKind.none => 'Reto',
      TextPathKind.circle => 'Circulo',
      TextPathKind.arc => 'Arco',
      TextPathKind.layer => 'Camada de forma',
    };

/// Onde a letra fica em relacao a linha do caminho.
enum TextPathAlign { above, on, below }

class TextPathSpec {
  const TextPathSpec({
    this.kind = TextPathKind.none,
    this.radius = 180,
    this.sweepDeg = 180,
    this.startDeg = -90,
    this.shapeLayerId,
    this.offset = 0,
    this.spacing = 0,
    this.align = TextPathAlign.on,
    this.perpendicular = true,
    this.reverse = false,
  });

  final TextPathKind kind;

  /// Circulo e arco.
  final double radius;
  final double sweepDeg;
  final double startDeg;

  /// Caminho vindo de outra camada.
  final String? shapeLayerId;

  /// Desliza o texto ao longo do caminho, em pixels. Animavel pela
  /// camada; e o que faz o selo girar.
  final double offset;

  /// Espaco extra entre letras, em pixels.
  final double spacing;

  final TextPathAlign align;

  /// A letra gira acompanhando a curva. Desligado, ela fica em pe —
  /// que e o que se quer quando o caminho e so uma trilha de posicao.
  final bool perpendicular;

  final bool reverse;

  bool get active => kind != TextPathKind.none;

  TextPathSpec copyWith({
    TextPathKind? kind,
    double? radius,
    double? sweepDeg,
    double? startDeg,
    String? shapeLayerId,
    bool clearShape = false,
    double? offset,
    double? spacing,
    TextPathAlign? align,
    bool? perpendicular,
    bool? reverse,
  }) =>
      TextPathSpec(
        kind: kind ?? this.kind,
        radius: radius ?? this.radius,
        sweepDeg: sweepDeg ?? this.sweepDeg,
        startDeg: startDeg ?? this.startDeg,
        shapeLayerId:
            clearShape ? null : (shapeLayerId ?? this.shapeLayerId),
        offset: offset ?? this.offset,
        spacing: spacing ?? this.spacing,
        align: align ?? this.align,
        perpendicular: perpendicular ?? this.perpendicular,
        reverse: reverse ?? this.reverse,
      );
}

/// Constroi o caminho do circulo ou do arco, centrado na origem.
///
/// O circulo comeca no topo por padrao (startDeg = -90), que e onde a
/// pessoa espera que a primeira letra de um selo apareca.
Path buildTextPath(TextPathSpec spec) {
  final r = math.max(1.0, spec.radius);
  switch (spec.kind) {
    case TextPathKind.circle:
      return Path()
        ..addArc(
          Rect.fromCircle(center: Offset.zero, radius: r),
          spec.startDeg * math.pi / 180,
          2 * math.pi,
        );
    case TextPathKind.arc:
      return Path()
        ..addArc(
          Rect.fromCircle(center: Offset.zero, radius: r),
          spec.startDeg * math.pi / 180,
          spec.sweepDeg * math.pi / 180,
        );
    case TextPathKind.none:
    case TextPathKind.layer:
      return Path();
  }
}

/// Onde uma unidade fica quando o texto segue [path].
typedef GlyphOnPath = ({Offset position, double angleRad});

/// Coloca uma unidade a [distance] pixels do inicio do caminho.
///
/// Devolve null quando a distancia cai fora — e o que faz a letra que
/// nao coube simplesmente nao aparecer, em vez de empilhar na ponta.
GlyphOnPath? placeOnPath(
  Path path,
  double distance, {
  TextPathSpec spec = const TextPathSpec(),
  double glyphHeight = 0,
}) {
  for (final metric in path.computeMetrics()) {
    final len = metric.length;
    if (len <= 0) continue;

    var d = distance;
    if (spec.reverse) d = len - d;
    if (d < 0 || d > len) return null;

    final tan = metric.getTangentForOffset(d);
    if (tan == null) return null;

    final angle = math.atan2(tan.vector.dy, tan.vector.dx);

    // Deslocamento perpendicular para pôr a letra acima ou abaixo da
    // linha, em vez de com a linha cortando o meio dela.
    var pos = tan.position;
    if (spec.align != TextPathAlign.on && glyphHeight > 0) {
      final nrm = Offset(-tan.vector.dy, tan.vector.dx);
      final k = spec.align == TextPathAlign.above
          ? -glyphHeight / 2
          : glyphHeight / 2;
      pos = Offset(pos.dx + nrm.dx * k, pos.dy + nrm.dy * k);
    }

    return (
      position: pos,
      angleRad: spec.perpendicular
          ? (spec.reverse ? angle + math.pi : angle)
          : 0,
    );
  }
  return null;
}

/// Comprimento total de um caminho — usado para saber se o texto cabe.
double pathLength(Path path) {
  var total = 0.0;
  for (final m in path.computeMetrics()) {
    total += m.length;
  }
  return total;
}
