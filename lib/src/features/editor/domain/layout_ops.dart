import 'dart:math' as math;
import 'dart:ui';

/// PRECISAO E LAYOUT (spec motion-graphics-pro, bloco 1): motion graphics
/// e 60% posicionamento exato, e no dedo nao fica exato. Estas operacoes
/// sao funcoes PURAS sobre caixas — o resultado e exato ao pixel e
/// verificavel numericamente.

/// Uma camada para fins de layout: o centro (que e o que o compositor
/// posiciona) e o tamanho da caixa renderizada.
typedef LayoutBox = ({String id, Offset center, Size size});

Rect _rectOf(LayoutBox b) => Rect.fromCenter(
    center: b.center, width: b.size.width, height: b.size.height);

enum AlignEdge { left, centerH, right, top, centerV, bottom }

/// A que a selecao se alinha.
enum AlignTo {
  /// A caixa da composicao inteira.
  composition,

  /// A caixa que envolve as camadas selecionadas.
  selection,

  /// Uma camada ancora da selecao (a primeira informada).
  anchor,
}

Rect _referenceRect(
  List<LayoutBox> boxes,
  AlignTo to,
  Size compSize,
  String? anchorId,
) {
  switch (to) {
    case AlignTo.composition:
      return Offset.zero & compSize;
    case AlignTo.anchor:
      final a = boxes.where((b) => b.id == anchorId);
      if (a.isNotEmpty) return _rectOf(a.first);
      return _referenceRect(boxes, AlignTo.selection, compSize, null);
    case AlignTo.selection:
      Rect? acc;
      for (final b in boxes) {
        final r = _rectOf(b);
        acc = acc == null ? r : acc.expandToInclude(r);
      }
      return acc ?? (Offset.zero & compSize);
  }
}

/// ALINHAR: devolve o novo CENTRO de cada camada (so as que mudam).
/// Alinhar pela borda usa a caixa real, entao camadas de tamanhos
/// diferentes encostam a borda no mesmo pixel.
Map<String, Offset> alignLayers(
  List<LayoutBox> boxes,
  AlignEdge edge, {
  AlignTo to = AlignTo.composition,
  Size compSize = const Size(1080, 1080),
  String? anchorId,
}) {
  if (boxes.isEmpty) return const {};
  final ref = _referenceRect(boxes, to, compSize, anchorId);
  final out = <String, Offset>{};
  for (final b in boxes) {
    final half = Offset(b.size.width / 2, b.size.height / 2);
    final c = b.center;
    final moved = switch (edge) {
      AlignEdge.left => Offset(ref.left + half.dx, c.dy),
      AlignEdge.centerH => Offset(ref.center.dx, c.dy),
      AlignEdge.right => Offset(ref.right - half.dx, c.dy),
      AlignEdge.top => Offset(c.dx, ref.top + half.dy),
      AlignEdge.centerV => Offset(c.dx, ref.center.dy),
      AlignEdge.bottom => Offset(c.dx, ref.bottom - half.dy),
    };
    if (moved != c) out[b.id] = moved;
  }
  return out;
}

enum DistributeAxis { horizontal, vertical }

/// As DUAS distribuicoes sao operacoes diferentes quando as camadas tem
/// tamanhos distintos — a spec insiste, e com razao: uma iguala os
/// CENTROS, a outra iguala os VAOS. Quem faz layout sente a diferenca.
enum DistributeMode { byCenter, byGap }

/// DISTRIBUIR: mantem as duas extremidades no lugar e reposiciona o
/// miolo. Menos de 3 camadas nao tem o que distribuir.
Map<String, Offset> distributeLayers(
  List<LayoutBox> boxes,
  DistributeAxis axis,
  DistributeMode mode,
) {
  if (boxes.length < 3) return const {};
  final horizontal = axis == DistributeAxis.horizontal;
  final sorted = [...boxes]..sort((a, b) => horizontal
      ? a.center.dx.compareTo(b.center.dx)
      : a.center.dy.compareTo(b.center.dy));

  final out = <String, Offset>{};
  final n = sorted.length;

  if (mode == DistributeMode.byCenter) {
    final firstC =
        horizontal ? sorted.first.center.dx : sorted.first.center.dy;
    final lastC =
        horizontal ? sorted.last.center.dx : sorted.last.center.dy;
    final step = (lastC - firstC) / (n - 1);
    for (var i = 1; i < n - 1; i++) {
      final b = sorted[i];
      final v = firstC + step * i;
      final moved = horizontal
          ? Offset(v, b.center.dy)
          : Offset(b.center.dx, v);
      if (moved != b.center) out[b.id] = moved;
    }
    return out;
  }

  // Por VAO igual: o espaco livre entre as extremidades e dividido em
  // (n-1) partes iguais, descontando o tamanho de cada camada.
  final startEdge = horizontal
      ? _rectOf(sorted.first).right
      : _rectOf(sorted.first).bottom;
  final endEdge =
      horizontal ? _rectOf(sorted.last).left : _rectOf(sorted.last).top;
  var inner = 0.0;
  for (var i = 1; i < n - 1; i++) {
    inner += horizontal ? sorted[i].size.width : sorted[i].size.height;
  }
  final gap = (endEdge - startEdge - inner) / (n - 1);
  var cursor = startEdge + gap;
  for (var i = 1; i < n - 1; i++) {
    final b = sorted[i];
    final extent = horizontal ? b.size.width : b.size.height;
    final v = cursor + extent / 2;
    final moved =
        horizontal ? Offset(v, b.center.dy) : Offset(b.center.dx, v);
    if (moved != b.center) out[b.id] = moved;
    cursor += extent + gap;
  }
  return out;
}

/// SEQUENCIAR CAMADAS (assistente PR-X7): escalona o inicio de N
/// camadas, com sobreposicao opcional. Montar uma cascata de 20
/// elementos vira um comando em vez de 20 arrastes.
///
/// [overlap] em 0 encosta uma na outra; 0,5 sobrepoe metade; negativo
/// abre intervalo.
Map<String, Duration> sequenceStarts(
  List<({String id, Duration start, Duration duration})> layers, {
  double overlap = 0,
  Duration? step,
}) {
  if (layers.isEmpty) return const {};
  final out = <String, Duration>{};
  var cursor = layers.first.start;
  for (final l in layers) {
    out[l.id] = cursor;
    final advance = step ??
        Duration(
            microseconds:
                (l.duration.inMicroseconds * (1 - overlap)).round());
    cursor += advance;
  }
  return out;
}

/// DISTRIBUIR NO TEMPO (assistente PR-X7): espalha os inicios
/// uniformemente entre o primeiro e o ultimo.
Map<String, Duration> distributeInTime(
  List<({String id, Duration start})> layers,
) {
  if (layers.length < 3) return const {};
  final sorted = [...layers]..sort((a, b) => a.start.compareTo(b.start));
  final first = sorted.first.start;
  final last = sorted.last.start;
  final stepUs =
      (last - first).inMicroseconds / (sorted.length - 1);
  final out = <String, Duration>{};
  for (var i = 1; i < sorted.length - 1; i++) {
    out[sorted[i].id] =
        first + Duration(microseconds: (stepUs * i).round());
  }
  return out;
}

/// ESCALA EXPONENCIAL (assistente PR-X7): converte a escala linear entre
/// dois valores em exponencial — e o zoom que "parece natural", porque a
/// percepcao de tamanho e logaritmica.
double exponentialScaleAt(double from, double to, double t) {
  final a = from <= 0 ? 0.0001 : from;
  final b = to <= 0 ? 0.0001 : to;
  final f = t.clamp(0.0, 1.0);
  // Interpolacao geometrica: razao constante por unidade de tempo.
  return a * math.pow(b / a, f);
}

/// ESPACAMENTO EXATO: encosta as camadas em sequencia com [gap] px entre
/// elas, a partir da primeira (que nao se move).
Map<String, Offset> spaceLayers(
  List<LayoutBox> boxes,
  DistributeAxis axis,
  double gap,
) {
  if (boxes.length < 2) return const {};
  final horizontal = axis == DistributeAxis.horizontal;
  final sorted = [...boxes]..sort((a, b) => horizontal
      ? a.center.dx.compareTo(b.center.dx)
      : a.center.dy.compareTo(b.center.dy));

  final out = <String, Offset>{};
  var cursor = horizontal
      ? _rectOf(sorted.first).right
      : _rectOf(sorted.first).bottom;
  for (var i = 1; i < sorted.length; i++) {
    final b = sorted[i];
    final extent = horizontal ? b.size.width : b.size.height;
    final v = cursor + gap + extent / 2;
    final moved =
        horizontal ? Offset(v, b.center.dy) : Offset(b.center.dx, v);
    if (moved != b.center) out[b.id] = moved;
    cursor += gap + extent;
  }
  return out;
}
