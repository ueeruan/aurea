import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/blend_extra.dart';

const _preto = (0.0, 0.0, 0.0);
const _branco = (1.0, 1.0, 1.0);
const _cinza = (0.5, 0.5, 0.5);

void main() {
  group('Catalogo', () {
    test('todo modo tem rotulo', () {
      for (final b in AureaBlend.values) {
        expect(aureaBlendLabel(b).trim(), isNotEmpty);
      }
    });

    test('os de pixel inteiro estao marcados', () {
      expect(aureaBlendIsPerPixel(AureaBlend.darkerColor), isTrue);
      expect(aureaBlendIsPerPixel(AureaBlend.lighterColor), isTrue);
      expect(aureaBlendIsPerPixel(AureaBlend.dissolve), isTrue);
      expect(aureaBlendIsPerPixel(AureaBlend.linearBurn), isFalse);
    });
  });

  group('Elemento neutro de cada modo', () {
    // A regra que separa "modo de mescla" de "efeito": existe um valor
    // do topo que devolve o fundo intacto. Errar isso e o que faz a
    // imagem clarear ou escurecer sozinha ao ligar o modo.
    test('Linear Burn: topo branco nao muda nada', () {
      for (final b in [0.0, 0.25, 0.5, 0.9, 1.0]) {
        expect(blendChannel(AureaBlend.linearBurn, b, 1), closeTo(b, 1e-9));
      }
    });

    test('Linear Light: topo 0,5 nao muda nada', () {
      for (final b in [0.0, 0.3, 0.75, 1.0]) {
        expect(
            blendChannel(AureaBlend.linearLight, b, 0.5), closeTo(b, 1e-9));
      }
    });

    test('Vivid Light: topo 0,5 nao muda nada', () {
      for (final b in [0.1, 0.4, 0.8]) {
        expect(blendChannel(AureaBlend.vividLight, b, 0.5), closeTo(b, 1e-9));
      }
    });

    test('Pin Light: topo 0,5 nao muda nada', () {
      for (final b in [0.0, 0.3, 1.0]) {
        expect(blendChannel(AureaBlend.pinLight, b, 0.5), closeTo(b, 1e-9));
      }
    });

    test('Subtrair: topo preto nao muda nada', () {
      for (final b in [0.0, 0.6, 1.0]) {
        expect(blendChannel(AureaBlend.subtract, b, 0), closeTo(b, 1e-9));
      }
    });

    test('Dividir: topo branco nao muda nada', () {
      for (final b in [0.0, 0.4, 1.0]) {
        expect(blendChannel(AureaBlend.divide, b, 1), closeTo(b, 1e-9));
      }
    });
  });

  group('Cada modo faz o que promete', () {
    test('Linear Burn escurece', () {
      expect(blendChannel(AureaBlend.linearBurn, 0.6, 0.6),
          lessThan(0.6));
      expect(blendChannel(AureaBlend.linearBurn, 0.2, 0.2), 0);
    });

    test('Subtrair nunca passa do preto', () {
      expect(blendChannel(AureaBlend.subtract, 0.3, 0.9), 0);
    });

    test('Dividir clareia e trava no branco', () {
      expect(blendChannel(AureaBlend.divide, 0.5, 0.25), 1);
      expect(blendChannel(AureaBlend.divide, 0.2, 0.8), closeTo(0.25, 1e-9));
    });

    // Hard Mix e binario por definicao: e o que faz o cartaz de duas
    // cores. Qualquer meio-tom no resultado seria bug.
    test('Hard Mix so devolve 0 ou 1', () {
      for (final b in [0.0, 0.2, 0.49, 0.5, 0.51, 0.8, 1.0]) {
        for (final s in [0.0, 0.3, 0.5, 0.7, 1.0]) {
          final v = blendChannel(AureaBlend.hardMix, b, s);
          expect(v == 0 || v == 1, isTrue,
              reason: 'fundo $b topo $s deu $v');
        }
      }
    });

    test('Pin Light escolhe entre fundo e o dobro do topo', () {
      // Topo escuro (0,2): so troca se for MAIS escuro que o fundo.
      expect(blendChannel(AureaBlend.pinLight, 0.9, 0.2), closeTo(0.4, 1e-9));
      expect(blendChannel(AureaBlend.pinLight, 0.1, 0.2), closeTo(0.1, 1e-9));
    });

    test('Vivid Light com topo branco estoura para o branco', () {
      expect(blendChannel(AureaBlend.vividLight, 0.4, 1), 1);
      expect(blendChannel(AureaBlend.vividLight, 0.4, 0), 0);
    });

    test('nenhum modo sai da faixa 0..1', () {
      for (final m in AureaBlend.values) {
        for (final b in [0.0, 0.33, 0.67, 1.0]) {
          for (final s in [0.0, 0.33, 0.67, 1.0]) {
            final v = blendChannel(m, b, s);
            expect(v, inInclusiveRange(0, 1),
                reason: '$m fundo $b topo $s');
          }
        }
      }
    });
  });

  group('Modos de pixel inteiro', () {
    test('Cor mais escura escolhe o pixel de menor luminancia', () {
      // Azul puro e mais escuro que amarelo puro em Rec. 709, mesmo os
      // dois estando "saturados" — e por isso que a escolha e por pixel.
      const azul = (0.0, 0.0, 1.0);
      const amarelo = (1.0, 1.0, 0.0);
      expect(blendPixel(AureaBlend.darkerColor, amarelo, azul), azul);
      expect(blendPixel(AureaBlend.darkerColor, azul, amarelo), azul);
    });

    test('Cor mais clara e o espelho', () {
      const azul = (0.0, 0.0, 1.0);
      const amarelo = (1.0, 1.0, 0.0);
      expect(blendPixel(AureaBlend.lighterColor, amarelo, azul), amarelo);
      expect(blendPixel(AureaBlend.lighterColor, azul, amarelo), amarelo);
    });

    // Escolher o PIXEL preserva a matiz; escolher canal a canal
    // inventaria uma cor que nao esta em nenhuma das duas.
    test('a cor escolhida e uma das duas, nunca uma terceira', () {
      const a = (0.9, 0.2, 0.1);
      const b = (0.1, 0.7, 0.3);
      final r = blendPixel(AureaBlend.darkerColor, a, b);
      expect(r == a || r == b, isTrue);
    });

    test('Dissolver e tudo ou nada', () {
      final passa = blendPixel(AureaBlend.dissolve, _preto, _branco,
          sourceAlpha: 0.5, dissolveDraw: 0.2);
      final naoPassa = blendPixel(AureaBlend.dissolve, _preto, _branco,
          sourceAlpha: 0.5, dissolveDraw: 0.8);
      expect(passa, _branco);
      expect(naoPassa, _preto);
    });

    test('Dissolver com topo opaco cobre sempre', () {
      for (final sorteio in [0.0, 0.5, 0.99]) {
        expect(
            blendPixel(AureaBlend.dissolve, _preto, _branco,
                sourceAlpha: 1, dissolveDraw: sorteio),
            _branco);
      }
    });
  });

  group('Composicao com alfa', () {
    // A garantia mais importante: camada invisivel nao muda um pixel.
    test('topo transparente devolve o fundo intacto', () {
      for (final m in AureaBlend.values) {
        final (cor, alfa) =
            composeBlend(m, _cinza, 1, _branco, 0, dissolveDraw: 0.99);
        expect(cor.$1, closeTo(0.5, 1e-9), reason: '$m');
        expect(cor.$2, closeTo(0.5, 1e-9), reason: '$m');
        expect(alfa, closeTo(1, 1e-9), reason: '$m');
      }
    });

    test('sobre fundo vazio, o topo aparece como e', () {
      for (final m in AureaBlend.values) {
        final (cor, alfa) = composeBlend(m, _preto, 0, _cinza, 1);
        expect(cor.$1, closeTo(0.5, 1e-9), reason: '$m');
        expect(alfa, closeTo(1, 1e-9), reason: '$m');
      }
    });

    test('o alfa do resultado e o de sempre', () {
      final (_, alfa) = composeBlend(
          AureaBlend.linearBurn, _cinza, 0.5, _branco, 0.5);
      expect(alfa, closeTo(0.75, 1e-9));
    });

    test('topo opaco manda no resultado', () {
      final (cor, _) =
          composeBlend(AureaBlend.subtract, _branco, 1, _cinza, 1);
      expect(cor.$1, closeTo(0.5, 1e-9));
    });
  });
}
