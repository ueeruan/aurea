import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/caption.dart';

void main() {
  group('SRT', () {
    const srt = '''
1
00:00:01,240 --> 00:00:03,020
isso muda tudo

2
00:00:03,500 --> 00:00:05,000
segunda fala
com duas linhas
''';

    test('parse le tempos e texto', () {
      final cues = parseSrt(srt);
      expect(cues.length, 2);
      expect(cues[0].start, const Duration(milliseconds: 1240));
      expect(cues[0].end, const Duration(milliseconds: 3020));
      expect(cues[0].text, 'isso muda tudo');
      expect(cues[1].text, 'segunda fala\ncom duas linhas');
    });

    test('round-trip: exportar e importar da cues identicos', () {
      final cues = parseSrt(srt);
      final roundTripped = parseSrt(serializeSrt(cues));
      expect(roundTripped.length, cues.length);
      for (var i = 0; i < cues.length; i++) {
        expect(roundTripped[i].start, cues[i].start);
        expect(roundTripped[i].end, cues[i].end);
        expect(roundTripped[i].text, cues[i].text);
      }
    });

    test('tolerante a \\r\\n e bloco sem indice', () {
      final cues = parseSrt(
          '00:00:00,000 --> 00:00:01,000\r\nfala\r\n\r\n00:00:02,000 --> 00:00:03,000\r\noutra\r\n');
      expect(cues.length, 2);
      expect(cues[0].text, 'fala');
    });
  });

  group('Cue ativo (busca binaria)', () {
    final cues = [
      Cue(
          start: const Duration(seconds: 1),
          end: const Duration(seconds: 2),
          text: 'a'),
      Cue(
          start: const Duration(seconds: 3),
          end: const Duration(seconds: 5),
          text: 'b'),
    ];

    test('dentro, fora e nas bordas', () {
      expect(activeCueAt(cues, const Duration(milliseconds: 1500))?.text, 'a');
      expect(activeCueAt(cues, const Duration(milliseconds: 2500)), null);
      expect(activeCueAt(cues, const Duration(seconds: 4))?.text, 'b');
      expect(activeCueAt(cues, const Duration(seconds: 1))?.text, 'a');
      expect(activeCueAt(cues, const Duration(seconds: 2)), null);
      expect(activeCueAt(cues, Duration.zero), null);
      expect(activeCueAt(cues, const Duration(seconds: 9)), null);
    });
  });

  group('Normalizacao', () {
    test('cue curto e unido ao vizinho (duracao minima 0,8s)', () {
      final cues = normalizeCues([
        Cue(
            start: Duration.zero,
            end: const Duration(milliseconds: 300),
            text: 'oi'),
        Cue(
            start: const Duration(milliseconds: 300),
            end: const Duration(milliseconds: 1500),
            text: 'tudo bem'),
      ]);
      expect(cues.length, 1);
      expect(cues[0].text, 'oi tudo bem');
      expect(cues[0].end, const Duration(milliseconds: 1500));
    });

    test('cue locked nao e unido', () {
      final cues = normalizeCues([
        Cue(
            start: Duration.zero,
            end: const Duration(milliseconds: 300),
            text: 'oi',
            locked: true),
        Cue(
            start: const Duration(milliseconds: 300),
            end: const Duration(milliseconds: 1500),
            text: 'tudo bem'),
      ]);
      expect(cues.length, 2);
    });

    test('quebra de linha nunca parte palavra', () {
      final wrapped = wrapCaptionText(
        'uma frase razoavelmente longa que precisa quebrar em duas linhas',
        maxCharsPerLine: 30,
      );
      final lines = wrapped.split('\n');
      expect(lines.length, 2);
      for (final line in lines.take(1)) {
        expect(line.length, lessThanOrEqualTo(30));
      }
      // Nenhuma palavra cortada: juntar de volta reproduz as palavras.
      expect(wrapped.replaceAll('\n', ' '),
          'uma frase razoavelmente longa que precisa quebrar em duas linhas');
    });

    test('maximo de linhas respeitado (excedente na ultima)', () {
      final wrapped = wrapCaptionText(
        'a b c d e f g h i j k l m n o p q r s t u v w x y z',
        maxCharsPerLine: 8,
        maxLines: 2,
      );
      expect(wrapped.split('\n').length, 2);
    });
  });
}
