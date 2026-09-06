import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/effect.dart';

/// KEYFRAME UNIVERSAL: o efeito tem um diamante so, e cada keyframe
/// guarda todos os parametros — marca no inicio, mexe no fim, e tudo
/// anima junto sem pensar em qual parametro tem diamante.
void main() {
  const t0 = Duration.zero;
  const t1 = Duration(seconds: 2);

  EffectInstance glow() => EffectInstance(type: EffectType.glowVol);

  test('efeito sem animacao: editar so muda o valor, nao cria keyframe', () {
    final e = glow().withParamEdited('radius', t1, 0.8);
    expect(e.hasAnimation, isFalse);
    expect(e.paramAt('radius', t0), closeTo(0.8, 1e-9));
    expect(e.paramAt('radius', t1), closeTo(0.8, 1e-9));
  });

  test('o diamante liga o keyframe em TODOS os parametros', () {
    final e = glow().withKeyframeToggled(t0);
    expect(e.hasKeyframeAt(t0), isTrue);
    for (final k in e.spec.params.keys) {
      expect(e.track(k).hasKeyframeAt(t0), isTrue, reason: k);
    }
    // E desliga de todos.
    final sem = e.withKeyframeToggled(t0);
    expect(sem.hasKeyframeAt(t0), isFalse);
    expect(sem.hasAnimation, isFalse);
  });

  test('marca no inicio, mexe no fim: um keyframe com tudo dentro', () {
    var e = glow().withKeyframeToggled(t0);
    final expoInicio = e.paramAt('exposure', t0);
    e = e.withParamEdited('radius', t1, 0.9);
    // O instante editado tem keyframe em todos os parametros.
    expect(e.hasKeyframeAt(t1), isTrue);
    for (final k in e.spec.params.keys) {
      expect(e.track(k).hasKeyframeAt(t1), isTrue, reason: k);
    }
    // O editado interpola; os outros seguem constantes.
    expect(e.paramAt('radius', t1), closeTo(0.9, 1e-9));
    expect(e.paramAt('radius', const Duration(seconds: 1)),
        greaterThan(e.paramAt('radius', t0)));
    expect(e.paramAt('exposure', t1), closeTo(expoInicio, 1e-9));
  });

  test('editar de novo no mesmo instante so troca o valor do keyframe', () {
    var e = glow().withKeyframeToggled(t0).withParamEdited('radius', t1, 0.9);
    e = e.withParamEdited('radius', t1, 0.4);
    expect(e.track('radius').keyframes.length, 2);
    expect(e.paramAt('radius', t1), closeTo(0.4, 1e-9));
  });

  test('os tempos de keyframe do efeito sao a uniao, sem repetir o instante',
      () {
    final e = glow().withKeyframeToggled(t0).withParamEdited('radius', t1, 0.9);
    final tempos = e.keyframeTimes.toSet();
    expect(tempos, {t0, t1});
  });
}
