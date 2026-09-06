import 'dart:ui';

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/effect_preset.dart';
import 'package:aurea/src/features/editor/domain/fx.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('inventario e catalogo (PR-C0/C4)', () {
    test('todo efeito tem nome, categoria e parametros', () {
      for (final e in effectSpecs.entries) {
        expect(e.value.name, isNotEmpty, reason: '${e.key}');
        expect(effectCategories, contains(e.value.category),
            reason: '${e.key} tem categoria fora da lista');
        expect(e.value.params, isNotEmpty, reason: '${e.key}');
      }
    });

    test('TODO parametro tem controle na UI — nenhum tipo orfao', () {
      // A regra permanente da spec: efeito com parametro sem controle
      // nao pode existir no catalogo.
      const suportados = {
        ParamKind.number,
        ParamKind.color,
        ParamKind.point,
        ParamKind.choice,
        ParamKind.seed,
        ParamKind.toggle,
      };
      for (final e in effectSpecs.entries) {
        for (final p in e.value.params.entries) {
          expect(suportados, contains(p.value.kind),
              reason: '${e.key}.${p.key} usa tipo sem controle');
          if (p.value.kind == ParamKind.choice) {
            expect(p.value.options, isNotEmpty,
                reason: '${e.key}.${p.key} e escolha sem opcoes');
          }
        }
      }
    });

    test('todo EffectType do enum tem spec (nada visivel e inerte)', () {
      for (final t in EffectType.values) {
        expect(effectSpecs.containsKey(t), isTrue, reason: '$t');
      }
    });

    test('busca acha por nome, categoria e SINONIMO', () {
      expect(searchEffects('bloom'), contains(EffectType.lightGlow));
      expect(searchEffects('rgb split'), contains(EffectType.rgbSplit));
      expect(searchEffects('pixelate'), contains(EffectType.mosaic));
      expect(searchEffects('shake'), contains(EffectType.tremor));
      expect(searchEffects('grao'), contains(EffectType.filmGrain));
      expect(searchEffects('cor'), contains(EffectType.levels));
      expect(searchEffects('zzzz'), isEmpty);
    });

    test('categorias somam o catalogo inteiro', () {
      var total = 0;
      for (final c in effectCategories) {
        total += effectsInCategory(c).length;
      }
      expect(total, effectSpecs.length);
    });
  });

  group('neutralidade (I2)', () {
    test('todo efeito tem um valor neutro declarado ou zeravel', () {
      // Nenhum parametro pode nascer fora da propria faixa.
      for (final e in effectSpecs.entries) {
        for (final p in e.value.params.entries) {
          expect(p.value.initial, greaterThanOrEqualTo(p.value.min),
              reason: '${e.key}.${p.key}');
          expect(p.value.initial, lessThanOrEqualTo(p.value.max),
              reason: '${e.key}.${p.key}');
        }
      }
    });

    test('instancia nova usa exatamente os valores iniciais da spec', () {
      for (final t in EffectType.values) {
        final inst = EffectInstance(type: t);
        for (final p in effectSpecs[t]!.params.entries) {
          expect(inst.paramAt(p.key, Duration.zero), p.value.initial,
              reason: '$t.${p.key}');
        }
      }
    });
  });

  group('determinismo (semente)', () {
    test('o mesmo (semente, tempo) da o mesmo tremor, sempre', () {
      TremorSample at(Duration t) => tremorSample(
            amplitudePx: 40,
            phase: t.inMicroseconds / 1e6 * 8,
            style: 0,
            seed: 7,
            zoom: 0.2,
            tiltDeg: 3,
          );
      // Frame 200 a 30 fps.
      const f200 = Duration(milliseconds: 6666);
      final direto = at(f200);
      // "Reproduzir do zero" e so amostrar antes; nada acumula.
      for (var i = 0; i < 200; i++) {
        at(Duration(milliseconds: i * 33));
      }
      final depois = at(f200);
      expect(depois.dx, direto.dx);
      expect(depois.dy, direto.dy);
      expect(depois.rotationDeg, direto.rotationDeg);
    });

    test('sementes diferentes dao resultados diferentes', () {
      TremorSample s(int seed) => tremorSample(
          amplitudePx: 40, phase: 3.2, style: 0, seed: seed);
      expect(s(1).dx, isNot(s(2).dx));
    });
  });

  group('presets (PR-C2)', () {
    EffectInstance glitchComKeyframes() =>
        EffectInstance(type: EffectType.glitch, params: {
          'quantidade': AnimatedDouble(0)
              .withKeyframe(const Duration(milliseconds: 200), 1.5)
              .withKeyframe(const Duration(milliseconds: 700), 0),
        });

    test('IDA E VOLTA: salvar e aplicar devolve o mesmo estado', () {
      final original = [glitchComKeyframes()];
      final preset = saveEffectPreset(
        name: 'x',
        effects: original,
        layerStart: Duration.zero,
        layerDuration: const Duration(seconds: 1),
        layerSize: const Size(1080, 1080),
      );
      final aplicado = applyEffectPreset(preset,
          at: Duration.zero, targetSize: const Size(1080, 1080));

      final a = original.first.track('quantidade');
      final b = aplicado.first.track('quantidade');
      expect(b.keyframes.length, a.keyframes.length);
      for (var i = 0; i < a.keyframes.length; i++) {
        expect(b.keyframes[i].time, a.keyframes[i].time);
        expect(b.keyframes[i].value, closeTo(a.keyframes[i].value, 1e-9));
      }
    });

    test('keyframes sao RELATIVOS: aplicar aos 12 s desloca tudo', () {
      final preset = saveEffectPreset(
        name: 'x',
        effects: [glitchComKeyframes()],
        layerStart: Duration.zero,
        layerDuration: const Duration(seconds: 1),
        layerSize: const Size(1080, 1080),
      );
      final aplicado = applyEffectPreset(preset,
          at: const Duration(seconds: 12),
          targetSize: const Size(1080, 1080));
      final k = aplicado.first.track('quantidade').keyframes;
      expect(k.first.time, const Duration(milliseconds: 12200));
      expect(k.last.time, const Duration(milliseconds: 12700));
    });

    test('ENTRE FORMATOS: distancia normaliza pelo tamanho da camada',
        () {
      // rgbSplit.deslocamento e relativo; angulo NAO e.
      final fx = EffectInstance(type: EffectType.rgbSplit, params: {
        'deslocamento': AnimatedDouble(54), // 5% do menor lado (1080)
        'angulo': AnimatedDouble(45),
      });
      final preset = saveEffectPreset(
        name: 'x',
        effects: [fx],
        layerStart: Duration.zero,
        layerDuration: const Duration(seconds: 1),
        layerSize: const Size(1080, 1920),
      );
      // Aplicado numa camada de menor lado 540: metade do valor.
      final aplicado = applyEffectPreset(preset,
          at: Duration.zero, targetSize: const Size(960, 540));
      expect(aplicado.first.paramAt('deslocamento', Duration.zero),
          closeTo(27, 1e-9));
      // Angulo nao normaliza.
      expect(aplicado.first.paramAt('angulo', Duration.zero), 45);
    });

    test('ESTICAR dobra os intervalos SEM alterar os valores', () {
      final preset = saveEffectPreset(
        name: 'x',
        effects: [glitchComKeyframes()],
        layerStart: Duration.zero,
        layerDuration: const Duration(seconds: 1),
        layerSize: const Size(1080, 1080),
      );
      final esticado = applyEffectPreset(
        preset,
        at: Duration.zero,
        targetSize: const Size(1080, 1080),
        stretchTo: const Duration(seconds: 2),
      );
      final k = esticado.first.track('quantidade').keyframes;
      expect(k.first.time, const Duration(milliseconds: 400));
      expect(k.last.time, const Duration(milliseconds: 1400));
      // Valores intactos.
      expect(k.first.value, 1.5);
      expect(k.last.value, 0);
    });

    test('TOLERANCIA DE VERSAO: avisa, nunca falha em silencio', () {
      final antigo = EffectPreset(
        name: 'velho',
        effects: [
          EffectInstance(type: EffectType.vignette, params: {
            'quantidade': AnimatedDouble(0.8),
            'parametroQueSumiu': AnimatedDouble(1),
          }),
        ],
      );
      final r = reconcilePreset(antigo);
      expect(r.warnings.any((w) => w.contains('parametroQueSumiu')),
          isTrue);
      // O que ainda existe permanece.
      expect(r.effects.single.paramAt('quantidade', Duration.zero), 0.8);
      // O que e novo entra no padrao, com aviso.
      expect(r.effects.single.params.containsKey('raio'), isTrue);
    });

    test('biblioteca de fabrica e valida e somente leitura', () {
      final presets = factoryPresets();
      expect(presets, isNotEmpty);
      for (final p in presets) {
        expect(p.builtIn, isTrue, reason: p.name);
        expect(p.effects, isNotEmpty, reason: p.name);
        // Reconciliacao nao pode acusar problema em preset de fabrica.
        final r = reconcilePreset(p);
        expect(r.warnings.where((w) => w.contains('nao existe mais')),
            isEmpty,
            reason: '${p.name}: ${r.warnings}');
      }
    });
  });

  group('assar em keyframes (PR-C3)', () {
    test('o assado bate com o procedural, frame a frame', () {
      final fx = EffectInstance(type: EffectType.tremor, params: {
        'amplitude': AnimatedDouble(50),
        'frequencia': AnimatedDouble(6),
        'estilo': AnimatedDouble(0),
        'semente': AnimatedDouble(9),
      });
      Offset sample(Duration t) {
        final s = tremorSample(
          amplitudePx: fx.paramAt('amplitude', t),
          phase: integratedPhase(fx.track('frequencia'), t),
          style: 0,
          seed: 9,
        );
        return Offset(s.dx, s.dy);
      }

      final baked = bakeProceduralMotion(
        effect: fx,
        duration: const Duration(seconds: 1),
        fps: 30,
        basePosition: const Offset(100, 100),
        baseRotation: 0,
        baseScale: 1,
        sampleOffset: sample,
      );

      // Em cada frame assado, o valor tem que ser o procedural + base.
      for (var i = 0; i <= 30; i++) {
        final t = Duration(microseconds: i * (1000000 ~/ 30));
        final esperado = const Offset(100, 100) + sample(t);
        final obtido = baked.position.valueAt(t);
        expect((obtido - esperado).distance, lessThan(0.001),
            reason: 'frame $i');
      }
      // E virou keyframe DE VERDADE, editavel.
      expect(baked.position.keyframes.length, 31);
    });
  });
}
