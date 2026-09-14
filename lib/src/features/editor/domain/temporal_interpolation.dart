import 'dart:math' as math;

import 'cut_ops.dart';
import 'effect.dart';
import 'layer.dart';

InterpolacaoDeQuadros interpolacaoEfetiva(VideoLayer layer) {
  for (final effect in layer.effects) {
    if (effect.type == EffectType.opticalFlow) {
      return effect.enabled
          ? InterpolacaoDeQuadros.movimento
          : InterpolacaoDeQuadros.nenhuma;
    }
  }
  return layer.interpolacao;
}

int fatorDeInterpolacao(VideoLayer layer) {
  if (interpolacaoEfetiva(layer) == InterpolacaoDeQuadros.nenhuma) return 1;
  final lenta = velocidadeMaisLenta(layer);
  if (!lenta.isFinite || lenta <= 0 || lenta >= 1) return 1;
  return (1 / lenta).ceil().clamp(1, 4);
}

/// A menor velocidade (fonte por tempo da composicao) que o clipe
/// atinge: a propria velocidade, ou a inclinacao mais rasa entre dois
/// keyframes do time remap.
double velocidadeMaisLenta(VideoLayer layer) {
  final track = timeRemapTrackOf(layer);
  if (track == null) return layer.speed.abs();
  final ks = track.keyframes;
  if (ks.length < 2) return layer.speed.abs();
  var menor = double.infinity;
  for (var i = 0; i + 1 < ks.length; i++) {
    final dt = (ks[i + 1].time - ks[i].time).inMicroseconds / 1000000.0;
    if (dt <= 0) continue;
    final ds = (ks[i + 1].value - ks[i].value).abs();
    // Trecho parado (quadro segurado) nao e camera lenta: e um quadro
    // so, e inventar quadros entre dois iguais nao muda nada.
    if (ds < 1e-6) continue;
    menor = math.min(menor, ds / dt);
  }
  return menor;
}

/// O FILTRO que inventa os quadros, ja com a virgula no fim para entrar
/// na frente da receita de extracao. Vazio quando nao ha o que inventar.
///
/// `mci` estima o movimento e desloca os pixels; `blend` so mistura os
/// vizinhos. Os dois sao do proprio ffmpeg — nada e escrito aqui, e e
/// por isso que funciona igual no Android e no iOS.
String filtroDeInterpolacao(VideoLayer layer, {required int fps}) {
  final fator = fatorDeInterpolacao(layer);
  if (fator <= 1) return '';
  final modo = switch (interpolacaoEfetiva(layer)) {
    // "IA" sem RIFE no aparelho tambem cai aqui: fluxo optico do ffmpeg.
    InterpolacaoDeQuadros.movimento ||
    InterpolacaoDeQuadros.ia => 'mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1:scd=fdiff:scd_threshold=10',
    _ => 'mi_mode=blend',
  };
  return 'minterpolate=fps=${fps * fator}:$modo,';
}
