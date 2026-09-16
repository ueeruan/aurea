import 'package:aurea/src/features/editor/domain/amostra_dos_efeitos.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:flutter_test/flutter_test.dart';

const _novos = [
  EffectType.cortina,
  EffectType.cortinaRadial,
  EffectType.apertarRecorte,
  EffectType.meioTom,
  EffectType.contorno,
  EffectType.brilhoPorDentro,
  EffectType.bordasAsperas,
];

void main() {
  test('os sete entraram no catálogo com id e prontos', () {
    for (final t in _novos) {
      final ficha = effectSpecs[t]!;
      expect(effectTypeFromId(ficha.id), t, reason: t.name);
      expect(ficha.presets.length, 3, reason: t.name);
      expect(ficha.montar, isNotEmpty, reason: t.name);
      for (final k in ficha.montar) {
        expect(ficha.params.containsKey(k), isTrue, reason: '${t.name}.$k');
      }
    }
    expect(effectSpecs[EffectType.cortina]!.id, 'wipe');
    expect(effectSpecs[EffectType.contorno]!.id, 'outline');
    expect(effectSpecs[EffectType.apertarRecorte]!.id, 'matte_choker');
  });

  test('cada um tem o seu modo no shader e lê o que a ficha oferece', () {
    for (final t in _novos) {
      final kernel = pixelKernels[t]!;
      final ficha = effectSpecs[t]!;
      expect(kernel.mode, inInclusiveRange(56, 62), reason: t.name);
      expect(kernel.keys.length, ficha.params.length, reason: t.name);
      for (final k in kernel.keys) {
        expect(ficha.params.containsKey(k), isTrue, reason: '${t.name}.$k');
      }
    }
    final modos = pixelKernels.values.map((k) => k.mode).toList();
    expect(modos.toSet().length, modos.length);
  });

  test('o aperto do recorte vai para os dois lados', () {
    final p = effectSpecs[EffectType.apertarRecorte]!.params['aperto']!;
    expect(p.min, lessThan(0), reason: 'negativo devolve a borda');
    expect(p.max, greaterThan(0), reason: 'positivo come a franja');
    expect(p.relative, isTrue, reason: 'o mesmo numero em qualquer resolucao');
  });

  test('a busca acha pelos nomes de casa', () {
    expect(searchEffects('cortina'), contains(EffectType.cortina));
    expect(searchEffects('franja'), contains(EffectType.apertarRecorte));
    expect(searchEffects('quadrinho'), contains(EffectType.meioTom));
    expect(searchEffects('adesivo'), contains(EffectType.contorno));
    expect(searchEffects('rasgado'), contains(EffectType.bordasAsperas));
  });

  group('a amostra das prévias', () {
    test('efeito de borda ganha silhueta; os outros continuam na foto', () {
      for (final t in _novos) {
        final id = effectSpecs[t]!.id;
        final precisa = const {
          'matte_choker',
          'outline',
          'inner_glow',
          'roughen_edges',
        }.contains(id);
        expect(efeitosComSilhueta.contains(id), precisa, reason: id);
      }
      // Quem ja aparecia na foto nao muda de amostra.
      expect(efeitosComSilhueta.contains('halftone'), isFalse);
      expect(efeitosComSilhueta.contains('wipe'), isFalse);
      // A repeticao precisa: as copias de uma camada de tela cheia caem
      // fora do quadro.
      expect(efeitosComSilhueta.contains('repeat_radial'), isTrue);
    });

    test('com silhueta, o efeito vai na forma e ela fica por cima', () {
      final com = amostraDoEfeito(EffectType.contorno, silhueta: true);
      expect(com.layers.length, 2);
      expect(com.layers.first, isA<ShapeLayer>());
      expect(com.layers.first.effects.single.type, EffectType.contorno);
      expect(com.layers.last, isA<ImageLayer>());
      expect(
        com.layers.last.effects,
        isEmpty,
        reason: 'a foto e so o fundo: o efeito e da forma',
      );

      final sem = amostraDoEfeito(EffectType.contorno);
      expect(sem.layers.single, isA<ImageLayer>());
      expect(sem.layers.single.effects.single.type, EffectType.contorno);
    });
  });
}
