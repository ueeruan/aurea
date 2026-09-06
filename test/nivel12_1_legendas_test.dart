import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/caption.dart';
import 'package:aurea/src/features/editor/domain/caption_highlight.dart';

Cue _p(int deMs, int ateMs, String texto) => Cue(
      start: Duration(milliseconds: deMs),
      end: Duration(milliseconds: ateMs),
      text: texto,
    );

/// "nos nao buscamos Ele" — quatro palavras coladas.
List<Cue> _frase() => [
      _p(0, 300, 'nos'),
      _p(320, 600, 'nao'),
      _p(620, 1100, 'buscamos'),
      _p(1120, 1400, 'Ele'),
    ];

void main() {
  group('A frase e diagramada uma vez', () {
    test('palavras coladas ficam no mesmo grupo', () {
      final frases = agruparEmFrases(_frase());
      expect(frases, hasLength(1));
      expect(frases.single.palavras, hasLength(4));
    });

    test('o grupo NAO muda enquanto a frase avanca', () {
      // E a regra: se o grupo trocasse a cada palavra, as vizinhas
      // pulariam de lugar.
      final frases = agruparEmFrases(_frase());
      final grupos = <List<String>>[];
      for (var ms = 0; ms < 1400; ms += 100) {
        final f = fraseEm(frases, Duration(milliseconds: ms));
        if (f != null) grupos.add([for (final p in f.palavras) p.text]);
      }
      for (final g in grupos) {
        expect(g, grupos.first);
      }
    });

    test('pausa longa comeca outro grupo', () {
      final frases = agruparEmFrases([
        _p(0, 300, 'um'),
        _p(320, 600, 'dois'),
        _p(2000, 2300, 'tres'),
      ]);
      expect(frases, hasLength(2));
      expect(frases.last.palavras.single.text, 'tres');
    });

    test('pontuacao forte fecha o grupo mesmo sem pausa', () {
      final frases = agruparEmFrases([
        _p(0, 300, 'acabou.'),
        _p(320, 600, 'comeca'),
      ]);
      expect(frases, hasLength(2));
    });

    test('o grupo nao passa de cinco palavras', () {
      final frases = agruparEmFrases([
        for (var i = 0; i < 12; i++) _p(i * 300, i * 300 + 280, 'p$i'),
      ]);
      for (final f in frases) {
        expect(f.palavras.length, lessThanOrEqualTo(5));
      }
    });
  });

  group('A palavra ativa e a que esta sendo dita', () {
    final frase = agruparEmFrases(_frase()).single;

    test('conferido em tres pontos do audio', () {
      expect(frase.ativaEm(const Duration(milliseconds: 150)), 0);
      expect(frase.ativaEm(const Duration(milliseconds: 800)), 2);
      expect(frase.ativaEm(const Duration(milliseconds: 1300)), 3);
    });

    test('entre duas palavras o destaque segura na ultima dita', () {
      // 310 ms cai no vao entre 'nos' e 'nao'.
      expect(frase.ativaEm(const Duration(milliseconds: 310)), 0);
    });

    test('antes da primeira palavra nao ha destaque', () {
      final tardia = CaptionPhrase([_p(500, 800, 'ola')]);
      expect(tardia.ativaEm(const Duration(milliseconds: 100)), isNull);
    });
  });

  group('Escala e cor', () {
    final frase = agruparEmFrases(_frase()).single;

    test('so a palavra ativa cresce; as vizinhas ficam em 1,0', () {
      const t = Duration(milliseconds: 900);
      final ativa = frase.ativaEm(t);
      for (var i = 0; i < frase.palavras.length; i++) {
        final e = escalaDaPalavra(
          indice: i,
          ativa: ativa,
          frase: frase,
          t: t,
          destaque: 2.0,
        );
        if (i == ativa) {
          expect(e, greaterThan(1.5));
        } else {
          expect(e, 1.0, reason: 'a vizinha $i se mexeu');
        }
      }
    });

    test('a mola nao passa do alvo', () {
      final frase2 = CaptionPhrase([_p(0, 2000, 'x')]);
      for (var ms = 0; ms <= 2000; ms += 5) {
        final e = escalaDaPalavra(
          indice: 0,
          ativa: 0,
          frase: frase2,
          t: Duration(milliseconds: ms),
          destaque: 2.0,
        );
        expect(e, lessThanOrEqualTo(2.0000001), reason: 'overshoot em $ms ms');
        expect(e, greaterThanOrEqualTo(1.0));
      }
    });

    test('a curva do inflar sobe e encosta em 1', () {
      expect(inflar(0), 0);
      expect(inflar(1), 1);
      expect(inflar(0.5), greaterThan(0.7));
      var anterior = 0.0;
      for (var i = 0; i <= 20; i++) {
        final v = inflar(i / 20);
        expect(v, greaterThanOrEqualTo(anterior));
        anterior = v;
      }
    });

    test('a cor entra junto com a escala', () {
      const t = Duration(milliseconds: 900);
      final c = corDaPalavra(
        indice: 2,
        ativa: 2,
        frase: frase,
        t: t,
        contexto: const Color(0xFFFFFFFF),
        destaque: const Color(0xFFFF0000),
      );
      expect(c.r, greaterThan(c.g));
    });

    test('a vizinha fica na cor do contexto', () {
      final c = corDaPalavra(
        indice: 0,
        ativa: 2,
        frase: frase,
        t: const Duration(milliseconds: 900),
        contexto: const Color(0xFFFFFFFF),
        destaque: const Color(0xFFFF0000),
      );
      expect(c, const Color(0xFFFFFFFF));
    });
  });

  group('Neutralidade', () {
    test('destaque em 100% e legenda comum', () {
      final frase = agruparEmFrases(_frase()).single;
      for (var i = 0; i < frase.palavras.length; i++) {
        expect(
          escalaDaPalavra(
            indice: i,
            ativa: 2,
            frase: frase,
            t: const Duration(milliseconds: 900),
            destaque: 1.0,
          ),
          1.0,
        );
      }
      expect(
        const CaptionHighlightStyle(ativo: true, destaque: 1.0).isNeutro,
        isTrue,
      );
    });

    test('sem tempo por palavra nao ha frase para destacar', () {
      expect(agruparEmFrases(const []), isEmpty);
      expect(fraseEm(const [], Duration.zero), isNull);
    });

    test('estilo desligado e neutro', () {
      expect(const CaptionHighlightStyle().isNeutro, isTrue);
    });
  });

  group('Palavra que nao cabe', () {
    test('encolhe ate a margem', () {
      final f = fatorParaCaber(larguraDoTexto: 900, larguraDisponivel: 600);
      expect(f, closeTo(600 / 900, 1e-9));
      expect(900 * f, closeTo(600, 1e-6));
    });

    test('o que ja cabe nao encolhe', () {
      expect(fatorParaCaber(larguraDoTexto: 300, larguraDisponivel: 600), 1);
    });

    test('largura invalida nao quebra a conta', () {
      expect(fatorParaCaber(larguraDoTexto: 0, larguraDisponivel: 600), 1);
      expect(fatorParaCaber(larguraDoTexto: 300, larguraDisponivel: 0), 1);
    });
  });

  group('Os cinco layouts e os cinco presets', () {
    test('sao cinco arranjos', () {
      expect(HighlightLayout.values, hasLength(5));
      for (final l in HighlightLayout.values) {
        expect(l.rotulo, isNotEmpty);
        expect(l.contextoPorLado, lessThanOrEqualTo(kMaxContextoPorLado));
      }
    });

    test('Sozinha nao mostra contexto', () {
      expect(HighlightLayout.sozinha.contextoPorLado, 0);
    });

    test('sao cinco presets, todos ativos', () {
      expect(HighlightPresets.todos, hasLength(5));
      for (final (nome, estilo) in HighlightPresets.todos) {
        expect(nome, isNotEmpty);
        expect(estilo.ativo, isTrue);
        expect(estilo.isNeutro, isFalse, reason: nome);
      }
    });

    test('Editorial e serifada maiuscula atravessada', () {
      const e = HighlightPresets.editorial;
      expect(e.layout, HighlightLayout.atravessada);
      expect(e.maiusculas, isTrue);
      expect(e.fonteDestaque, 'serif');
    });

    test('Manifesto e minuscula empilhada com entrelinha apertada', () {
      const m = HighlightPresets.manifesto;
      expect(m.layout, HighlightLayout.empilhada);
      expect(m.maiusculas, isFalse);
      expect(m.entrelinha, lessThan(1.0));
      expect(m.tracking, lessThan(0));
    });

    test('o estilo e da camada: trocar o preset troca tudo de uma vez', () {
      const antes = HighlightPresets.editorial;
      final depois = antes.copyWith(corDestaque: const Color(0xFF00FF00));
      expect(depois.corDestaque, const Color(0xFF00FF00));
      expect(depois.layout, antes.layout);
      expect(depois.maiusculas, antes.maiusculas);
    });
  });
}
