// O DESFOQUE DE MOVIMENTO (CC Force Motion Blur) — a janela de exposicao.
//
// O MOTOR JA ESTAVA PRONTO E NAO TINHA FICHA: o efeito nao aparecia na
// galeria nem tinha como ser ajustado, o mesmo defeito que o Time Slice
// tinha. O que se cobra aqui e a conta da janela, que e o que decide PARA
// ONDE o arrasto cai.
//
// OS PARAMETROS SAO DO PLUGIN, medidos: Motion Blur Samples 8, Shutter
// Angle 180, Shutter Phase 0, Native Motion Blur 2.
//
// O RSMB (RE:Vision) NAO ESTA INSTALADO e nao ha o que medir dele. A
// diferenca entre ele e este esta escrita na ficha: o RSMB estima o fluxo
// entre quadros e por isso borra a partir de um quadro so; aqui cada
// amostra e um quadro de verdade.
import 'package:aurea/src/features/editor/domain/desfoque_forcado.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:flutter_test/flutter_test.dart';

const _quadro = Duration(microseconds: 41667); // 24 fps

EffectInstance _efeito({
  double amostras = 8,
  double angulo = 180,
  double fase = 0,
  double nativo = 0,
}) => EffectInstance(
  type: EffectType.forceMotionBlur,
  params: {
    'samples': AnimatedDouble(amostras),
    'shutter_angle': AnimatedDouble(angulo),
    'shutter_phase': AnimatedDouble(fase),
    'native_motion_blur': AnimatedDouble(nativo),
  },
);

void main() {
  group('a janela de exposicao', () {
    test('180 graus sao meia exposicao, e a fase 0 arrasta para frente', () {
      final j = janelaDaExposicao(
        t: const Duration(seconds: 1),
        angulo: 180,
        fase: 0,
        fps: 24,
      );
      expect(j.inicio, const Duration(seconds: 1));
      expect(
        (j.fim - j.inicio).inMicroseconds,
        closeTo(41667 / 2, 2),
        reason: 'meia exposicao e meio quadro de 24 fps',
      );
    });

    test('fase -90 centra a janela no quadro', () {
      final j = janelaDaExposicao(
        t: const Duration(seconds: 1),
        angulo: 180,
        fase: -90,
        fps: 24,
      );
      final meio = (j.fim - j.inicio).inMicroseconds / 2;
      expect((j.inicio - const Duration(seconds: 1)).inMicroseconds,
          closeTo(-meio, 2));
      expect((j.fim - const Duration(seconds: 1)).inMicroseconds,
          closeTo(meio, 2));
    });

    test('fase -180 pega o que ja passou', () {
      final j = janelaDaExposicao(
        t: const Duration(seconds: 1),
        angulo: 180,
        fase: -180,
        fps: 24,
      );
      expect(j.inicio, lessThan(const Duration(seconds: 1)));
      expect(j.fim, const Duration(seconds: 1));
    });

    test('angulo zero nao abre janela nenhuma', () {
      final j = janelaDaExposicao(
        t: const Duration(seconds: 2),
        angulo: 0,
        fase: 0,
        fps: 30,
      );
      expect(j.inicio, j.fim);
    });

    test('angulo e fase fora da faixa sao presos, e nao viram NaN', () {
      final j = janelaDaExposicao(
        t: Duration.zero,
        angulo: double.nan,
        fase: double.infinity,
        fps: 30,
      );
      expect(j.inicio.inMicroseconds.isFinite, isTrue);
      expect(j.fim.inMicroseconds.isFinite, isTrue);
      // 720 graus e o teto do obturador: dois quadros inteiros.
      final teto = janelaDaExposicao(
        t: Duration.zero,
        angulo: 5000,
        fase: 0,
        fps: 24,
      );
      expect((teto.fim - teto.inicio).inMicroseconds, closeTo(83333, 2));
    });
  });

  group('as amostras dentro da janela', () {
    test('a primeira cai no comeco e a ultima no fim', () {
      const janela = (
        inicio: Duration(milliseconds: 100),
        fim: Duration(milliseconds: 200),
      );
      expect(instanteDaAmostra(janela, 0, 8), janela.inicio);
      expect(instanteDaAmostra(janela, 7, 8), janela.fim);
    });

    test('e sao espacadas por igual', () {
      const janela = (
        inicio: Duration.zero,
        fim: Duration(milliseconds: 70),
      );
      final passos = [
        for (var i = 0; i < 8; i++) instanteDaAmostra(janela, i, 8),
      ];
      for (var i = 1; i < 8; i++) {
        expect(
          (passos[i] - passos[i - 1]).inMicroseconds,
          closeTo(10000, 1),
          reason: 'amostra $i',
        );
      }
    });

    test('uma amostra so nao divide nada', () {
      const janela = (
        inicio: Duration(milliseconds: 5),
        fim: Duration(milliseconds: 9),
      );
      expect(instanteDaAmostra(janela, 0, 1), janela.inicio);
    });

    test('a janela do efeito le angulo e fase da ficha', () {
      final j = janelaDoEfeito(_efeito(fase: -180), Duration.zero,
          const Duration(seconds: 1), 24);
      expect(j.inicio, lessThan(const Duration(seconds: 1)));
      expect(j.fim, const Duration(seconds: 1));
    });
  });

  group('a ficha', () {
    test('esta registrada com o id do plugin e os valores de fabrica', () {
      final spec = effectSpecs[EffectType.forceMotionBlur];
      expect(spec, isNotNull);
      expect(spec!.id, 'cc_force_motion_blur');
      expect(spec.params['samples']!.initial, 8);
      expect(spec.params['shutter_angle']!.initial, 180);
      expect(spec.params['shutter_phase']!.initial, 0);
    });

    test('TODAS as chaves que o motor le existem na ficha', () {
      const lidas = [
        'samples',
        'shutter_angle',
        'shutter_phase',
        'native_motion_blur',
      ];
      final spec = effectSpecs[EffectType.forceMotionBlur]!;
      for (final chave in lidas) {
        expect(spec.params.containsKey(chave), isTrue,
            reason: 'o motor le "$chave" e a ficha nao tem essa chave');
      }
    });

    test('os presets usam as chaves que existem na ficha', () {
      final spec = effectSpecs[EffectType.forceMotionBlur]!;
      for (final pronto in spec.presets) {
        for (final chave in pronto.valores.keys) {
          expect(spec.params.containsKey(chave), isTrue,
              reason: 'preset "${pronto.nome}" mexe em "$chave"');
        }
      }
    });
  });
}
