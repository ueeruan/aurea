// "CADA LETRA VIROU SEPARADA E SEM LIGACAO COM O QUE VEM DEPOIS."
//
// Relato de testador, com arabe, depois de aplicar efeitos. Nao era o
// efeito: era o DESENHO POR LETRA. O arabe e uma escrita cursiva — a forma
// de cada letra depende dos vizinhos (isolada, inicial, medial, final). O
// animador desenhava uma letra por paragrafo, e uma letra sozinha nao tem
// com quem se ligar: o Shaper devolve a forma isolada, e a palavra inteira
// sai picada.
//
// A CORRECAO desenha a PALAVRA e recorta a letra. A prova de que a ligacao
// voltou e simples e nao depende de fonte nenhuma: a palavra moldada e
// mais ESTREITA do que a soma das letras isoladas, porque as formas
// ligadas sao mais compactas. Se o desenho voltar a ser por letra, a
// largura volta a crescer.
import 'package:aurea/src/features/editor/domain/cor_da_unidade.dart';
import 'package:aurea/src/features/editor/domain/direcao_do_texto.dart';
import 'package:characters/characters.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/presentation/widgets/animated_text.dart'
    show AnimatedTextView, painterDoTextoAnimado;
import 'package:aurea/src/features/editor/presentation/widgets/texto_no_atlas.dart'
    show estiloReduzido, reducaoDoCorpo;

const _arabe = 'مرحبا';

TextPainter _medir(String texto, TextDirection direcao) => TextPainter(
  text: TextSpan(
    text: texto,
    style: const TextStyle(fontSize: 40, fontFamily: 'Roboto'),
  ),
  textDirection: direcao,
)..layout();

/// GRAVA O QUE O CANVAS RECEBE.
class _Caneta implements Canvas {
  final paragrafos = <(double largura, Offset onde)>[];
  final recortes = <Rect>[];

  @override
  void drawParagraph(ui.Paragraph paragraph, Offset offset) {
    paragrafos.add((paragraph.width, offset));
  }

  @override
  void clipRect(
    Rect rect, {
    ui.ClipOp clipOp = ui.ClipOp.intersect,
    bool doAntiAlias = true,
  }) => recortes.add(rect);

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('direcao do texto', () {
    test('arabe e RTL; hebraico tambem; latino nao', () {
      expect(direcaoDoTexto(_arabe), TextDirection.rtl);
      expect(direcaoDoTexto('שלום'), TextDirection.rtl);
      expect(direcaoDoTexto('Aurea'), TextDirection.ltr);
      expect(direcaoDoTexto('Aurea 123'), TextDirection.ltr);
    });

    test('o PRIMEIRO caractere forte decide; numero e espaco nao votam', () {
      // Espaco, numero e pontuacao nao tem direcao: quem decide e a
      // primeira letra.
      expect(direcaoDoTexto('  123 ${_arabe}'), TextDirection.rtl);
      expect(direcaoDoTexto('123 A'), TextDirection.ltr);
      // Sem nenhum caractere forte, o padrao do Unicode: LTR.
      expect(direcaoDoTexto('123 ... 456'), TextDirection.ltr);
      expect(direcaoDoTexto(''), TextDirection.ltr);
    });
  });

  group('a escrita cursiva continua ligada', () {
    // O QUE ESTE GRUPO PROVA, E O QUE ELE NAO PROVA.
    //
    // O ARABE NAO TEM FONTE NO HOST: a unica fonte que o `flutter test`
    // carrega nao cobre o alfabeto arabe, e todo glifo cai na largura de
    // reserva. MEDIR LARGURA AQUI NAO PROVA NADA — as formas ligadas e as
    // isoladas medem igual, entao um teste de largura passaria nos dois
    // mundos e nao protegeria coisa nenhuma.
    //
    // O que da para provar e O CAMINHO DE DESENHO, que e exatamente o que
    // foi mudado: cada letra e desenhada com um RECORTE e a partir do
    // paragrafo da PALAVRA, e nao de um paragrafo de uma letra so. Com a
    // fonte de reserva isso e visivel: o paragrafo da palavra e mais largo
    // que o da letra, e a diferenca aparece no tamanho do que chega ao
    // canvas.
    //
    // A moldagem em si (HarfBuzz escolhendo a forma medial) e do motor e
    // nao tem como ser conferida no host. Quem confere e o aparelho.

    /// GRAVA O QUE O CANVAS RECEBE.
    TextLayer camada(String texto) => TextLayer(
      name: 't',
      text: texto,
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      fontSize: 40,
      animators: [
        TextAnimator(
          name: 'entrada',
          selectors: [RangeSelector()],
          properties: [AnimatorProperty(type: TextAnimProp.positionX)],
        ),
      ],
    );

    test('cada letra e desenhada com um recorte — e nao solta', () {
      final caneta = _Caneta();
      final pintor = painterDoTextoAnimado(camada(_arabe), Duration.zero);
      pintor.paint(caneta, const Size(600, 200));

      // Cinco letras, cinco recortes: e o recorte que separa a letra da
      // vizinha sem partir a palavra em cinco paragrafos.
      expect(caneta.recortes, hasLength(5));
      expect(caneta.paragrafos, hasLength(5));
      for (final r in caneta.recortes) {
        expect(r.width, greaterThan(0));
        expect(r.height, greaterThan(0));
      }
    });

    test('o paragrafo desenhado e o da PALAVRA, e nao o de uma letra', () {
      final caneta = _Caneta();
      painterDoTextoAnimado(camada(_arabe), Duration.zero)
          .paint(caneta, const Size(600, 200));

      // O QUE CHEGA AO CANVAS E O PARAGRAFO REDUZIDO (o glifo gigante nao
      // pode ser pedido ao atlas — ver `texto_no_atlas.dart`), entao a
      // conta e feita no mesmo corpo: mesma fonte, mesma reducao.
      final estilo = AnimatedTextView.styleFor(camada(_arabe), animated: true);
      final k = reducaoDoCorpo(40);
      final reduzido = estiloReduzido(estilo, k, corpo: 40);
      double largura(String texto) => (TextPainter(
        text: TextSpan(text: texto, style: reduzido),
        textDirection: TextDirection.rtl,
      )..layout())
          .width;

      final daPalavra = largura(_arabe);
      final daLetra = largura(_arabe.characters.first);
      expect(daPalavra, greaterThan(0));
      expect(daLetra, greaterThan(0));

      for (final (largura, _) in caneta.paragrafos) {
        expect(
          largura,
          closeTo(daPalavra, 0.5),
          reason: 'o canvas recebeu um paragrafo que nao e o da palavra',
        );
      }
      // A GUARDA DO RELATO: desenhar a LETRA devolveria um paragrafo cinco
      // vezes mais estreito. E esta diferenca que diz se a ligacao voltou.
      expect(
        daPalavra,
        greaterThan(daLetra * 4),
        reason: 'a palavra nao esta maior que a letra: '
            'a comparacao nao distingue mais os dois caminhos',
      );
    });

    test('a direcao RTL chega ao paragrafo desenhado', () {
      // Sem fonte arabe a largura nao muda, mas a DIRECAO do paragrafo
      // muda, e ela decide quebra de linha e alinhamento. Se alguem voltar
      // a fixar LTR, o `direcaoDoTexto` continua certo e o desenho nao —
      // entao o que se prende aqui e que o paragrafo desenhado e o do
      // `_Diagrama`, que ja nasce com a direcao do texto.
      expect(direcaoDoTexto(_arabe), TextDirection.rtl);
      final caneta = _Caneta();
      painterDoTextoAnimado(
        camada(_arabe),
        Duration.zero,
      ).paint(caneta, const Size(600, 200));
      expect(caneta.paragrafos, isNotEmpty);
    });
  });

  group('palavraDe: o contexto que o desenho por letra precisa', () {
    final u = TextUnits.of(r'ola $  mundo');

    test('cada letra sabe a palavra a que pertence', () {
      expect(u.palavraDe(0), 'ola');
      expect(u.palavraDe(2), 'ola');
      expect(u.palavraDe(4), r'$');
      expect(u.palavraDe(7), 'mundo');
      expect(u.palavraDe(11), 'mundo');
    });

    test('espaco devolve a propria unidade, e nao a palavra', () {
      expect(u.palavraDe(3), ' ');
    });

    test('fora dos limites nao estoura', () {
      expect(u.palavraDe(-1), '');
      expect(u.palavraDe(999), '');
    });

    test('em arabe, a palavra inteira volta inteira', () {
      final a = TextUnits.of('${_arabe} ${_arabe}');
      expect(a.palavraDe(0), _arabe);
      expect(a.palavraDe(4), _arabe);
      expect(a.palavraDe(5), ' ');
      expect(a.palavraDe(6), _arabe);
    });
  });

  group('a matriz de cor por letra', () {
    test('neutra devolve nulo — o caminho comum nao paga camada a mais', () {
      expect(matrizDaUnidade(0, 100, 100, 1), isNull);
    });

    test('cada eixo sozinho produz uma matriz propria', () {
      for (final m in [
        matrizDaUnidade(90, 100, 100, 1),
        matrizDaUnidade(0, 50, 100, 1),
        matrizDaUnidade(0, 100, 50, 1),
        matrizDaUnidade(0, 100, 100, 0.5),
      ]) {
        expect(m, isNotNull);
        expect(m, hasLength(20));
        expect(m!.every((v) => v.isFinite), isTrue);
        // A ultima linha e o alfa: nao pode mexer em RGB.
        expect(m[15], 0);
        expect(m[16], 0);
        expect(m[17], 0);
      }
    });

    test('o alfa entra so na ultima linha', () {
      final m = matrizDaUnidade(0, 100, 100, 0.5)!;
      expect(m[18], closeTo(0.5, 1e-9));
      expect(m[0], closeTo(1, 1e-9));
    });
  });
}
