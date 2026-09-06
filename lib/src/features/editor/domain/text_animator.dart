import 'dart:math' as math;

import 'package:characters/characters.dart';
import 'package:flutter/animation.dart';
import 'package:uuid/uuid.dart';

import 'keyframe.dart';

/// Motor de animadores de texto (PR-T1/PR-T3 da spec AM2-motor-de-texto):
/// multi-seletores por animador combinados por modo, seletor Wiggly com
/// ruido puro, matematica de cobertura por unidade.
///
/// Invariantes:
/// - I1 Determinismo: cobertura e ruido sao funcao pura de
///   (seed, indice, tempo); nada acumula estado entre frames.
/// - I2 Neutralidade: cobertura 0 ou propriedades neutras nao alteram nada.

enum SelectorMode { add, subtract, intersect, min, max, difference }

enum SelectorUnits { percent, byIndex }

enum SelectorBasedOn { characters, charactersNoSpaces, words, lines }

enum SelectorShape { square, rampUp, rampDown, triangle, round, smooth }

/// Ordem em que as unidades sao percorridas (PR-R1 da spec
/// autoria-de-texto): remapeia o INDICE da unidade antes de calcular a
/// posicao no seletor. E o que destrava "do centro", "das bordas" e
/// "aleatoria" sem tocar em mais nada do motor.
enum SelectorOrder { identity, inverse, center, edges, random }

/// Rank da unidade [i] quando a ordem parte do CENTRO: distancia
/// crescente do meio, desempate pela esquerda. Formula fechada (O(1)) —
/// isto roda por unidade, por frame.
int _centerRank(int i, int n) {
  final mid = (n - 1) ~/ 2;
  if (n.isOdd) {
    final d = (i - mid).abs();
    if (d == 0) return 0;
    return i < mid ? 2 * d - 1 : 2 * d;
  }
  if (i <= mid) return 2 * (mid - i);
  return 2 * (i - mid - 1) + 1;
}

/// Indice efetivo da unidade [i] sob a ordem [order].
int orderMapIndex(SelectorOrder order, int i, int n, int seed) {
  if (n <= 1) return 0;
  return switch (order) {
    SelectorOrder.identity => i,
    SelectorOrder.inverse => n - 1 - i,
    SelectorOrder.center => _centerRank(i, n),
    // Bordas e o inverso de centro: as pontas primeiro, o meio por ultimo.
    SelectorOrder.edges => n - 1 - _centerRank(i, n),
    SelectorOrder.random => seededPermutation(seed, n)[i],
  };
}

/// Um seletor produz cobertura c em [0,1] (ou fora, com overshoot) por
/// unidade de texto.
sealed class TextSelector {
  TextSelector({
    String? id,
    this.mode = SelectorMode.add,
    this.basedOn = SelectorBasedOn.characters,
  }) : id = id ?? const Uuid().v4();

  final String id;
  final SelectorMode mode;
  final SelectorBasedOn basedOn;

  /// Cobertura da unidade [i] entre [n] unidades no tempo local [t].
  double coverageAt(int i, int n, Duration t);
}

/// Bezier cubica 1D (remapeia cobertura por easeHigh/easeLow).
double _cubicBezier1D(double c, double p1x, double p1y, double p2x, double p2y) {
  if (c <= 0) return 0;
  if (c >= 1) return 1;
  return Cubic(p1x.clamp(0.0, 1.0), p1y, p2x.clamp(0.0, 1.0), p2y)
      .transform(c);
}

/// Permutacao Fisher-Yates deterministica de (seed, n).
/// PRNG proprio (xorshift32) para o resultado nao depender da plataforma.
List<int> seededPermutation(int seed, int n) {
  final out = List<int>.generate(n, (i) => i);
  var s = (seed == 0 ? 0x9E3779B9 : seed) & 0xFFFFFFFF;
  int next() {
    s ^= (s << 13) & 0xFFFFFFFF;
    s ^= (s >> 17);
    s ^= (s << 5) & 0xFFFFFFFF;
    return s;
  }

  for (var i = n - 1; i > 0; i--) {
    final j = next() % (i + 1);
    final tmp = out[i];
    out[i] = out[j];
    out[j] = tmp;
  }
  return out;
}

class RangeSelector extends TextSelector {
  RangeSelector({
    super.id,
    super.mode,
    super.basedOn,
    this.units = SelectorUnits.percent,
    AnimatedDouble? start,
    AnimatedDouble? end,
    AnimatedDouble? offset,
    this.shape = SelectorShape.square,
    AnimatedDouble? amount,
    AnimatedDouble? smoothness,
    AnimatedDouble? easeHigh,
    AnimatedDouble? easeLow,
    this.randomizeOrder = false,
    this.randomSeed = 1,
    this.order = SelectorOrder.identity,
    this.holdBeyond = false,
  })  : start = start ?? AnimatedDouble(0),
        end = end ?? AnimatedDouble(1),
        offset = offset ?? AnimatedDouble(0),
        amount = amount ?? AnimatedDouble(100),
        smoothness = smoothness ?? AnimatedDouble(100),
        easeHigh = easeHigh ?? AnimatedDouble(0),
        easeLow = easeLow ?? AnimatedDouble(0);

  final SelectorUnits units;

  /// PERCENT: 0..1. INDEX: em unidades.
  final AnimatedDouble start;
  final AnimatedDouble end;
  final AnimatedDouble offset;
  final SelectorShape shape;

  /// -100..100.
  final AnimatedDouble amount;

  /// 0..100 (so SQUARE).
  final AnimatedDouble smoothness;

  /// -100..100.
  final AnimatedDouble easeHigh;
  final AnimatedDouble easeLow;

  /// Legado: equivale a [SelectorOrder.random].
  final bool randomizeOrder;
  final int randomSeed;

  /// Ordem de percurso das unidades (PR-R1).
  final SelectorOrder order;

  /// Unidade AINDA NAO alcancada pela janela (p > hi) mantem cobertura
  /// cheia em vez de zero. Sem isto nao existe animacao de ENTRADA: quem
  /// ainda nao entrou apareceria no estado neutro (ou seja, visivel).
  /// O que ja passou (p < lo) continua em zero — ja entrou.
  final bool holdBeyond;

  @override
  double coverageAt(int i, int n, Duration t) {
    if (n <= 0) return 0;
    final effectiveOrder =
        randomizeOrder ? SelectorOrder.random : order;
    final index = orderMapIndex(effectiveOrder, i, n, randomSeed);
    final p = (index + 0.5) / n;

    final s = start.valueAt(t);
    final e = end.valueAt(t);
    final o = offset.valueAt(t);
    double lo, hi;
    if (units == SelectorUnits.percent) {
      lo = math.min(s, e) + o;
      hi = math.max(s, e) + o;
    } else {
      lo = math.min(s, e) / n + o / n;
      hi = math.max(s, e) / n + o / n;
    }
    final w = hi - lo;
    if (w <= 0) return 0;

    final tt = (p - lo) / w;
    double c;
    if (tt > 1 && holdBeyond) {
      c = 1;
    } else if (tt < 0 || tt > 1) {
      c = 0;
    } else {
      c = switch (shape) {
        SelectorShape.square => _square(p, lo, hi, n, t),
        SelectorShape.rampUp => tt,
        SelectorShape.rampDown => 1 - tt,
        SelectorShape.triangle => 1 - (2 * tt - 1).abs(),
        SelectorShape.round =>
          math.sqrt(math.max(0, 1 - math.pow(2 * tt - 1, 2))),
        SelectorShape.smooth => 0.5 - 0.5 * math.cos(2 * math.pi * tt),
      };
    }

    // Ease high/low: remapeia a cobertura.
    final eL = (easeLow.valueAt(t) / 100).clamp(-1.0, 1.0);
    final eH = (easeHigh.valueAt(t) / 100).clamp(-1.0, 1.0);
    if (eL != 0 || eH != 0) {
      c = _cubicBezier1D(
        c,
        math.max(0, eL),
        math.max(0, -eL),
        1 - math.max(0, eH),
        1 - math.max(0, -eH),
      );
    }

    return c * (amount.valueAt(t) / 100);
  }

  double _square(double p, double lo, double hi, int n, Duration t) {
    final sm = (smoothness.valueAt(t) / 100).clamp(0.0, 1.0);
    if (sm == 0) return 1;
    final u = 1 / n;
    final a = ((p - lo) / (sm * u)).clamp(0.0, 1.0);
    final b = ((hi - p) / (sm * u)).clamp(0.0, 1.0);
    return a * b;
  }

  RangeSelector copyWith({
    SelectorMode? mode,
    SelectorBasedOn? basedOn,
    SelectorUnits? units,
    AnimatedDouble? start,
    AnimatedDouble? end,
    AnimatedDouble? offset,
    SelectorShape? shape,
    AnimatedDouble? amount,
    AnimatedDouble? smoothness,
    AnimatedDouble? easeHigh,
    AnimatedDouble? easeLow,
    bool? randomizeOrder,
    int? randomSeed,
    SelectorOrder? order,
    bool? holdBeyond,
  }) {
    return RangeSelector(
      id: id,
      order: order ?? this.order,
      holdBeyond: holdBeyond ?? this.holdBeyond,
      mode: mode ?? this.mode,
      basedOn: basedOn ?? this.basedOn,
      units: units ?? this.units,
      start: start ?? this.start,
      end: end ?? this.end,
      offset: offset ?? this.offset,
      shape: shape ?? this.shape,
      amount: amount ?? this.amount,
      smoothness: smoothness ?? this.smoothness,
      easeHigh: easeHigh ?? this.easeHigh,
      easeLow: easeLow ?? this.easeLow,
      randomizeOrder: randomizeOrder ?? this.randomizeOrder,
      randomSeed: randomSeed ?? this.randomSeed,
    );
  }
}

/// Ruido de valor 2D puro: hash (seed, x, y) -> [0,1], interpolado suave.
double valueNoise01(int seed, double x, double y) {
  double hash(int xi, int yi) {
    var h = seed & 0xFFFFFFFF;
    h = (h ^ (xi * 0x27D4EB2F)) & 0xFFFFFFFF;
    h = (h * 0x85EBCA6B) & 0xFFFFFFFF;
    h = (h ^ (yi * 0x165667B1)) & 0xFFFFFFFF;
    h = (h * 0xC2B2AE35) & 0xFFFFFFFF;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 0xFFFFFF;
  }

  final x0 = x.floor();
  final y0 = y.floor();
  double smooth(double v) => v * v * (3 - 2 * v);
  final fx = smooth(x - x0);
  final fy = smooth(y - y0);
  final a = hash(x0, y0);
  final b = hash(x0 + 1, y0);
  final c = hash(x0, y0 + 1);
  final d = hash(x0 + 1, y0 + 1);
  final top = a + (b - a) * fx;
  final bottom = c + (d - c) * fx;
  return top + (bottom - top) * fy;
}

class WigglySelector extends TextSelector {
  WigglySelector({
    super.id,
    super.mode,
    super.basedOn,
    AnimatedDouble? maxAmount,
    AnimatedDouble? minAmount,
    AnimatedDouble? wigglesPerSecond,
    AnimatedDouble? correlation,
    AnimatedDouble? temporalPhase,
    AnimatedDouble? spatialPhase,
    this.lockDimensions = false,
    this.randomSeed = 1,
  })  : maxAmount = maxAmount ?? AnimatedDouble(100),
        minAmount = minAmount ?? AnimatedDouble(-100),
        wigglesPerSecond = wigglesPerSecond ?? AnimatedDouble(2),
        correlation = correlation ?? AnimatedDouble(50),
        temporalPhase = temporalPhase ?? AnimatedDouble(0),
        spatialPhase = spatialPhase ?? AnimatedDouble(0);

  final AnimatedDouble maxAmount;
  final AnimatedDouble minAmount;
  final AnimatedDouble wigglesPerSecond;

  /// 0..100: 0 = cada unidade treme sozinha; 100 = todas juntas.
  final AnimatedDouble correlation;
  final AnimatedDouble temporalPhase;
  final AnimatedDouble spatialPhase;
  final bool lockDimensions;
  final int randomSeed;

  @override
  double coverageAt(int i, int n, Duration t) {
    final seconds = t.inMicroseconds / 1e6;
    final corr = (correlation.valueAt(t) / 100).clamp(0.0, 1.0);
    final u = i * (1 - corr) + spatialPhase.valueAt(t) / 360;
    final tau =
        seconds * wigglesPerSecond.valueAt(t) + temporalPhase.valueAt(t) / 360;

    final shared = valueNoise01(randomSeed, 0, tau);
    final own = valueNoise01(randomSeed, u + 1, tau);
    final noise = own + (shared - own) * corr;

    final lo = minAmount.valueAt(t);
    final hi = maxAmount.valueAt(t);
    return (lo + noise * (hi - lo)) / 100;
  }

  WigglySelector copyWith({
    SelectorMode? mode,
    SelectorBasedOn? basedOn,
    AnimatedDouble? maxAmount,
    AnimatedDouble? minAmount,
    AnimatedDouble? wigglesPerSecond,
    AnimatedDouble? correlation,
    AnimatedDouble? temporalPhase,
    AnimatedDouble? spatialPhase,
    bool? lockDimensions,
    int? randomSeed,
  }) {
    return WigglySelector(
      id: id,
      mode: mode ?? this.mode,
      basedOn: basedOn ?? this.basedOn,
      maxAmount: maxAmount ?? this.maxAmount,
      minAmount: minAmount ?? this.minAmount,
      wigglesPerSecond: wigglesPerSecond ?? this.wigglesPerSecond,
      correlation: correlation ?? this.correlation,
      temporalPhase: temporalPhase ?? this.temporalPhase,
      spatialPhase: spatialPhase ?? this.spatialPhase,
      lockDimensions: lockDimensions ?? this.lockDimensions,
      randomSeed: randomSeed ?? this.randomSeed,
    );
  }
}

/// Combina as coberturas dos seletores (§4.6): o PRIMEIRO define a base,
/// seja qual for o modo dele (desvio consciente do AE).
double combinedCoverage(
  List<TextSelector> selectors,
  int i,
  int n,
  Duration t, {
  bool allowOvershoot = false,
}) {
  return combinedCoverageWith(
    selectors,
    (s) => s.coverageAt(i, n, t),
    allowOvershoot: allowOvershoot,
  );
}

/// Variante em que cada seletor calcula a propria cobertura (necessario
/// quando cada um tem uma base diferente: caractere vs palavra vs linha).
double combinedCoverageWith(
  List<TextSelector> selectors,
  double Function(TextSelector) coverageOf, {
  bool allowOvershoot = false,
}) {
  if (selectors.isEmpty) return 0;
  var acc = coverageOf(selectors.first);
  for (var k = 1; k < selectors.length; k++) {
    final c = coverageOf(selectors[k]);
    acc = switch (selectors[k].mode) {
      SelectorMode.add => acc + c,
      SelectorMode.subtract => acc - c,
      SelectorMode.intersect => acc * c,
      SelectorMode.min => math.min(acc, c),
      SelectorMode.max => math.max(acc, c),
      SelectorMode.difference => (acc - c).abs(),
    };
  }
  return allowOvershoot ? acc : acc.clamp(0.0, 1.0);
}

/// Segmentacao do texto em unidades (grapheme clusters via `characters`,
/// nunca code units — §3.1) com mapeamento para palavra e linha logica.
class TextUnits {
  TextUnits._({
    required this.clusters,
    required this.codeUnitStart,
    required this.codeUnitEnd,
    required this.isWhitespace,
    required this.charIndex,
    required this.charNoSpaceIndex,
    required this.wordIndex,
    required this.lineIndex,
    required this.charCount,
    required this.charNoSpaceCount,
    required this.wordCount,
    required this.lineCount,
  });

  factory TextUnits.of(String text) {
    final clusters = <String>[];
    final starts = <int>[];
    final ends = <int>[];
    var offset = 0;
    for (final c in text.characters) {
      clusters.add(c);
      starts.add(offset);
      offset += c.length;
      ends.add(offset);
    }

    final isWs = [
      for (final c in clusters) c.trim().isEmpty,
    ];

    final charIdx = List<int>.filled(clusters.length, -1);
    final charNoSpaceIdx = List<int>.filled(clusters.length, -1);
    final wordIdx = List<int>.filled(clusters.length, -1);
    final lineIdx = List<int>.filled(clusters.length, 0);

    var chars = 0, noSpaces = 0, words = 0, lines = 0;
    var inWord = false;
    for (var i = 0; i < clusters.length; i++) {
      if (clusters[i] == '\n') {
        lineIdx[i] = lines;
        lines++;
        inWord = false;
        charIdx[i] = chars++;
        continue;
      }
      lineIdx[i] = lines;
      charIdx[i] = chars++;
      if (!isWs[i]) {
        charNoSpaceIdx[i] = noSpaces++;
        if (!inWord) {
          inWord = true;
          words++;
        }
        wordIdx[i] = words - 1;
      } else {
        inWord = false;
      }
    }

    return TextUnits._(
      clusters: clusters,
      codeUnitStart: starts,
      codeUnitEnd: ends,
      isWhitespace: isWs,
      charIndex: charIdx,
      charNoSpaceIndex: charNoSpaceIdx,
      wordIndex: wordIdx,
      lineIndex: lineIdx,
      charCount: chars,
      charNoSpaceCount: noSpaces,
      wordCount: words,
      lineCount: lines + 1,
    );
  }

  final List<String> clusters;
  final List<int> codeUnitStart;
  final List<int> codeUnitEnd;
  final List<bool> isWhitespace;
  final List<int> charIndex;
  final List<int> charNoSpaceIndex;
  final List<int> wordIndex;
  final List<int> lineIndex;
  final int charCount;
  final int charNoSpaceCount;
  final int wordCount;
  final int lineCount;

  int get length => clusters.length;

  /// (indice, total) da unidade [i] na base pedida; indice -1 = a unidade
  /// nao conta nessa base (ex.: espaco em charactersNoSpaces) -> c = 0.
  (int, int) indexFor(int i, SelectorBasedOn basedOn) => switch (basedOn) {
        SelectorBasedOn.characters => (charIndex[i], charCount),
        SelectorBasedOn.charactersNoSpaces =>
          (charNoSpaceIndex[i], charNoSpaceCount),
        SelectorBasedOn.words => (wordIndex[i], wordCount),
        SelectorBasedOn.lines => (lineIndex[i], lineCount),
      };

  /// Cobertura combinada dos seletores para a unidade [i], respeitando a
  /// base de CADA seletor.
  double coverageFor(
    List<TextSelector> selectors,
    int i,
    Duration t, {
    bool allowOvershoot = false,
  }) {
    return combinedCoverageWith(
      selectors,
      (s) {
        final (idx, count) = indexFor(i, s.basedOn);
        if (idx < 0 || count <= 0) return 0;
        return s.coverageAt(idx, count, t);
      },
      allowOvershoot: allowOvershoot,
    );
  }
}

/// Propriedades animaveis pelo animador.
///
/// As seis primeiras sao o nucleo antigo; as demais entraram com o
/// animador novo, porque sem DESFOQUE, COR e ESCALA POR EIXO metade das
/// animacoes de texto que as pessoas querem sao impossiveis de montar.
enum TextAnimProp {
  positionX,
  positionY,
  scale, // %; multiplicativa (uniforme)
  rotation, // graus; aditiva
  opacity, // %; multiplicativa
  tracking, // px; aditivo no avanco

  scaleX, // %; multiplicativa
  scaleY, // %; multiplicativa
  blur, // px; aditivo
  skew, // graus; aditivo
  hue, // graus; aditivo
  saturation, // %; multiplicativa
  brightness, // %; multiplicativa

  // 3D POR UNIDADE: cada letra, palavra ou frase gira no espaco e se
  // afasta/aproxima da camera. E o "texto 3D" de motion — a palavra
  // que vira como uma porta, a frase que vem de longe.
  rotationX, // graus; aditivo
  rotationY, // graus; aditivo
  positionZ, // px; aditivo (positivo = longe)
}

String textAnimPropLabel(TextAnimProp p) => switch (p) {
      TextAnimProp.positionX => 'Posicao X',
      TextAnimProp.positionY => 'Posicao Y',
      TextAnimProp.scale => 'Escala',
      TextAnimProp.rotation => 'Rotacao',
      TextAnimProp.opacity => 'Opacidade',
      TextAnimProp.tracking => 'Espacamento',
      TextAnimProp.scaleX => 'Escala X',
      TextAnimProp.scaleY => 'Escala Y',
      TextAnimProp.blur => 'Desfoque',
      TextAnimProp.skew => 'Inclinacao',
      TextAnimProp.hue => 'Matiz',
      TextAnimProp.saturation => 'Saturacao',
      TextAnimProp.brightness => 'Brilho',
      TextAnimProp.rotationX => 'Rotacao X (3D)',
      TextAnimProp.rotationY => 'Rotacao Y (3D)',
      TextAnimProp.positionZ => 'Posicao Z (3D)',
    };

class AnimatorProperty {
  AnimatorProperty({
    String? id,
    required this.type,
    AnimatedDouble? value,
  })  : id = id ?? const Uuid().v4(),
        value = value ?? AnimatedDouble(_neutralOf(type));

  final String id;
  final TextAnimProp type;
  final AnimatedDouble value;

  AnimatorProperty copyWith({AnimatedDouble? value}) =>
      AnimatorProperty(id: id, type: type, value: value ?? this.value);

  static double _neutralOf(TextAnimProp type) => switch (type) {
        TextAnimProp.scale ||
        TextAnimProp.opacity ||
        TextAnimProp.scaleX ||
        TextAnimProp.scaleY ||
        TextAnimProp.saturation ||
        TextAnimProp.brightness =>
          100,
        _ => 0,
      };

  double get neutral => _neutralOf(type);

  /// Escala, opacidade, saturacao e brilho sao MULTIPLICATIVAS; o resto
  /// e aditivo. Em ambos os casos cobertura zero devolve a base intacta
  /// — e o que garante a invariante de neutralidade.
  static bool isMultiplicative(TextAnimProp type) => switch (type) {
        TextAnimProp.scale ||
        TextAnimProp.opacity ||
        TextAnimProp.scaleX ||
        TextAnimProp.scaleY ||
        TextAnimProp.saturation ||
        TextAnimProp.brightness =>
          true,
        _ => false,
      };

  /// Combina o valor base da unidade com este animador sob cobertura [c].
  double apply(double base, Duration t, double c) {
    final v = value.valueAt(t);
    if (isMultiplicative(type)) return base * (1 + (v / 100 - 1) * c);
    return base + v * c;
  }
}

/// Animador: N seletores combinados + N propriedades aplicadas em pilha.
class TextAnimator {
  TextAnimator({
    String? id,
    this.name = 'Animador',
    this.enabled = true,
    List<TextSelector>? selectors,
    List<AnimatorProperty>? properties,
    this.allowOvershoot = false,
  })  : id = id ?? const Uuid().v4(),
        selectors = List.unmodifiable(selectors ?? [RangeSelector()]),
        properties = List.unmodifiable(properties ?? const []);

  final String id;
  final String name;
  final bool enabled;
  final List<TextSelector> selectors;
  final List<AnimatorProperty> properties;
  final bool allowOvershoot;

  double coverageAt(int i, int n, Duration t) => enabled
      ? combinedCoverage(selectors, i, n, t, allowOvershoot: allowOvershoot)
      : 0;

  TextAnimator copyWith({
    String? name,
    bool? enabled,
    List<TextSelector>? selectors,
    List<AnimatorProperty>? properties,
    bool? allowOvershoot,
  }) {
    return TextAnimator(
      id: id,
      name: name ?? this.name,
      enabled: enabled ?? this.enabled,
      selectors: selectors ?? this.selectors,
      properties: properties ?? this.properties,
      allowOvershoot: allowOvershoot ?? this.allowOvershoot,
    );
  }
}

// ===================================================================
/// Em que ordem as unidades entram.
enum TextAnimOrder { forward, reverse, center, edges, random }

String textAnimOrderLabel(TextAnimOrder o) => switch (o) {
      TextAnimOrder.forward => 'Do inicio',
      TextAnimOrder.reverse => 'Do fim',
      TextAnimOrder.center => 'Do centro',
      TextAnimOrder.edges => 'Das bordas',
      TextAnimOrder.random => 'Aleatoria',
    };

SelectorOrder selectorOrderFor(TextAnimOrder o) => switch (o) {
      TextAnimOrder.forward => SelectorOrder.identity,
      TextAnimOrder.reverse => SelectorOrder.inverse,
      TextAnimOrder.center => SelectorOrder.center,
      TextAnimOrder.edges => SelectorOrder.edges,
      TextAnimOrder.random => SelectorOrder.random,
    };

/// A curva de cada unidade.
enum TextAnimEase { linear, suave, acelerar, desacelerar, mola, quicar }

String textAnimEaseLabel(TextAnimEase e) => switch (e) {
      TextAnimEase.linear => 'Linear',
      TextAnimEase.suave => 'Suave',
      TextAnimEase.acelerar => 'Acelerar',
      TextAnimEase.desacelerar => 'Desacelerar',
      TextAnimEase.mola => 'Mola',
      TextAnimEase.quicar => 'Quicar',
    };

/// MOLA — a mesma conta da extensao MultiTools:
///
///   s = amplitude * cos(freq * t * 2pi) / exp(decay * t)
///
/// Ali ela e a quantidade do seletor; aqui ela vira a CURVA da unidade,
/// que e a mesma coisa vista do outro lado: comeca deslocada, cruza o
/// alvo, passa um pouco, e volta — o overshoot que da vida ao movimento.
double springEase(
  double u, {
  double amplitude = 1,
  double frequency = 1.8,
  double decay = 5,
}) {
  if (u <= 0) return 0;
  final t = u;
  return 1 -
      amplitude *
          math.cos(frequency * t * 2 * math.pi) *
          math.exp(-decay * t);
}

/// Quique de bola: cai e bate, sem passar do alvo.
double bounceEase(double u) {
  if (u <= 0) return 0;
  if (u >= 1) return 1;
  const n = 7.5625, d = 2.75;
  var x = u;
  if (x < 1 / d) return n * x * x;
  if (x < 2 / d) return n * (x -= 1.5 / d) * x + 0.75;
  if (x < 2.5 / d) return n * (x -= 2.25 / d) * x + 0.9375;
  return n * (x -= 2.625 / d) * x + 0.984375;
}

double applyEase(
  TextAnimEase ease,
  double u, {
  double amplitude = 1,
  double frequency = 1.8,
  double decay = 5,
}) {
  final x = u.clamp(0.0, 1.0);
  return switch (ease) {
    TextAnimEase.linear => x,
    TextAnimEase.suave => Curves.easeInOut.transform(x),
    TextAnimEase.acelerar => Curves.easeIn.transform(x),
    TextAnimEase.desacelerar => Curves.easeOut.transform(x),
    TextAnimEase.mola => springEase(x,
        amplitude: amplitude, frequency: frequency, decay: decay),
    TextAnimEase.quicar => bounceEase(x),
  };
}

// -------------------------------------------------------- o seletor

/// A forma da onda de uma ENFASE (animacao que fica repetindo).
enum LoopShape { sine, triangle, square, pulse, noise }

/// SELETOR ESCALONADO: a cobertura de cada unidade vem direto de
/// (inicio, duracao, atraso, ordem, curva).
///
/// O seletor de faixa do AE consegue a mesma coisa, mas exige converter
/// tempo em porcentagem de janela na cabeca — que e exatamente o que
/// tornava o animador antigo insuportavel de usar.
class StaggerSelector extends TextSelector {
  StaggerSelector({
    super.id,
    super.mode,
    super.basedOn,
    this.start = Duration.zero,
    this.duration = const Duration(milliseconds: 600),
    this.stagger = const Duration(milliseconds: 60),
    this.order = TextAnimOrder.forward,
    this.seed = 1,
    this.ease = TextAnimEase.suave,
    this.amplitude = 1,
    this.frequency = 1.8,
    this.decay = 5,
    this.startCovered = true,
    this.loop = false,
    this.loopShape = LoopShape.sine,
  });

  final Duration start;
  final Duration duration;
  final Duration stagger;
  final TextAnimOrder order;
  final int seed;
  final TextAnimEase ease;
  final double amplitude;
  final double frequency;
  final double decay;

  /// ENTRADA comeca COBERTA (a unidade nasce deslocada e caminha para o
  /// neutro). SAIDA comeca neutra e vai para a coberta. Sem essa
  /// distincao, animacao de entrada mostraria tudo pronto no frame 0.
  final bool startCovered;

  /// ENFASE: em vez de ir de um estado ao outro, oscila para sempre.
  final bool loop;
  final LoopShape loopShape;

  @override
  double coverageAt(int i, int n, Duration t) {
    if (n <= 0) return 0;
    final idx = orderMapIndex(selectorOrderFor(order), i, n, seed);
    final t0us = start.inMicroseconds + stagger.inMicroseconds * idx;
    final dus = duration.inMicroseconds;
    if (dus <= 0) {
      // Duracao zero = maquina de escrever: liga de uma vez.
      final on = t.inMicroseconds >= t0us;
      if (loop) return on ? 1 : 0;
      return startCovered ? (on ? 0 : 1) : (on ? 1 : 0);
    }

    final raw = (t.inMicroseconds - t0us) / dus;

    if (loop) {
      // Fase continua: cada unidade entra defasada da anterior.
      final phase = raw - raw.floorToDouble();
      return _loopValue(phase < 0 ? phase + 1 : phase, idx);
    }

    final e = applyEase(ease, raw.clamp(0.0, 1.0),
        amplitude: amplitude, frequency: frequency, decay: decay);
    return startCovered ? 1 - e : e;
  }

  double _loopValue(double p, int idx) {
    switch (loopShape) {
      case LoopShape.sine:
        return 0.5 - 0.5 * math.cos(p * 2 * math.pi);
      case LoopShape.triangle:
        return 1 - (2 * p - 1).abs();
      case LoopShape.square:
        return p < 0.5 ? 1 : 0;
      case LoopShape.pulse:
        // Um pico curto e um longo descanso — o "piscar".
        return p < 0.18 ? 0.5 - 0.5 * math.cos(p / 0.18 * 2 * math.pi) : 0;
      case LoopShape.noise:
        // Ruido deterministico por unidade e por fase (invariante I1).
        final a = _hash(idx * 7919 + (p * 64).floor());
        final b = _hash(idx * 7919 + (p * 64).floor() + 1);
        final f = (p * 64) - (p * 64).floorToDouble();
        return a + (b - a) * (f * f * (3 - 2 * f));
    }
  }

  static double _hash(int x) {
    var h = x * 374761393 + 668265263;
    h = (h ^ (h >> 13)) * 1274126177;
    return ((h ^ (h >> 16)) & 0x7fffffff) / 0x7fffffff;
  }

  StaggerSelector copyWith({
    Duration? start,
    Duration? duration,
    Duration? stagger,
    TextAnimOrder? order,
    int? seed,
    TextAnimEase? ease,
    double? amplitude,
    double? frequency,
    double? decay,
    bool? startCovered,
    bool? loop,
    LoopShape? loopShape,
    SelectorBasedOn? basedOn,
  }) =>
      StaggerSelector(
        id: id,
        mode: mode,
        basedOn: basedOn ?? this.basedOn,
        start: start ?? this.start,
        duration: duration ?? this.duration,
        stagger: stagger ?? this.stagger,
        order: order ?? this.order,
        seed: seed ?? this.seed,
        ease: ease ?? this.ease,
        amplitude: amplitude ?? this.amplitude,
        frequency: frequency ?? this.frequency,
        decay: decay ?? this.decay,
        startCovered: startCovered ?? this.startCovered,
        loop: loop ?? this.loop,
        loopShape: loopShape ?? this.loopShape,
      );
}
