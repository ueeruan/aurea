import 'package:aurea/src/features/editor/domain/caption.dart';
import 'package:flutter_test/flutter_test.dart';

Cue _w(String text, int startMs, int endMs) => Cue(
      start: Duration(milliseconds: startMs),
      end: Duration(milliseconds: endMs),
      text: text,
    );

void main() {
  group('modo Curtas (groupWordCues)', () {
    test('agrupa ate 3 palavras respeitando o limite de caracteres', () {
      final words = [
        _w('todos', 0, 300),
        _w('os', 320, 450),
        _w('dias', 470, 800),
        _w('empresarios', 820, 1400),
        _w('tomam', 1420, 1700),
        _w('decisoes', 1720, 2200),
      ];
      final out = groupWordCues(words);
      // "todos os dias" (3 palavras) fecha o bloco; "empresarios" (11
      // chars) + "tomam" cabe; "decisoes" estoura os 20 chars e abre
      // outro bloco.
      expect(out[0].text, 'todos os dias');
      expect(out[0].start, Duration.zero);
      expect(out[0].end, const Duration(milliseconds: 800));
      expect(out[1].text, 'empresarios tomam');
      expect(out[2].text, 'decisoes');
      expect(out.length, 3);
    });

    test('pausa longa quebra o bloco (frase nova)', () {
      final words = [
        _w('ola', 0, 300),
        // gap de 1s ate a proxima palavra.
        _w('mundo', 1300, 1600),
      ];
      final out = groupWordCues(words);
      expect(out.length, 2);
      expect(out[0].text, 'ola');
      expect(out[1].text, 'mundo');
    });

    test('lista vazia e de 1 palavra passam intactas', () {
      expect(groupWordCues(const []), isEmpty);
      final one = groupWordCues([_w('oi', 0, 200)]);
      expect(one.single.text, 'oi');
    });
  });

  group('edicao de cue', () {
    test('copyWith locked preserva id e tempos', () {
      final c = _w('formaceuticos', 100, 900);
      final fixed = c.copyWith(text: 'farmaceuticos', locked: true);
      expect(fixed.id, c.id);
      expect(fixed.start, c.start);
      expect(fixed.end, c.end);
      expect(fixed.text, 'farmaceuticos');
      expect(fixed.locked, isTrue);
    });

    test('normalizeCues nao funde cue travado (correcao manual fica)', () {
      final cues = [
        Cue(
            start: Duration.zero,
            end: const Duration(milliseconds: 400),
            text: 'curto',
            locked: true),
        _w('seguinte', 500, 1500),
      ];
      final out = normalizeCues(cues);
      expect(out.length, 2);
      expect(out[0].text, 'curto');
    });
  });
}
