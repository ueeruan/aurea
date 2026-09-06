import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/effect.dart';

void main() {
  group('Identificador estavel', () {
    test('todo efeito tem id em snake_case', () {
      for (final e in effectSpecs.entries) {
        expect(e.value.id, matches(RegExp(r'^[a-z][a-z0-9_]*$')),
            reason: '${e.key}');
      }
    });

    test('nenhum id se repete', () {
      final ids = effectSpecs.values.map((s) => s.id).toList();
      expect(ids.toSet().length, ids.length);
    });

    // O id e o que vai no arquivo: se a ida e volta falhar, projeto
    // salvo abre com o efeito errado.
    test('id vai e volta para o mesmo tipo', () {
      for (final t in EffectType.values) {
        expect(effectTypeFromId(effectIdOf(t)), t);
      }
    });

    test('id desconhecido devolve null em vez de estourar', () {
      expect(effectTypeFromId('nao_existe'), isNull);
    });

    // Renomear nao pode quebrar o que ja existe.
    test('os nomes antigos ainda abrem', () {
      expect(effectTypeFromId('cc_split'), EffectType.ccSplit);
      expect(effectTypeFromId('cc_semear'), EffectType.ccScatterize);
      expect(effectTypeFromId('glow_vol'), EffectType.glowVol);
      expect(effectTypeFromId('pixel_sort'), EffectType.pixelSort);
    });
  });

  group('Nomes e categorias em ingles', () {
    const categorias = {
      'Color', 'Light', 'Lens', 'Blur', 'Distort',
      'Stylize', 'Glitch', 'Time', 'Generate', 'Utility',
    };

    test('toda categoria esta na lista', () {
      for (final e in effectSpecs.entries) {
        expect(categorias, contains(e.value.category),
            reason: '${e.key} tem "${e.value.category}"');
      }
    });

    test('o prefixo CC de plugin de terceiro saiu', () {
      for (final s in effectSpecs.values) {
        expect(s.name.startsWith('CC '), isFalse, reason: s.name);
      }
    });

    test('nenhum nome tem acento (sao em ingles)', () {
      for (final s in effectSpecs.values) {
        expect(s.name, matches(RegExp(r'^[\x20-\x7E]+$')), reason: s.name);
      }
    });

    // A busca tem de continuar achando em portugues: quem digita
    // "desfoque" espera achar Gaussian Blur.
    test('a busca aceita portugues como sinonimo', () {
      final gauss = effectSpecs[EffectType.gaussianBlur]!;
      expect(gauss.synonyms, contains('desfoque'));
      final shake = effectSpecs[EffectType.tremor]!;
      expect(shake.synonyms, contains('tremor'));
      final glow = effectSpecs[EffectType.lightGlow]!;
      expect(glow.synonyms.any((s) => s.contains('brilho')), isTrue);
    });

    test('todo efeito tem pelo menos um sinonimo', () {
      for (final e in effectSpecs.entries) {
        expect(e.value.synonyms, isNotEmpty, reason: '${e.key}');
      }
    });
  });
}
