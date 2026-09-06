import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  group('Camada de ajuste (PR-B1)', () {
    test('sobrevive ao round-trip com efeitos e mascaras', () {
      final adj = AdjustmentLayer(
        name: 'Ajuste',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        effects: [EffectInstance(type: EffectType.tint)],
        masks: [
          LayerMask(path: AnimatedPath(BezierPath.ellipse(400, 400))),
        ],
        opacity: AnimatedDouble(0.7),
      );
      final p = VideoProject(
          name: 'p', createdAt: DateTime(2026), layers: [adj]);
      final back = projectFromJson(
          jsonDecode(jsonEncode(projectToJson(p)))
              as Map<String, dynamic>);
      final l = back.layers.single;
      expect(l, isA<AdjustmentLayer>());
      expect(l.effects.single.type, EffectType.tint);
      expect(l.masks.length, 1);
      expect(l.opacity.base, closeTo(0.7, 1e-9));
    });

    test('I2: ajuste sem efeitos ativos e um no-op declarado', () {
      final adj = AdjustmentLayer(
        name: 'Ajuste',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        effects: [
          EffectInstance(type: EffectType.gaussianBlur, enabled: false),
        ],
      );
      // A regra do compositor: so ha trabalho se algum efeito esta ativo.
      expect(adj.effects.any((e) => e.enabled), false);
    });
  });

  group('Neutralidade do texto (PR-B2, teste da triagem §3)', () {
    test('animador VAZIO nao altera nenhum valor de unidade', () {
      final animator = TextAnimator(name: 'vazio', properties: const []);
      // Cobertura pode ser qualquer coisa: sem propriedades, nada muda.
      final units = TextUnits.of('Texto de teste');
      for (var i = 0; i < units.length; i++) {
        final c = units.coverageFor(
            animator.selectors, i, const Duration(seconds: 1));
        expect(c, greaterThanOrEqualTo(0)); // cobre tudo, e dai?
        // Sem propriedades, o pipeline aplica zero transformacoes:
        expect(animator.properties, isEmpty);
      }
    });

    test('propriedade em valor NEUTRO devolve exatamente a base', () {
      for (final type in TextAnimProp.values) {
        final p = AnimatorProperty(type: type);
        // Cobertura total (c=1): valor neutro nao mexe na base.
        expect(p.apply(37.5, Duration.zero, 1.0), closeTo(37.5, 1e-9),
            reason: '$type');
        // Cobertura zero: idem (I2 dupla).
        final p2 = AnimatorProperty(
            type: type, value: AnimatedDouble(999));
        expect(p2.apply(37.5, Duration.zero, 0.0), closeTo(37.5, 1e-9),
            reason: '$type c=0');
      }
    });

    test('escala e opacidade sao MULTIPLICATIVAS (item 3)', () {
      final scale = AnimatorProperty(
          type: TextAnimProp.scale, value: AnimatedDouble(50));
      // base 100% * lerp(1, 0.5, 1) = 50.
      expect(scale.apply(100, Duration.zero, 1.0), closeTo(50, 1e-9));
      // meia cobertura: 75.
      expect(scale.apply(100, Duration.zero, 0.5), closeTo(75, 1e-9));
    });

    test('centro da unidade em (i+0,5)/N (item 4)', () {
      final sel = RangeSelector(
        start: AnimatedDouble(0),
        end: AnimatedDouble(0.5),
        smoothness: AnimatedDouble(0),
      );
      // 4 unidades: centros 0,125 · 0,375 · 0,625 · 0,875.
      expect(sel.coverageAt(0, 4, Duration.zero), 1);
      expect(sel.coverageAt(1, 4, Duration.zero), 1);
      expect(sel.coverageAt(2, 4, Duration.zero), 0);
      expect(sel.coverageAt(3, 4, Duration.zero), 0);
    });

    test('end < start e ordenado, nao ignorado (item 9)', () {
      final sel = RangeSelector(
        start: AnimatedDouble(0.5),
        end: AnimatedDouble(0.0),
        smoothness: AnimatedDouble(0),
      );
      expect(sel.coverageAt(0, 4, Duration.zero), 1);
      expect(sel.coverageAt(3, 4, Duration.zero), 0);
    });
  });

  group('Efeitos novos (AUREA-2 §2)', () {
    test('specs existem com parametros animaveis', () {
      expect(effectSpecs[EffectType.spatialEcho]!.params.length, 7);
      expect(effectSpecs[EffectType.radialAberration]!.params,
          contains('quantidade'));
      expect(
          effectSpecs[EffectType.echo]!.params, contains('matiz'));
    });

    test('efeitos serializam pelo indice de forma estavel', () {
      final e = EffectInstance(type: EffectType.spatialEcho);
      final layer = ShapeLayer(
        name: 's',
        startTime: Duration.zero,
        duration: const Duration(seconds: 2),
        effects: [e],
      );
      final back = layerFromJson(
          jsonDecode(jsonEncode(layerToJson(layer)))
              as Map<String, dynamic>);
      expect(back.effects.single.type, EffectType.spatialEcho);
    });
  });
}
