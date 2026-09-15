import 'dart:math' as math;
import 'dart:ui';

import 'package:flutter/animation.dart';

import 'expression_engine.dart';

/// Tipo de easing do segmento (taxonomia oficial do Alight Motion).
enum EasingType {
  cubicBezier,
  bounce,
  elastic,
  cyclic,
  random,
  steps,
  elasticSteps,
  spring,
  hold,
  // Os de baixo entraram na v1.1.1 (familias da curva). Ficam NO FIM:
  // o arquivo do projeto grava o indice.
  /// O quique ao contrario: bate no comeco e decola.
  bounceIn,

  /// O elastico ao contrario: estica no comeco e dispara.
  elasticIn,

  /// Degraus que caem em alturas sorteadas (sempre as mesmas).
  stepsRandom,

  /// Vai e volta [count] vezes e termina no valor final.
  oscillate,

  /// A transicao suave repetida [count] vezes.
  repeat,

  /// A rampa reta repetida [count] vezes.
  sawtooth,
}

/// Easing do SEGMENTO que sai de um keyframe (modelo Alight: N keyframes =
/// N-1 curvas; a curva remapeia tempo->tempo dentro do trecho).
class Easing {
  const Easing({
    this.type = EasingType.cubicBezier,
    this.x1 = 0,
    this.y1 = 0,
    this.x2 = 1,
    this.y2 = 1,
    this.count = 4,
    this.smooth = 1.0,
    this.intensity = 0.5,
    this.response = 0.55,
    this.damping = 0.825,
    this.initialVelocity = 0,
  });

  // Presets bezier (coluna de presets do graph editor).
  static const linear = Easing();
  static const easeIn = Easing(x1: 0.42, y1: 0, x2: 1, y2: 1);
  static const easeOut = Easing(x1: 0, y1: 0, x2: 0.58, y2: 1);
  static const easeInOut = Easing(x1: 0.42, y1: 0, x2: 0.58, y2: 1);
  static const overshoot = Easing(x1: 0.34, y1: 1.56, x2: 0.64, y2: 1);
  static const bounce = Easing(type: EasingType.bounce, count: 4, intensity: .5);
  static const elastic = Easing(
    type: EasingType.elastic,
    count: 3,
    intensity: .65,
  );

  /// Timing usado nas transicoes de interface da Apple. As molas usam o
  /// mesmo vocabulário do SwiftUI (resposta, amortecimento e velocidade
  /// inicial), mas ficam limitadas a 1% para não produzir overshoot visível.
  static const appleStandard = Easing(x1: 0.25, y1: 0.1, x2: 0.25, y2: 1);
  static const appleEntrance = Easing(
    type: EasingType.spring,
    response: 0.55,
    damping: 0.825,
  );
  static const appleExit = Easing(x1: 0.4, y1: 0, x2: 1, y2: 1);
  static const interfaceSpring = Easing(
    type: EasingType.spring,
    response: 0.55,
    damping: 0.825,
    initialVelocity: 0,
  );
  static const softSpring = Easing(
    type: EasingType.spring,
    response: 0.8,
    damping: 1,
  );

  static const bezierPresets = [linear, easeIn, easeOut, easeInOut, overshoot];

  // As familias do painel da curva (v1.1.1).
  static const bounceIn = Easing(
    type: EasingType.bounceIn,
    count: 4,
    intensity: .5,
  );
  static const elasticIn = Easing(
    type: EasingType.elasticIn,
    count: 3,
    intensity: .65,
  );
  static const steps = Easing(type: EasingType.steps, count: 4);
  static const stepsRandom = Easing(
    type: EasingType.stepsRandom,
    count: 5,
    intensity: .8,
  );
  static const elasticSteps = Easing(type: EasingType.elasticSteps, count: 4);
  static const hold = Easing(type: EasingType.hold);
  static const oscillate = Easing(type: EasingType.oscillate, count: 3);
  static const cyclic = Easing(type: EasingType.cyclic, count: 2);
  static const random = Easing(type: EasingType.random, intensity: .8);
  static const repeat = Easing(type: EasingType.repeat, count: 3);
  static const sawtooth = Easing(type: EasingType.sawtooth, count: 3);

  /// A MESMA CURVA NO SENTIDO CONTRARIO: o fim vira o comeco. Nulo quando
  /// o tipo e simetrico e inverter nao mudaria nada.
  Easing? get invertida => switch (type) {
    EasingType.cubicBezier => copyWith(
      x1: (1 - x2).clamp(0.0, 1.0),
      y1: 1 - y2,
      x2: (1 - x1).clamp(0.0, 1.0),
      y2: 1 - y1,
    ),
    EasingType.bounce => copyWith(type: EasingType.bounceIn),
    EasingType.bounceIn => copyWith(type: EasingType.bounce),
    EasingType.elastic => copyWith(type: EasingType.elasticIn),
    EasingType.elasticIn => copyWith(type: EasingType.elastic),
    _ => null,
  };

  final EasingType type;

  /// Alcas cubic-bezier: x em [0,1]; y pode passar de [0,1] com overshoot.
  final double x1, y1, x2, y2;

  /// Steps/elasticSteps: degraus; cyclic: ciclos.
  final int count;

  /// Cyclic: 1 = senoide suave, 0 = linear (dente de serra).
  final double smooth;

  /// Random: forca do ruido.
  final double intensity;

  /// Mola de interface no modelo do SwiftUI.
  final double response;
  final double damping;
  final double initialVelocity;

  bool get isLinear =>
      type == EasingType.cubicBezier &&
      x1 == 0 &&
      y1 == 0 &&
      x2 == 1 &&
      y2 == 1;

  /// Remapeia o progresso t (0..1) do segmento.
  double transform(double t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    switch (type) {
      case EasingType.cubicBezier:
        if (isLinear) return t;
        return Cubic(
          x1.clamp(0.0, 1.0),
          y1,
          x2.clamp(0.0, 1.0),
          y2,
        ).transform(t);
      case EasingType.bounce:
        // QUICAR PARAMETRICO. `count` e quantos toques no chao; `intensity`
        // e quanto cada quique perde de altura (mais intensidade, quiques
        // mais altos por mais tempo). Antes era `Curves.bounceOut` fixo, e
        // os dois parametros existiam sem mudar um pixel — as alcas
        // amarelas do editor de curvas escrevem AQUI.
        final n = count < 1 ? 1 : count;
        final decaimento = math.pow(1 - t, 2.0 * (1.5 - intensity)).toDouble();
        return (1 - decaimento * math.cos(math.pi * n * t).abs()).clamp(
          0.0,
          1.0,
        );
      case EasingType.elastic:
        // ELASTICO PARAMETRICO. `count` e quantas oscilacoes cabem no
        // trecho; `intensity` e quanto elas demoram a se acalmar. Sai de
        // zero e assenta em um, como o `elasticOut` de sempre, mas com os
        // dois numeros de verdade.
        final n = count < 1 ? 1 : count;
        final freio = math.pow(2.0, -10.0 * t / (0.35 + intensity)).toDouble();
        return freio * math.sin((t * n - 0.25) * 2 * math.pi) + 1;
      case EasingType.steps:
        final n = count < 2 ? 2 : count;
        return ((t * n).floor() / (n - 1)).clamp(0.0, 1.0);
      case EasingType.elasticSteps:
        final n = count < 2 ? 2 : count;
        final s = (t * n).floor();
        final frac = t * n - s;
        return ((s + Curves.elasticOut.transform(frac)) / n).clamp(0.0, 1.5);
      case EasingType.cyclic:
        final c = count < 1 ? 1 : count;
        final u = t * c;
        final f = u - u.floorToDouble();
        final smoothed = 0.5 - 0.5 * math.cos(math.pi * f);
        return smooth * smoothed + (1 - smooth) * f;
      case EasingType.random:
        // Ruido deterministico (scrub-estavel): garante f(0)=0 e f(1)=1.
        final envelope = 4 * t * (1 - t);
        final noise = math.sin(t * 27.4) * 0.62 + math.sin(t * 61.7) * 0.38;
        return (t + intensity * 0.3 * envelope * noise).clamp(0.0, 1.0);
      case EasingType.spring:
        return _spring(t);
      case EasingType.hold:
        return t >= 1 ? 1 : 0;
      case EasingType.bounceIn:
        return 1 - copyWith(type: EasingType.bounce).transform(1 - t);
      case EasingType.elasticIn:
        return 1 - copyWith(type: EasingType.elastic).transform(1 - t);
      case EasingType.stepsRandom:
        final n = count < 2 ? 2 : count;
        final k = (t * n).floor().clamp(0, n - 1);
        if (k == n - 1) return 1;
        final base = k / (n - 1);
        // Sorteio fixo por degrau (o scrub nunca muda o desenho).
        final sorteio = math.sin((k + 1) * 12.9898) * 43758.5453;
        final ruido = (sorteio - sorteio.floorToDouble()) - 0.5;
        return (base + ruido * intensity / (n - 1)).clamp(0.0, 1.0);
      case EasingType.oscillate:
        final c = count < 1 ? 1 : count;
        return 0.5 - 0.5 * math.cos(math.pi * (2 * c - 1) * t);
      case EasingType.repeat:
        final c = count < 1 ? 1 : count;
        final u = t * c;
        final f = u - u.floorToDouble();
        return 0.5 - 0.5 * math.cos(math.pi * f);
      case EasingType.sawtooth:
        final c = count < 1 ? 1 : count;
        final u = t * c;
        return u - u.floorToDouble();
    }
  }

  double _spring(double t) {
    final r = response.clamp(0.05, 10.0);
    final zeta = damping.clamp(0.0, 4.0);
    final omega = 2 * math.pi / r;
    double value;
    if ((zeta - 1).abs() < 1e-6) {
      value = 1 - math.exp(-omega * t) * (1 + (omega - initialVelocity) * t);
    } else if (zeta < 1) {
      final wd = omega * math.sqrt(1 - zeta * zeta);
      final c = (zeta * omega - initialVelocity) / wd;
      value =
          1 -
          math.exp(-zeta * omega * t) *
              (math.cos(wd * t) + c * math.sin(wd * t));
    } else {
      final root = math.sqrt(zeta * zeta - 1);
      final a = -omega * (zeta - root);
      final b = -omega * (zeta + root);
      final c2 = (initialVelocity + a) / (b - a);
      final c1 = -1 - c2;
      value = 1 + c1 * math.exp(a * t) + c2 * math.exp(b * t);
    }
    return value.clamp(0.0, 1.009);
  }

  /// VELOCIDADE no progresso [x] (0..1): dy/dx da curva de valor, em
  /// "vezes a velocidade media" — 1 e o ritmo constante do linear.
  ///
  /// Para a bezier a derivada e ANALITICA: acha-se o parametro u com
  /// x(u) = x por bissecao (x(u) e monotona porque x1, x2 estao em 0..1)
  /// e divide-se y'(u) por x'(u). Diferenca finita em cima de
  /// [transform] nao serve: o solver da bezier tem erro de ~1e-3, da
  /// ordem do passo, e a "derivada" vira serrilhado. Nas outras curvas
  /// [transform] e formula fechada, e a diferenca central basta.
  double speedAt(double x) {
    final xc = x.clamp(0.0, 1.0);
    if (type != EasingType.cubicBezier || isLinear) {
      if (type == EasingType.cubicBezier) return 1;
      const h = 1e-3;
      final a = transform((xc - h).clamp(0.0, 1.0));
      final b = transform((xc + h).clamp(0.0, 1.0));
      final dx = (xc + h).clamp(0.0, 1.0) - (xc - h).clamp(0.0, 1.0);
      return dx <= 0 ? 1 : (b - a) / dx;
    }
    final ax = x1.clamp(0.0, 1.0), bx = x2.clamp(0.0, 1.0);
    double px(double u) {
      final v = 1 - u;
      return 3 * v * v * u * ax + 3 * v * u * u * bx + u * u * u;
    }

    double dpx(double u) {
      final v = 1 - u;
      return 3 * v * v * ax + 6 * v * u * (bx - ax) + 3 * u * u * (1 - bx);
    }

    double dpy(double u) {
      final v = 1 - u;
      return 3 * v * v * y1 + 6 * v * u * (y2 - y1) + 3 * u * u * (1 - y2);
    }

    // Bissecao: 40 passos dao u com erro ~1e-12.
    var lo = 0.0, hi = 1.0;
    for (var i = 0; i < 40; i++) {
      final mid = (lo + hi) / 2;
      if (px(mid) < xc) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    final u = (lo + hi) / 2;
    var dx = dpx(u), dy = dpy(u);
    // Ponta degenerada (alca em cima da ancora): a direcao vem da
    // segunda derivada, isto e, da outra alca.
    if (dx.abs() < 1e-9 && dy.abs() < 1e-9) {
      if (u < 0.5) {
        dx = bx;
        dy = y2;
      } else {
        dx = 1 - ax;
        dy = 1 - y1;
      }
      if (dx.abs() < 1e-9 && dy.abs() < 1e-9) return 1;
    }
    if (dx.abs() < 1e-9) {
      return dy >= 0 ? double.infinity : double.negativeInfinity;
    }
    return dy / dx;
  }

  Easing copyWith({
    EasingType? type,
    double? x1,
    double? y1,
    double? x2,
    double? y2,
    int? count,
    double? smooth,
    double? intensity,
    double? response,
    double? damping,
    double? initialVelocity,
  }) {
    return Easing(
      type: type ?? this.type,
      x1: x1 ?? this.x1,
      y1: y1 ?? this.y1,
      x2: x2 ?? this.x2,
      y2: y2 ?? this.y2,
      count: count ?? this.count,
      smooth: smooth ?? this.smooth,
      intensity: intensity ?? this.intensity,
      response: response ?? this.response,
      damping: damping ?? this.damping,
      initialVelocity: initialVelocity ?? this.initialVelocity,
    );
  }

  String get label => switch (type) {
    EasingType.cubicBezier => isLinear ? 'Linear' : 'Bezier',
    EasingType.bounce => 'Quicar',
    EasingType.elastic => 'Elastico',
    EasingType.cyclic => 'Ciclico',
    EasingType.random => 'Aleatorio',
    EasingType.steps => 'Degraus',
    EasingType.elasticSteps => 'Deg. elastico',
    EasingType.spring => 'Mola',
    EasingType.hold => 'Manter',
    EasingType.bounceIn => 'Quique na entrada',
    EasingType.elasticIn => 'Elástico na entrada',
    EasingType.stepsRandom => 'Degraus aleatórios',
    EasingType.oscillate => 'Oscilar',
    EasingType.repeat => 'Repetir',
    EasingType.sawtooth => 'Dente de serra',
  };
}

class Keyframe<T> {
  const Keyframe({
    required this.time,
    required this.value,
    this.ease = Easing.linear,
  });

  /// Tempo local a camada (0 = inicio da camada).
  final Duration time;
  final T value;
  final Easing ease;

  Keyframe<T> copyWith({Duration? time, T? value, Easing? ease}) => Keyframe(
    time: time ?? this.time,
    value: value ?? this.value,
    ease: ease ?? this.ease,
  );
}

/// Mesmo frame se a diferenca for menor que isto.
///
/// PUBLICA porque ha quem decida sobre um instante INTEIRO da camada: o
/// losango da linha do tempo junta marcas de muitas trilhas, e decidir se
/// ele se arrasta tem de usar a mesma regua de "mesmo instante" que cada
/// trilha usa por dentro.
const kToleranciaDoKeyframe = Duration(milliseconds: 8);

const _epsilon = kToleranciaDoKeyframe;

/// Nucleo compartilhado da avaliacao: busca binaria + segmento com easing.
/// As listas sao sempre mantidas ordenadas por tempo.
(T, T, double)? _segmentAt<T>(List<Keyframe<T>> kfs, Duration t) {
  if (t <= kfs.first.time) return null; // antes do primeiro
  if (t >= kfs.last.time) return null; // depois do ultimo
  var lo = 0;
  var hi = kfs.length - 1;
  while (hi - lo > 1) {
    final mid = (lo + hi) >> 1;
    if (kfs[mid].time <= t) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  final a = kfs[lo];
  final b = kfs[hi];
  final span = (b.time - a.time).inMicroseconds;
  var f = span == 0 ? 1.0 : (t - a.time).inMicroseconds / span;
  f = a.ease.transform(f.clamp(0.0, 1.0));
  return (a.value, b.value, f);
}

List<Keyframe<T>> _insertSorted<T>(List<Keyframe<T>> kfs, Keyframe<T> kf) {
  final out = [
    for (final k in kfs)
      if ((k.time - kf.time).abs() >= _epsilon) k,
    kf,
  ]..sort((a, b) => a.time.compareTo(b.time));
  return List.unmodifiable(out);
}

List<Keyframe<T>> _removeAt<T>(List<Keyframe<T>> kfs, Duration t) {
  return List.unmodifiable([
    for (final k in kfs)
      if ((k.time - t).abs() >= _epsilon) k,
  ]);
}

/// MOVE UMA MARCA NO TEMPO, com o valor e a curva dela.
///
/// Apagar e cravar de novo — `withoutKeyframe(de).withKeyframe(para, v)` —
/// perdia a CURVA: a marca renascia linear, e o trecho que sai dela mudava
/// de desenho so por ter mudado de lugar. Aqui a marca e a mesma e so o
/// tempo muda. A marca anterior, dona do trecho que chega, fica com a
/// curva dela; as outras nao trocam de ordem entre si.
///
/// Nulo quando nao ha marca em [de] (dentro da tolerancia) e quando ja ha
/// OUTRA marca em [para]: juntar duas marcas num instante so apagaria uma
/// delas em silencio, e isso nao se desfaz olhando para a linha do tempo.
List<Keyframe<T>>? moverMarcaNaLista<T>(
  List<Keyframe<T>> kfs,
  Duration de,
  Duration para,
) {
  // A marca que anda e a mais perto de [de], se estiver dentro da
  // tolerancia.
  var indice = -1;
  var menor = _epsilon;
  for (var i = 0; i < kfs.length; i++) {
    final d = (kfs[i].time - de).abs();
    if (d < menor) {
      menor = d;
      indice = i;
    }
  }
  if (indice < 0) return null;
  for (var i = 0; i < kfs.length; i++) {
    if (i != indice && (kfs[i].time - para).abs() < _epsilon) return null;
  }
  final movida = kfs[indice].copyWith(time: para);
  final out = [...kfs]..removeAt(indice);
  // Insercao no lugar, e nao sort: o sort nao promete estabilidade, e
  // marcas que por acaso tenham o mesmo tempo nao podem trocar de ordem.
  var onde = out.indexWhere((k) => k.time > para);
  if (onde < 0) onde = out.length;
  out.insert(onde, movida);
  return out;
}

/// Segmento de keyframe em que um tempo cai (PR-G0 do Modulo Grid):
/// valor anterior, valor seguinte e fracao JA com easing aplicado.
/// E o que permite o "caminho mais curto": animar um alvo de 1 a 4 vai
/// direto, sem passar por 2 e 3.
class KeyframeSegment {
  const KeyframeSegment(this.from, this.to, this.fraction);

  final double from;
  final double to;
  final double fraction;
}

/// Propriedade double animavel (escala, rotacao, opacidade...).
/// LOOP DE KEYFRAMES (spec motion-graphics-pro, PR-X6). E o recurso mais
/// usado do sistema de expressoes do AE: dois keyframes em Ciclo ja sao
/// uma animacao infinita, sem encher a timeline.
enum LoopMode {
  /// Sem loop: segura o primeiro/ultimo valor (comportamento padrao).
  none,

  /// Repete do inicio.
  cycle,

  /// Repete alternando a direcao.
  pingPong,

  /// Repete SOMANDO o delta total a cada volta — movimento continuo
  /// (esteira, marquee).
  offset,

  /// Mantem a VELOCIDADE do ultimo trecho, indefinidamente.
  continueValue,
}

/// Onde o loop vale.
enum LoopWhen { after, before, both }

class LoopSpec {
  const LoopSpec({
    this.mode = LoopMode.none,
    this.when = LoopWhen.after,
    this.count = 0,
  });

  final LoopMode mode;
  final LoopWhen when;

  /// Quantos keyframes finais (ou iniciais) participam do ciclo.
  /// 0 = todos.
  final int count;

  bool get active => mode != LoopMode.none;

  bool get loopsAfter =>
      active && (when == LoopWhen.after || when == LoopWhen.both);
  bool get loopsBefore =>
      active && (when == LoopWhen.before || when == LoopWhen.both);

  LoopSpec copyWith({LoopMode? mode, LoopWhen? when, int? count}) => LoopSpec(
    mode: mode ?? this.mode,
    when: when ?? this.when,
    count: count ?? this.count,
  );

  static const none = LoopSpec();
}

/// Resultado do remapeamento de tempo do loop: em que instante DENTRO do
/// trecho ciclado amostrar, e quanto somar ao valor (modo Deslocado).
typedef _LoopSample = ({Duration time, double cycles});

/// Remapeia [t] para dentro do trecho [from]..[to], conforme o modo.
/// Devolve null quando o loop nao se aplica.
_LoopSample? _loopRemap(
  LoopSpec loop,
  Duration t,
  Duration first,
  Duration last,
  Duration from,
  Duration to,
) {
  if (!loop.active) return null;
  final span = to - from;
  if (span <= Duration.zero) return null;

  if (t > last && loop.loopsAfter) {
    final over = t - last;
    final spanUs = span.inMicroseconds;
    final n = over.inMicroseconds ~/ spanUs;
    final rem = Duration(microseconds: over.inMicroseconds % spanUs);
    switch (loop.mode) {
      case LoopMode.cycle:
      case LoopMode.offset:
        return (time: from + rem, cycles: (n + 1).toDouble());
      case LoopMode.pingPong:
        // Voltas impares correm de tras para frente.
        final backwards = n.isEven;
        return (time: backwards ? to - rem : from + rem, cycles: 0);
      case LoopMode.continueValue:
      case LoopMode.none:
        return null;
    }
  }

  if (t < first && loop.loopsBefore) {
    final under = first - t;
    final spanUs = span.inMicroseconds;
    final n = under.inMicroseconds ~/ spanUs;
    final rem = Duration(microseconds: under.inMicroseconds % spanUs);
    switch (loop.mode) {
      case LoopMode.cycle:
      case LoopMode.offset:
        return (time: to - rem, cycles: -(n + 1).toDouble());
      case LoopMode.pingPong:
        final forwards = n.isEven;
        return (time: forwards ? from + rem : to - rem, cycles: 0);
      case LoopMode.continueValue:
      case LoopMode.none:
        return null;
    }
  }
  return null;
}

class AnimatedDouble {
  AnimatedDouble(
    this.base, [
    List<Keyframe<double>>? keyframes,
    this.loop = LoopSpec.none,
    this.expression,
  ]) : keyframes = List.unmodifiable(keyframes ?? const <Keyframe<double>>[]);

  final double base;
  final List<Keyframe<double>> keyframes;

  /// Loop dos keyframes (PR-X6).
  final LoopSpec loop;

  /// A EXPRESSAO, quando ha. Roda por cima do valor dos keyframes: dentro
  /// dela, `value` e o que a propriedade teria sem ela e `time` e o
  /// tempo local em segundos — o mesmo contrato do After Effects.
  ///
  /// Mora AQUI, e nao no efeito, porque aqui vale para tudo que anima:
  /// parametro de efeito, transformacao, animador de texto. Quem le
  /// [valueAt] recebe a expressao aplicada sem saber que ela existe.
  final String? expression;

  bool get isAnimated => keyframes.isNotEmpty;
  bool get hasExpression => expression != null && expression!.trim().isNotEmpty;

  bool hasKeyframeAt(Duration t) =>
      keyframes.any((k) => (k.time - t).abs() < _epsilon);

  /// O erro de compilacao da expressao, se houver. Nulo = boa, ou ausente.
  ExpressionError? get expressionError =>
      hasExpression ? _compilada(expression!).$2 : null;

  /// O ultimo erro de EXECUCAO por texto de expressao: compila, mas
  /// falha ao rodar. Fica fora do objeto (que e imutavel) para a
  /// interface poder mostrar, em vez de a falha sumir calada.
  static final Map<String, ExpressionError> runtimeErrors = {};

  /// Compilada UMA vez por texto. `valueAt` roda por quadro e por
  /// parametro; compilar ali seria o fim do preview. O teto evita que
  /// uma expressao editada letra a letra encha a memoria de arvores.
  static final Map<String, (CompiledExpression?, ExpressionError?)> _cache = {};
  static (CompiledExpression?, ExpressionError?) _compilada(String fonte) {
    final pronta = _cache[fonte];
    if (pronta != null) return pronta;
    if (_cache.length >= 64) _cache.remove(_cache.keys.first);
    return _cache[fonte] = CompiledExpression.tentar(fonte);
  }

  /// O valor com a expressao aplicada — o que todo consumidor le.
  double valueAt(Duration t) {
    final raw = _rawAt(t);
    if (!hasExpression) return raw;
    final fonte = expression!;
    final (prog, _) = _compilada(fonte);
    if (prog == null) return raw;
    final r = ExpressionEvaluator(
      prog,
      ExpressionContext(time: t.inMicroseconds / 1000000, value: raw),
    ).avaliar();
    if (!r.deuCerto) {
      runtimeErrors[fonte] = r.erro!;
      return raw;
    }
    runtimeErrors.remove(fonte);
    final v = r.comoNumero(raw);
    return v.isFinite ? v : raw;
  }

  /// O valor SO dos keyframes, sem a expressao.
  double _rawAt(Duration t) {
    if (keyframes.isEmpty) return base;
    final first = keyframes.first;
    final last = keyframes.last;

    // LOOP (PR-X6): fora do intervalo dos keyframes, o tempo e
    // remapeado para dentro do trecho ciclado.
    if (loop.active && keyframes.length >= 2) {
      if (loop.mode == LoopMode.continueValue) {
        // Mantem a velocidade do trecho da ponta, indefinidamente.
        if (t > last.time && loop.loopsAfter) {
          final prev = keyframes[keyframes.length - 2];
          final dt = (last.time - prev.time).inMicroseconds;
          if (dt <= 0) return last.value;
          final v = (last.value - prev.value) / dt;
          return last.value + v * (t - last.time).inMicroseconds;
        }
        if (t < first.time && loop.loopsBefore) {
          final next = keyframes[1];
          final dt = (next.time - first.time).inMicroseconds;
          if (dt <= 0) return first.value;
          final v = (next.value - first.value) / dt;
          return first.value - v * (first.time - t).inMicroseconds;
        }
      } else {
        final (from, to) = _loopRange(t);
        final s = _loopRemap(loop, t, first.time, last.time, from, to);
        if (s != null) {
          final delta = valueAt(to) - valueAt(from);
          return _rawValueAt(s.time) +
              (loop.mode == LoopMode.offset ? delta * s.cycles : 0);
        }
      }
    }
    return _rawValueAt(t);
  }

  /// Trecho que participa do loop: os [LoopSpec.count] keyframes da
  /// ponta (0 = todos).
  (Duration, Duration) _loopRange(Duration t) {
    final n = keyframes.length;
    final c = loop.count;
    if (c <= 0 || c >= n) {
      return (keyframes.first.time, keyframes.last.time);
    }
    // Depois do fim usa os ULTIMOS c; antes do inicio, os PRIMEIROS c.
    if (t > keyframes.last.time) {
      return (keyframes[n - c].time, keyframes.last.time);
    }
    return (keyframes.first.time, keyframes[c - 1].time);
  }

  double _rawValueAt(Duration t) {
    if (t <= keyframes.first.time) return keyframes.first.value;
    if (t >= keyframes.last.time) return keyframes.last.value;
    final (a, b, f) = _segmentAt(keyframes, t)!;
    return lerpDouble(a, b, f)!;
  }

  /// Entre quais keyframes [t] esta (null fora de um segmento) — PR-G0.
  KeyframeSegment? segmentAt(Duration t) {
    if (keyframes.length < 2) return null;
    if (t <= keyframes.first.time || t >= keyframes.last.time) return null;
    final (a, b, f) = _segmentAt(keyframes, t)!;
    return KeyframeSegment(a, b, f);
  }

  AnimatedDouble withBase(double v) =>
      AnimatedDouble(v, keyframes, loop, expression);

  /// Poe, troca ou tira (com nulo) a expressao.
  AnimatedDouble withExpression(String? fonte) =>
      AnimatedDouble(base, keyframes, loop, fonte);

  /// Liga/desliga o loop dos keyframes (PR-X6).
  AnimatedDouble withLoop(LoopSpec spec) =>
      AnimatedDouble(base, keyframes, spec, expression);

  AnimatedDouble withKeyframe(
    Duration t,
    double v, [
    Easing ease = Easing.linear,
  ]) => AnimatedDouble(
    base,
    _insertSorted(keyframes, Keyframe(time: t, value: v, ease: ease)),
    loop,
    expression,
  );

  AnimatedDouble withoutKeyframe(Duration t) {
    final rest = _removeAt(keyframes, t);
    // Removeu o ultimo keyframe: volta a ser estatico no valor atual.
    // O valor CRU: tirar o ultimo keyframe nao pode assar a expressao
    // dentro da base — ela continua viva, por cima.
    if (rest.isEmpty) return AnimatedDouble(_rawAt(t), null, loop, expression);
    return AnimatedDouble(base, rest, loop, expression);
  }

  /// A MARCA DE [de] VAI PARA [para], com o valor e a curva dela.
  ///
  /// A propria trilha, intacta, quando nao ha marca em [de] ou quando ja
  /// ha outra marca em [para] (ver [moverMarcaNaLista]). Intacta quer
  /// dizer o MESMO objeto: quem move o instante inteiro de uma camada
  /// descobre assim, sem comparar valores, que trilhas nao tinham nada ali.
  AnimatedDouble comKeyframeMovido(Duration de, Duration para) {
    if (de == para) return this;
    final novas = moverMarcaNaLista(keyframes, de, para);
    if (novas == null) return this;
    return AnimatedDouble(base, novas, loop, expression);
  }

  /// EDITAR UM VALOR NUNCA CRIA KEYFRAME.
  ///
  /// Regra do produto, acima da convencao do After Effects
  /// (`docs/keyframe-explicito.md`). Aqui estava a raiz do defeito:
  ///
  ///     isAnimated ? withKeyframe(t, v, easeAt(t)) : withBase(v)
  ///
  /// Toda propriedade JA ANIMADA cravava um keyframe sozinha a cada
  /// mudanca de valor, em qualquer instante, sem depender de
  /// interruptor nenhum e sem nenhum sinal na tela. Pior: o keyframe
  /// recem-nascido entrava LINEAR (`easeAt` devolve linear quando nao
  /// ha marca ali), achatando a curva do trecho ao redor.
  ///
  /// Agora sao tres casos, e so tres:
  ///
  ///   estatica ............ muda a base
  ///   animada, SOBRE marca  atualiza AQUELA marca, com a curva dela
  ///   animada, FORA de marca  NAO MEXE — quem crava e o losango
  ///
  /// O terceiro caso devolve `this` de proposito: quem chama decide o
  /// que fazer com a edicao recusada (ver a edicao pendente no
  /// controller). Silencio aqui seria um controle inerte; e por isso
  /// que existe [aceitaEdicaoEm].
  AnimatedDouble edited(Duration t, double v) {
    if (!isAnimated) return withBase(v);
    if (hasKeyframeAt(t)) return withKeyframe(t, v, easeAt(t));
    return this;
  }

  /// A edicao em [t] chega ao projeto, ou precisa do losango antes?
  ///
  /// Falso quer dizer "animada e fora de uma marca": [edited] devolve a
  /// trilha intacta, e quem chama tem de tratar a recusa.
  bool aceitaEdicaoEm(Duration t) => !isAnimated || hasKeyframeAt(t);

  /// Easing do keyframe em [t] (ou o padrao, se nao houver).
  Easing easeAt(Duration t) {
    for (final k in keyframes) {
      if ((k.time - t).abs() < _epsilon) return k.ease;
    }
    return Easing.linear;
  }

  /// Troca o easing do keyframe em [t], se existir.
  AnimatedDouble withEase(Duration t, Easing ease) => AnimatedDouble(
    base,
    [
      for (final k in keyframes)
        if ((k.time - t).abs() < _epsilon) k.copyWith(ease: ease) else k,
    ],
    loop,
    expression,
  );

  /// Aplica a curva a TODOS os segmentos ("Paste Curve to All Keyframes").
  AnimatedDouble withEaseAll(Easing ease) => AnimatedDouble(
    base,
    [for (final k in keyframes) k.copyWith(ease: ease)],
    loop,
    expression,
  );

  /// INVERTER NO TEMPO (assistente PR-X7): espelha os keyframes dentro
  /// do proprio intervalo — o percurso passa a correr de tras para
  /// frente, mantendo as posicoes no tempo.
  AnimatedDouble reversedInTime() {
    if (keyframes.length < 2) return this;
    final first = keyframes.first.time;
    final last = keyframes.last.time;
    final flipped = <Keyframe<double>>[
      for (final k in keyframes)
        Keyframe(time: first + (last - k.time), value: k.value, ease: k.ease),
    ]..sort((a, b) => a.time.compareTo(b.time));
    return AnimatedDouble(base, flipped, loop, expression);
  }
}

/// Propriedade Offset animavel (posicao, ancora...).
class AnimatedOffset {
  AnimatedOffset(
    this.base, [
    List<Keyframe<Offset>>? keyframes,
    this.loop = LoopSpec.none,
  ]) : keyframes = List.unmodifiable(keyframes ?? const <Keyframe<Offset>>[]);

  final Offset base;
  final List<Keyframe<Offset>> keyframes;

  /// Loop dos keyframes (PR-X6).
  final LoopSpec loop;

  bool get isAnimated => keyframes.isNotEmpty;

  bool hasKeyframeAt(Duration t) =>
      keyframes.any((k) => (k.time - t).abs() < _epsilon);

  Offset valueAt(Duration t) {
    if (keyframes.isEmpty) return base;
    final first = keyframes.first;
    final last = keyframes.last;

    if (loop.active && keyframes.length >= 2) {
      if (loop.mode == LoopMode.continueValue) {
        if (t > last.time && loop.loopsAfter) {
          final prev = keyframes[keyframes.length - 2];
          final dt = (last.time - prev.time).inMicroseconds.toDouble();
          if (dt <= 0) return last.value;
          final over = (t - last.time).inMicroseconds.toDouble();
          return last.value + (last.value - prev.value) / dt * over;
        }
        if (t < first.time && loop.loopsBefore) {
          final next = keyframes[1];
          final dt = (next.time - first.time).inMicroseconds.toDouble();
          if (dt <= 0) return first.value;
          final under = (first.time - t).inMicroseconds.toDouble();
          return first.value - (next.value - first.value) / dt * under;
        }
      } else {
        final (from, to) = _loopRange(t);
        final s = _loopRemap(loop, t, first.time, last.time, from, to);
        if (s != null) {
          final delta = _rawValueAt(to) - _rawValueAt(from);
          return _rawValueAt(s.time) +
              (loop.mode == LoopMode.offset ? delta * s.cycles : Offset.zero);
        }
      }
    }
    return _rawValueAt(t);
  }

  (Duration, Duration) _loopRange(Duration t) {
    final n = keyframes.length;
    final c = loop.count;
    if (c <= 0 || c >= n) {
      return (keyframes.first.time, keyframes.last.time);
    }
    if (t > keyframes.last.time) {
      return (keyframes[n - c].time, keyframes.last.time);
    }
    return (keyframes.first.time, keyframes[c - 1].time);
  }

  Offset _rawValueAt(Duration t) {
    if (t <= keyframes.first.time) return keyframes.first.value;
    if (t >= keyframes.last.time) return keyframes.last.value;
    final (a, b, f) = _segmentAt(keyframes, t)!;
    return Offset.lerp(a, b, f)!;
  }

  AnimatedOffset withBase(Offset v) => AnimatedOffset(v, keyframes, loop);

  AnimatedOffset withLoop(LoopSpec spec) =>
      AnimatedOffset(base, keyframes, spec);

  AnimatedOffset withKeyframe(
    Duration t,
    Offset v, [
    Easing ease = Easing.linear,
  ]) => AnimatedOffset(
    base,
    _insertSorted(keyframes, Keyframe(time: t, value: v, ease: ease)),
    loop,
  );

  AnimatedOffset withoutKeyframe(Duration t) {
    final rest = _removeAt(keyframes, t);
    if (rest.isEmpty) return AnimatedOffset(valueAt(t));
    return AnimatedOffset(base, rest, loop);
  }

  /// A mesma regra de [AnimatedDouble.comKeyframeMovido].
  AnimatedOffset comKeyframeMovido(Duration de, Duration para) {
    if (de == para) return this;
    final novas = moverMarcaNaLista(keyframes, de, para);
    if (novas == null) return this;
    return AnimatedOffset(base, novas, loop);
  }

  /// A mesma regra de [AnimatedDouble.edited]: editar valor nunca cria
  /// keyframe.
  AnimatedOffset edited(Duration t, Offset v) {
    if (!isAnimated) return withBase(v);
    if (hasKeyframeAt(t)) return withKeyframe(t, v, easeAt(t));
    return this;
  }

  bool aceitaEdicaoEm(Duration t) => !isAnimated || hasKeyframeAt(t);

  Easing easeAt(Duration t) {
    for (final k in keyframes) {
      if ((k.time - t).abs() < _epsilon) return k.ease;
    }
    return Easing.linear;
  }

  AnimatedOffset withEase(Duration t, Easing ease) => AnimatedOffset(base, [
    for (final k in keyframes)
      if ((k.time - t).abs() < _epsilon) k.copyWith(ease: ease) else k,
  ], loop);

  AnimatedOffset withEaseAll(Easing ease) => AnimatedOffset(base, [
    for (final k in keyframes) k.copyWith(ease: ease),
  ], loop);

  /// Inverter no tempo (PR-X7).
  AnimatedOffset reversedInTime() {
    if (keyframes.length < 2) return this;
    final first = keyframes.first.time;
    final last = keyframes.last.time;
    final flipped = <Keyframe<Offset>>[
      for (final k in keyframes)
        Keyframe(time: first + (last - k.time), value: k.value, ease: k.ease),
    ]..sort((a, b) => a.time.compareTo(b.time));
    return AnimatedOffset(base, flipped, loop);
  }
}
