import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';

void main() {
  group('TextUnits — segmentacao por grapheme cluster', () {
    test('emoji ZWJ e uma unidade so', () {
      final u = TextUnits.of('👨‍👩‍👧');
      expect(u.length, 1);
      expect(u.charCount, 1);
    });

    test('acento combinante nao separa', () {
      // 'e' + U+0301 (combinante)
      final u = TextUnits.of('café');
      expect(u.length, 4);
      expect(u.clusters.last, 'é');
    });

    test('contagens por base em frase com emoji e espacos', () {
      final u = TextUnits.of('👨‍👩‍👧 cafe fim');
      // clusters: emoji, ' ', c,a,f,e, ' ', f,i,m  -> 10 unidades
      expect(u.length, 10);
      expect(u.charCount, 10);
      expect(u.charNoSpaceCount, 8);
      expect(u.wordCount, 3);
      expect(u.lineCount, 1);
    });

    test('espacos nao contam em charactersNoSpaces (indice -1)', () {
      final u = TextUnits.of('ab cd');
      final (spaceIdx, _) =
          u.indexFor(2, SelectorBasedOn.charactersNoSpaces);
      expect(spaceIdx, -1);
      final (dIdx, count) =
          u.indexFor(4, SelectorBasedOn.charactersNoSpaces);
      expect(dIdx, 3);
      expect(count, 4);
    });

    test('quebra de linha separa linhas', () {
      final u = TextUnits.of('ab\ncd');
      expect(u.lineCount, 2);
      expect(u.lineIndex[0], 0);
      expect(u.lineIndex[4], 1);
      expect(u.wordCount, 2);
    });

    test('cobertura respeita a base de CADA seletor', () {
      final u = TextUnits.of('ab cd');
      // Seletor por PALAVRA cobrindo so a primeira palavra.
      final byWord = RangeSelector(
        basedOn: SelectorBasedOn.words,
        start: AnimatedDoubleFake.zero,
        end: AnimatedDoubleFake.half,
        smoothness: AnimatedDouble(0),
      );
      // 'a' (palavra 0) coberto; 'd' (palavra 1) nao; espaco -> 0.
      expect(u.coverageFor([byWord], 0, Duration.zero), 1);
      expect(u.coverageFor([byWord], 4, Duration.zero), 0);
      expect(u.coverageFor([byWord], 2, Duration.zero), 0);
    });
  });
}

/// Helpers para valores fixos legiveis nos testes.
abstract final class AnimatedDoubleFake {
  static final zero = AnimatedDouble(0);
  static final half = AnimatedDouble(0.5);
}
