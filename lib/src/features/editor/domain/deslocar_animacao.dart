import 'dart:ui';

import 'cut_ops.dart';
import 'keyframe.dart';
import 'layer.dart';

/// DESLOCA NO TEMPO toda a animacao da camada (transformacao e parametros
/// de efeito) em [delta].
///
/// E o que aparar o inicio e dividir precisam: o tempo local da camada
/// recomeca noutro instante, e os keyframes tem de ir junto para cada um
/// continuar no quadro em que foi feito. Keyframe que cairia antes do zero
/// sai, e o valor daquele instante fica preso no zero com a curva dele.
Layer deslocarAnimacao(Layer layer, Duration delta) {
  if (delta == Duration.zero) return layer;
  AnimatedDouble d(AnimatedDouble t) => deslocarDouble(t, delta);
  AnimatedOffset o(AnimatedOffset t) => deslocarOffset(t, delta);
  return layer.copyLayer(
    position: o(layer.position),
    positionZ: d(layer.positionZ),
    scaleX: d(layer.scaleX),
    scaleY: d(layer.scaleY),
    rotation: d(layer.rotation),
    rotationX: d(layer.rotationX),
    rotationY: d(layer.rotationY),
    opacity: d(layer.opacity),
    skewX: d(layer.skewX),
    skewY: d(layer.skewY),
    pivot: o(layer.pivot),
    effects: [
      for (final e in layer.effects)
        e.copyWith(
          params: {
            for (final entry in e.params.entries)
              entry.key: deslocarDouble(entry.value, delta),
          },
        ),
    ],
  );
}

AnimatedDouble deslocarDouble(AnimatedDouble t, Duration delta) {
  if (!t.isAnimated) return t;
  final noZero = t.valueAt(-delta);
  final easeNoZero = t.easeAt(-delta);
  final novos = <Keyframe<double>>[];
  var presa = false;
  for (final k in t.keyframes) {
    final quando = k.time + delta;
    if (quando < Duration.zero) {
      presa = true;
      continue;
    }
    novos.add(k.copyWith(time: quando));
  }
  if (presa) {
    novos.removeWhere((k) => k.time == Duration.zero);
    novos.insert(
      0,
      Keyframe<double>(time: Duration.zero, value: noZero, ease: easeNoZero),
    );
  }
  return AnimatedDouble(t.base, novos, t.loop, t.expression);
}

AnimatedOffset deslocarOffset(AnimatedOffset t, Duration delta) {
  if (!t.isAnimated) return t;
  final noZero = t.valueAt(-delta);
  final easeNoZero = t.easeAt(-delta);
  final novos = <Keyframe<Offset>>[];
  var presa = false;
  for (final k in t.keyframes) {
    final quando = k.time + delta;
    if (quando < Duration.zero) {
      presa = true;
      continue;
    }
    novos.add(k.copyWith(time: quando));
  }
  if (presa) {
    novos.removeWhere((k) => k.time == Duration.zero);
    novos.insert(
      0,
      Keyframe<Offset>(time: Duration.zero, value: noZero, ease: easeNoZero),
    );
  }
  return AnimatedOffset(t.base, novos, t.loop);
}

/// APARA O INICIO de uma camada em [delta] (positivo), mantendo cada
/// quadro que sobra no mesmo instante absoluto: a midia avanca o ponto de
/// entrada no arquivo, o grupo avanca o conteudo e a animacao desloca.
Layer aparaInicio(Layer layer, Duration delta) {
  if (delta <= Duration.zero) return layer;
  final inicio = layer.startTime + delta;
  final duracao = layer.duration - delta;
  if (duracao <= Duration.zero) return layer;
  switch (layer) {
    case VideoLayer v when hasTimeRemap(v) || v.reverse:
      final corte = sliceVideoTrack(v, delta, v.duration);
      return deslocarAnimacao(
        v.copyLayer(
          startTime: inicio,
          duration: duracao,
          sourceOffset: corte.sourceOffset,
          speed: 1,
          reverse: false,
          effects: replaceTimeRemap(v, corte.track),
        ),
        -delta,
      ).copyLayer(effects: replaceTimeRemap(v, corte.track));
    case VideoLayer v:
      return deslocarAnimacao(
        v.copyLayer(
          startTime: inicio,
          duration: duracao,
          sourceOffset:
              v.sourceOffset +
              Duration(microseconds: (delta.inMicroseconds * v.speed).round()),
        ),
        -delta,
      );
    case AudioLayer a:
      return deslocarAnimacao(
        a.copyLayer(
          startTime: inicio,
          duration: duracao,
          sourceOffset:
              a.sourceOffset +
              Duration(microseconds: (delta.inMicroseconds * a.speed).round()),
        ),
        -delta,
      );
    case GroupLayer g when g.timeRemap == null:
      return deslocarAnimacao(
        g.copyLayer(
          startTime: inicio,
          duration: duracao,
          contentOffset: g.contentOffset + delta,
        ),
        -delta,
      );
    default:
      return deslocarAnimacao(
        layer.copyLayer(startTime: inicio, duration: duracao),
        -delta,
      );
  }
}
