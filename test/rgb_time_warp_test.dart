// RGB TIME WARP: cada canal de um instante.
//
// O QUE SE PROVA AQUI:
//
//   1. os tres deslocamentos sao independentes, com sinal, e o desligado
//      devolve zero nos tres;
//   2. o deslocamento e em QUADROS: o mesmo numero anda o mesmo tanto em
//      24, 30 e 60 fps;
//   3. o mesmo instante devolve o mesmo, em qualquer ordem de leitura;
//   4. a EXPORTACAO e avisada dos instantes de fora — sem isso os tres
//      canais mostrariam o mesmo quadro e o efeito nao faria nada;
//   5. a ficha nao perde parametro.
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/rgb_time_warp.dart';
import 'package:aurea/src/features/editor/domain/time_slice.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

EffectInstance _warp({
  double r = 0,
  double g = 0,
  double b = 0,
  double mix = 100,
  bool ligado = true,
}) {
  var e = EffectInstance(type: EffectType.rgbTimeWarp);
  for (final (k, v) in [
    ('red_frames', r),
    ('green_frames', g),
    ('blue_frames', b),
    ('mix', mix),
  ]) {
    e = e.withParamEdited(k, Duration.zero, v);
  }
  return e.copyWith(enabled: ligado);
}

void main() {
  group('os deslocamentos', () {
    test('tres canais independentes, com sinal', () {
      final d = deslocamentosDoTimeWarp([
        _warp(r: -3, g: 0, b: 3),
      ], Duration.zero);
      expect(d.r, -3);
      expect(d.g, 0);
      expect(d.b, 3);
    });

    test('efeito desligado, ou sem efeito nenhum: zero nos tres', () {
      expect(
        deslocamentosDoTimeWarp([_warp(r: 5, ligado: false)], Duration.zero),
        (r: 0, g: 0, b: 0),
      );
      expect(deslocamentosDoTimeWarp([], Duration.zero), (r: 0, g: 0, b: 0));
    });

    test('zero em tudo NAO e efeito ativo', () {
      // E o que deixa o palco pular o trabalho inteiro sem perguntar
      // mais nada.
      expect(timeWarpAtivo([_warp()], Duration.zero), isFalse);
      expect(timeWarpAtivo([_warp(r: .5)], Duration.zero), isTrue);
      expect(timeWarpAtivo([], Duration.zero), isFalse);
    });

    test('o mesmo instante devolve o mesmo, em qualquer ordem', () {
      final e = [_warp(r: -4, b: 4)];
      final a = deslocamentosDoTimeWarp(e, const Duration(milliseconds: 500));
      deslocamentosDoTimeWarp(e, Duration.zero);
      deslocamentosDoTimeWarp(e, const Duration(seconds: 3));
      expect(deslocamentosDoTimeWarp(e, const Duration(milliseconds: 500)), a);
    });

    test('fora da faixa nao estoura', () {
      for (final v in [-5000.0, 5000.0, double.nan, double.infinity]) {
        final d = deslocamentosDoTimeWarp([_warp(r: v)], Duration.zero);
        expect(d.r.isFinite, isTrue, reason: 'r = $v');
        expect(d.r.abs(), lessThanOrEqualTo(120));
      }
    });
  });

  group('a exportacao sabe dos instantes de fora', () {
    Layer camada(EffectInstance e) => ShapeLayer(
      name: 'v',
      startTime: const Duration(seconds: 1),
      duration: const Duration(seconds: 4),
      effects: [e],
    );

    test('os tres deslocamentos entram na lista de instantes', () {
      // SEM ISTO os tres canais saem do MESMO quadro: o `quadroEm` da
      // exportacao so decodifica o que esta nesta lista.
      final t = const Duration(seconds: 2);
      final instantes = instantesDeOutroTempo(
        [camada(_warp(r: -3, g: 0, b: 3))],
        t,
        30,
      );
      // -3 quadros a 30 fps = -100 ms; +3 = +100 ms.
      expect(instantes.contains(t - const Duration(milliseconds: 100)), isTrue);
      expect(instantes.contains(t + const Duration(milliseconds: 100)), isTrue);
    });

    test('a taxa muda o TAMANHO do passo, e nao o numero de quadros', () {
      final t = const Duration(seconds: 2);
      final a30 = instantesDeOutroTempo([camada(_warp(r: 6))], t, 30);
      final a60 = instantesDeOutroTempo([camada(_warp(r: 6))], t, 60);
      expect(
        a30.any((d) => d == t + const Duration(milliseconds: 200)),
        isTrue,
        reason: '6 quadros a 30 fps = 200 ms',
      );
      expect(
        a60.any((d) => d == t + const Duration(milliseconds: 100)),
        isTrue,
        reason: '6 quadros a 60 fps = 100 ms',
      );
    });

    test('sem deslocamento, nenhum instante extra', () {
      final t = const Duration(seconds: 2);
      expect(instantesDeOutroTempo([camada(_warp())], t, 30), isEmpty);
    });

    test('offset fora do clipe segura o primeiro ou o ultimo quadro', () {
      final l = camada(_warp(r: -120, b: 120));
      final noInicio = instantesDeOutroTempo(
        [l],
        l.startTime + const Duration(milliseconds: 10),
        30,
      );
      expect(noInicio, contains(l.startTime));

      final fim = l.startTime + l.duration - const Duration(milliseconds: 10);
      final noFim = instantesDeOutroTempo([l], fim, 30);
      expect(
        noFim,
        contains(l.startTime + l.duration - const Duration(microseconds: 1)),
      );
    });
  });

  group('a ficha', () {
    final spec = effectSpecs[EffectType.rgbTimeWarp]!;

    test('e o RGB Time Warp, com os tres canais em quadros', () {
      expect(spec.name, 'RGB Time Warp');
      expect(spec.id, 'rgb_time_warp');
      for (final k in [
        'red_frames',
        'green_frames',
        'blue_frames',
        'clamp_chroma',
        'mix',
      ]) {
        expect(spec.params.containsKey(k), isTrue, reason: 'falta $k');
      }
      // NEGATIVO E POSITIVO: o rastro pode vir de tras ou de frente.
      for (final k in ['red_frames', 'green_frames', 'blue_frames']) {
        final p = spec.params[k]!;
        expect(p.min, lessThan(0));
        expect(p.max, greaterThan(0));
        expect(p.initial, 0);
      }
    });

    test('os quatro presets apontam para chave que existe', () {
      expect(spec.presets.length, 4);
      for (final p in spec.presets) {
        expect(p.valores, isNotEmpty);
        for (final k in p.valores.keys) {
          expect(
            spec.params.containsKey(k),
            isTrue,
            reason: '${p.nome} pede $k, que nao existe',
          );
        }
      }
      expect(spec.montar.length, lessThanOrEqualTo(3));
    });

    test('os deslocamentos aceitam keyframe', () {
      var e = EffectInstance(type: EffectType.rgbTimeWarp);
      e = e.withParamEdited('red_frames', Duration.zero, 0);
      e = e.withKeyframeToggled(Duration.zero);
      e = e.withParamEdited(
        'red_frames',
        const Duration(seconds: 1),
        10,
        forcar: true,
      );
      expect(deslocamentosDoTimeWarp([e], Duration.zero).r, 0);
      expect(
        deslocamentosDoTimeWarp([e], const Duration(seconds: 1)).r,
        closeTo(10, 1e-9),
      );
      expect(
        deslocamentosDoTimeWarp([e], const Duration(milliseconds: 500)).r,
        closeTo(5, 1e-9),
      );
    });
  });
}
