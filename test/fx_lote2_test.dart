import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/widgets/fx_lote2.dart';

const _novos = <EffectType>[
  EffectType.timeRemap,
  EffectType.pixelSort,
  EffectType.blobTracker,
  EffectType.turbulentDisplace,
  EffectType.unsharpMask,
  EffectType.motionTile,
  EffectType.bend,
  EffectType.ccScatterize,
  EffectType.ccSplit,
  EffectType.vhs,
  EffectType.filmDamage,
  EffectType.glitchify,
];

void main() {
  group('Catalogo do lote 2', () {
    test('os 12 efeitos novos existem e tem spec', () {
      for (final t in _novos) {
        expect(effectSpecs[t], isNotNull, reason: '$t sem spec');
        expect(effectSpecs[t]!.name.trim(), isNotEmpty);
        expect(effectSpecs[t]!.params, isNotEmpty, reason: '$t sem params');
      }
    });

    test('todo efeito do catalogo tem spec — nenhum tipo orfao', () {
      for (final t in EffectType.values) {
        expect(effectSpecs[t], isNotNull, reason: '$t sem spec');
      }
    });

    test('categoria de todo efeito esta na lista de categorias', () {
      for (final e in effectSpecs.entries) {
        expect(effectCategories, contains(e.value.category),
            reason: '${e.key} usa categoria fora da lista');
      }
    });

    test('parametro tem faixa valida e inicial dentro dela', () {
      for (final e in effectSpecs.entries) {
        for (final p in e.value.params.entries) {
          expect(p.value.min, lessThan(p.value.max),
              reason: '${e.key}/${p.key}');
          expect(p.value.initial,
              inInclusiveRange(p.value.min, p.value.max),
              reason: '${e.key}/${p.key}');
        }
      }
    });

    test('escolha declara as opcoes; alternar e 0..1', () {
      for (final e in effectSpecs.entries) {
        for (final p in e.value.params.entries) {
          if (p.value.kind == ParamKind.choice) {
            expect(p.value.options.length, greaterThanOrEqualTo(2),
                reason: '${e.key}/${p.key} e escolha sem opcoes');
            expect(p.value.max,
                closeTo(p.value.options.length - 1, 0.001),
                reason: '${e.key}/${p.key}: max nao bate com as opcoes');
          }
          if (p.value.kind == ParamKind.toggle) {
            expect(p.value.min, 0);
            expect(p.value.max, 1);
          }
        }
      }
    });

    test('a busca acha os novos pelos nomes que a gente digita', () {
      void achou(String termo, EffectType alvo) {
        expect(searchEffects(termo), contains(alvo),
            reason: 'buscar "$termo" nao achou $alvo');
      }

      achou('pixel sort', EffectType.pixelSort);
      achou('time remap', EffectType.timeRemap);
      achou('turbulent', EffectType.turbulentDisplace);
      achou('unsharp', EffectType.unsharpMask);
      achou('motion tile', EffectType.motionTile);
      achou('bend', EffectType.bend);
      achou('semear', EffectType.ccScatterize);
      achou('cc split', EffectType.ccSplit);
      achou('vhs', EffectType.vhs);
      achou('film damage', EffectType.filmDamage);
      achou('glitchify', EffectType.glitchify);
      achou('blob', EffectType.blobTracker);
      // E tambem pelo que a pessoa QUER, nao pelo nome tecnico.
      achou('congelar', EffectType.timeRemap);
      achou('velho', EffectType.filmDamage);
      achou('fita', EffectType.vhs);
      achou('nitidez', EffectType.unsharpMask);
    });

    test('o efeito nasce com os valores iniciais da spec', () {
      for (final t in _novos) {
        final fx = EffectInstance(type: t);
        for (final p in effectSpecs[t]!.params.entries) {
          expect(fx.paramAt(p.key, Duration.zero),
              closeTo(p.value.initial, 1e-9),
              reason: '$t/${p.key}');
        }
      }
    });
  });

  group('Ruido dos efeitos', () {
    // Invariante I1: o mesmo instante da sempre o mesmo quadro. Sem
    // isso, exportar duas vezes daria videos diferentes.
    test('e funcao pura de (x, y, semente)', () {
      for (var i = 0; i < 50; i++) {
        final a = fxNoise(i.toDouble(), 3, 7);
        final b = fxNoise(i.toDouble(), 3, 7);
        expect(a, b);
        expect(a, inInclusiveRange(0, 1));
      }
    });

    test('semente diferente muda o resultado', () {
      var diferentes = 0;
      for (var i = 0; i < 50; i++) {
        if (fxNoise(i.toDouble(), 0, 1) != fxNoise(i.toDouble(), 0, 2)) {
          diferentes++;
        }
      }
      expect(diferentes, greaterThan(40));
    });

    test('ruido interpolado e continuo', () {
      var maxSalto = 0.0;
      for (var i = 0; i < 200; i++) {
        final a = fxValueNoise(i / 20, 1.5, 9);
        final b = fxValueNoise((i + 1) / 20, 1.5, 9);
        maxSalto = maxSalto > (a - b).abs() ? maxSalto : (a - b).abs();
        expect(a, inInclusiveRange(0, 1));
      }
      expect(maxSalto, lessThan(0.4));
    });

    test('fractal fica em [0,1] com qualquer numero de oitavas', () {
      for (var oct = 1; oct <= 5; oct++) {
        for (var i = 0; i < 30; i++) {
          final v = fxFractal(i * 0.7, i * 0.3, 4, oct);
          expect(v, inInclusiveRange(0, 1), reason: 'oitavas $oct');
        }
      }
    });
  });

  group('Pintores', () {
    // Repintar so quando algo muda: num editor que roda a 30 fps, um
    // shouldRepaint frouxo derruba a taxa sozinho.
    test('nao repintam quando nada mudou', () {
      final a = TurbulentDisplacePainter(
          amount: 10, scale: 40, complexity: 2, evolution: 0, seed: 1);
      final b = TurbulentDisplacePainter(
          amount: 10, scale: 40, complexity: 2, evolution: 0, seed: 1);
      expect(a.shouldRepaint(b), isFalse);

      final c = TurbulentDisplacePainter(
          amount: 22, scale: 40, complexity: 2, evolution: 0, seed: 1);
      expect(a.shouldRepaint(c), isTrue);

      const v1 = VhsPainter(
          intensity: 1, lines: 1, noise: 1, time: Duration.zero, seed: 1);
      const v2 = VhsPainter(
          intensity: 1, lines: 1, noise: 1, time: Duration.zero, seed: 1);
      expect(v1.shouldRepaint(v2), isFalse);
      const v3 = VhsPainter(
          intensity: 1,
          lines: 1,
          noise: 1,
          time: Duration(seconds: 1),
          seed: 1);
      expect(v1.shouldRepaint(v3), isTrue);
    });

    test('rastreador de blobs desenha sem estourar em tamanho zero', () {
      const p = BlobTrackerPainter(
        track: null,
        time: Duration.zero,
        color: Color(0xFFB8FF3D),
        style: 1,
        showCenter: true,
        showLines: true,
        lineType: 0,
        lineStyle: 1,
        palette: 1,
        thickness: 2,
        opacity: 100,
        fill: 0,
        cornerLength: 20,
        showCaption: true,
        captionContent: 1,
        captionPosition: 0,
        fontSize: 12,
        seed: 7,
      );
      final recorder = ui.PictureRecorder();
      final canvas = Canvas(recorder);
      p.paint(canvas, Size.zero);
      p.paint(canvas, const Size(320, 240));
      recorder.endRecording();
    });

    test('VHS e filme danificado desenham em tamanho zero sem quebrar',
        () {
      final recorder = ui.PictureRecorder();
      final canvas = Canvas(recorder);
      const VhsPainter(
              intensity: 0.6,
              lines: 0.5,
              noise: 0.3,
              time: Duration(milliseconds: 500),
              seed: 5)
          .paint(canvas, Size.zero);
      const FilmDamagePainter(
              dust: 0.5,
              scratches: 0.4,
              burn: 0.3,
              time: Duration(milliseconds: 500),
              seed: 11)
          .paint(canvas, const Size(200, 100));
      recorder.endRecording();
    });
  });
}
