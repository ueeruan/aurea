// O TEXTO DO AUREA EM TODOS OS IDIOMAS QUE ELE DIZ FALAR.
//
// O RELATO QUE ORIGINOU ISTO foi o arabe ("cada letra virou separada") — e
// ele foi tratado como se fosse um problema do arabe. Nao e. O mesmo
// caminho de desenho atende coreano, japones, chines, hindi, hebraico,
// russo e emoji, e cada um desses tem uma armadilha propria:
//
//   * ARABE e HEBRAICO sao cursivos ou RTL: a forma depende do vizinho, e
//     quem carrega a forma e a PALAVRA;
//   * COREANO e escrito em jamo que se COMPOEM em bloco: partir por code
//     unit separa a consoante da vogal e o bloco some;
//   * HINDI (devanagari) tem marcas que se juntam a consoante e clusters
//     que so existem juntos;
//   * CHINES e JAPONES nao tem espaco: a quebra de linha e por caractere, e
//     "palavra" nao e o que separa as coisas;
//   * EMOJI tem sequencias com ZWJ (a familia e UM desenho) e seletores de
//     variacao — contar code units transforma um emoji em sete.
//
// A PROVA AQUI NAO DEPENDE DE FONTE. O host do `flutter test` so tem fonte
// latina: medir tinta com texto coreano mediria a largura de reserva. O que
// se prova e O CAMINHO DE DESENHO — quantas unidades existem e quantas
// chegam ao canvas —, e isso e exatamente onde "um trecho desapareceu"
// acontece.
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/direcao_do_texto.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:aurea/src/features/editor/presentation/widgets/animated_text.dart'
    show painterDoTextoAnimado;
import 'package:characters/characters.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// UMA AMOSTRA POR ESCRITA, com o que aquela escrita tem de dificil.
///
/// Os nomes sao o idioma; o comentario diz o que a amostra cobra.
const _amostras = <String, String>{
  'Português': 'Ação, coração e você',
  'English': 'Hello, world! Motion',
  // O espanhol abre a interrogacao com o sinal invertido: um caractere
  // que so existe nesta escrita e que um filtro de "letra" descartaria.
  'Español': '¡Hola! ¿Añadir efecto?',
  // RTL e cursivo: a letra muda de forma conforme os vizinhos.
  'العربية': 'مرحبا بالعالم',
  // RTL e nao cursivo: o teste gemeo do arabe, com a mesma direcao e
  // outra regra de forma.
  'עברית': 'שלום עולם',
  // SILABAS COMPOSTAS: cada bloco e um cluster de 2 a 4 code units.
  '한국어': '안녕하세요 세계',
  // SEM ESPACO: a quebra de linha e por caractere.
  '日本語': 'こんにちは世界',
  '简体中文': '你好，世界',
  // MARCAS QUE SE JUNTAM: a vogal nao existe sem a consoante.
  'हिन्दी': 'नमस्ते दुनिया',
  'Русский': 'Привет, мир!',
  'Bahasa': 'Halo, dunia! Sebuah efek',
  // ZWJ, seletor de variacao e bandeira: sequencias que sao UM desenho.
  'Emoji': 'Olá 👨‍👩‍👧‍👦 🇧🇷 ✨',
  // ACENTO COMBINANTE: "e" + acento agudo e um cluster, nao dois.
  'Combinante': 'café com açúcar',
};

/// GRAVA O QUE O CANVAS RECEBE.
class _Caneta implements Canvas {
  final paragrafos = <(double largura, Offset onde)>[];
  final recortes = <Rect>[];
  int camadas = 0;

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
  void saveLayer(Rect? bounds, Paint paint) => camadas++;

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

/// A CAMADA DE TEXTO com um animador de entrada — o caminho que desenha
/// unidade por unidade.
TextLayer _camada(String texto, {TextAnimProp? extra}) => TextLayer(
  name: 't',
  text: texto,
  startTime: Duration.zero,
  duration: const Duration(seconds: 4),
  fontSize: 40,
  animators: [
    TextAnimator(
      name: 'entrada',
      selectors: [RangeSelector()],
      properties: [
        AnimatorProperty(type: TextAnimProp.positionX),
        if (extra != null) AnimatorProperty(type: extra),
      ],
    ),
  ],
);

/// QUANTAS UNIDADES DESENHAVEIS o texto tem: os clusters que nao sao espaco.
int _desenhaveis(String texto) =>
    TextUnits.of(texto).clusters.where((c) => c.trim().isNotEmpty).length;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('a segmentacao nao perde nada, em escrita nenhuma', () {
    test('os clusters reconstroem o texto caractere a caractere', () {
      // O TESTE MAIS DIRETO QUE EXISTE para "um trecho desapareceu": juntar
      // os pedacos tem de devolver o texto ORIGINAL, byte a byte. Uma
      // segmentacao por code unit partiria o emoji e o jamo no meio e a
      // juncao nao voltaria igual.
      for (final entrada in _amostras.entries) {
        final units = TextUnits.of(entrada.value);
        expect(
          units.clusters.join(),
          entrada.value,
          reason: '${entrada.key}: a segmentacao perdeu ou trocou caractere',
        );
      }
    });

    test('a contagem e de CLUSTERS, e nao de code units', () {
      // O EMOJI DA FAMILIA E UM DESENHO SO. Contado por code unit ele
      // viraria sete, e o animador desenharia sete pedacos de nada.
      const familia = '👨‍👩‍👧‍👦';
      expect(familia.length, greaterThan(10), reason: 'code units');
      expect(familia.characters.length, 1, reason: 'clusters');
      expect(TextUnits.of(familia).length, 1);

      // O ACENTO COMBINANTE tambem: "e" + agudo e UMA letra.
      const combinante = 'é';
      expect(combinante.length, 2);
      expect(TextUnits.of(combinante).length, 1);

      // A SILABA COREANA: bloco de tres code units, um cluster.
      const silaba = '안';
      expect(silaba.length, 1);
      expect(TextUnits.of('안녕').length, 2);
    });

    test('toda unidade pertence a uma palavra, e a palavra a contem', () {
      for (final entrada in _amostras.entries) {
        final units = TextUnits.of(entrada.value);
        for (var i = 0; i < units.length; i++) {
          if (units.isWhitespace[i]) continue;
          final palavra = units.palavraDe(i);
          expect(palavra, isNotEmpty, reason: '${entrada.key} unidade $i');
          expect(
            palavra.contains(units.clusters[i]),
            isTrue,
            reason:
                '${entrada.key}: a palavra "$palavra" nao contem a unidade '
                '"${units.clusters[i]}"',
          );
        }
      }
    });

    test('o texto de uma palavra inclui as letras sem espaco entre elas', () {
      // A PALAVRA E O QUE CARREGA A LIGACAO. Se ela viesse cortada, o
      // desenho por unidade voltaria a desenhar letra solta — que foi o
      // defeito do arabe, e que tambem estragaria o devanagari.
      final units = TextUnits.of('مرحبا بالعالم');
      final daPrimeira = units.palavraDe(0);
      expect(daPrimeira, 'مرحبا');
      // E o espaco nao entra na palavra.
      expect(daPrimeira.contains(' '), isFalse);
    });
  });

  group('a direcao sai da escrita, e nao da fixacao', () {
    test('RTL onde e RTL, LTR em todo o resto', () {
      expect(direcaoDoTexto(_amostras['العربية']!), TextDirection.rtl);
      expect(direcaoDoTexto(_amostras['עברית']!), TextDirection.rtl);
      for (final idioma in const [
        'Português',
        'English',
        'Español',
        '한국어',
        '日本語',
        '简体中文',
        'हिन्दी',
        'Русский',
        'Bahasa',
      ]) {
        expect(
          direcaoDoTexto(_amostras[idioma]!),
          TextDirection.ltr,
          reason: idioma,
        );
      }
    });

    test('texto misturado: quem decide e a PRIMEIRA letra', () {
      // Um titulo que comeca em arabe e termina em latim continua RTL; o
      // contrario, LTR. E a regra do Unicode, e e ela que o Flutter segue
      // na quebra de linha e no alinhamento.
      expect(direcaoDoTexto('مرحبا Aurea'), TextDirection.rtl);
      expect(direcaoDoTexto('Aurea مرحبا'), TextDirection.ltr);
      // Numero e espaco nao votam.
      expect(direcaoDoTexto('  123 مرحبا'), TextDirection.rtl);
    });
  });

  group('nada desaparece no desenho por unidade', () {
    /// Desenha no tempo [t] e devolve a caneta.
    _Caneta desenha(String texto, Duration t, {TextAnimProp? extra}) {
      final caneta = _Caneta();
      painterDoTextoAnimado(_camada(texto, extra: extra), t)
          .paint(caneta, const Size(900, 300));
      return caneta;
    }

    test('cada unidade desenhavel chega ao canvas, em toda escrita', () {
      // ESTE E O TESTE DO RELATO, generalizado. "Ao aplicar animacao ou
      // efeito, nenhum caractere pode desaparecer": o numero de unidades
      // desenhadas tem de ser o numero de unidades que existem.
      //
      // O tempo e o FIM da camada (a animacao assentada) de proposito: no
      // inicio de uma entrada, zero opacidade e o comportamento certo, e
      // nao um defeito.
      for (final entrada in _amostras.entries) {
        final caneta = desenha(
          entrada.value,
          const Duration(milliseconds: 3900),
        );
        final esperado = _desenhaveis(entrada.value);
        expect(
          caneta.recortes,
          hasLength(esperado),
          reason: '${entrada.key}: recortes desenhados',
        );
        expect(
          caneta.paragrafos,
          hasLength(esperado),
          reason: '${entrada.key}: paragrafos desenhados',
        );
      }
    });

    test('o efeito de desfoque nao come unidade', () {
      // O DESFOQUE ABRE UM `saveLayer` POR UNIDADE. Se a camada fosse
      // montada fora do laco, o desfoque pegaria o texto inteiro — e o
      // que se prova aqui e que cada unidade continua sendo desenhada.
      for (final idioma in const ['العربية', '한국어', 'हिन्दी', 'Emoji']) {
        final caneta = desenha(
          _amostras[idioma]!,
          const Duration(milliseconds: 3900),
          extra: TextAnimProp.blur,
        );
        expect(
          caneta.paragrafos,
          hasLength(_desenhaveis(_amostras[idioma]!)),
          reason: idioma,
        );
      }
    });

    test('o efeito de cor nao come unidade', () {
      for (final idioma in const ['العربية', '日本語', '简体中文', 'Emoji']) {
        final caneta = desenha(
          _amostras[idioma]!,
          const Duration(milliseconds: 3900),
          extra: TextAnimProp.hue,
        );
        expect(
          caneta.paragrafos,
          hasLength(_desenhaveis(_amostras[idioma]!)),
          reason: idioma,
        );
      }
    });

    test('todo recorte tem area — nao ha recorte vazio', () {
      // UM RECORTE DE AREA ZERO E UM CARACTERE QUE NAO APARECE: o clip
      // corta tudo o que viria depois. E a forma mais silenciosa de
      // "sumiu uma letra".
      for (final entrada in _amostras.entries) {
        final caneta = desenha(
          entrada.value,
          const Duration(milliseconds: 3900),
        );
        for (final r in caneta.recortes) {
          expect(r.width, greaterThan(0), reason: entrada.key);
          expect(r.height, greaterThan(0), reason: entrada.key);
        }
      }
    });

    test('o paragrafo desenhado e o da PALAVRA, em toda escrita', () {
      // O CAMINHO CERTO E UM SO: desenhar a palavra e recortar a unidade.
      // Desenhar a letra solta quebraria a cursiva do arabe E a ligacao do
      // devanagari — e a prova de que o paragrafo e o da palavra nao
      // depende de fonte: a palavra e mais larga que a letra.
      for (final idioma in const ['العربية', 'עברית', 'हिन्दी', '한국어']) {
        final texto = _amostras[idioma]!;
        final caneta = desenha(texto, const Duration(milliseconds: 3900));
        expect(caneta.paragrafos, isNotEmpty, reason: idioma);
        final larguras = caneta.paragrafos.map((p) => p.$1).toSet();
        expect(
          larguras.length,
          lessThanOrEqualTo(TextUnits.of(texto).wordCount),
          reason: '$idioma: mais larguras distintas que palavras',
        );
      }
    });

    test('o tempo nao muda QUANTAS unidades existem, so onde elas estao', () {
      // Desenhar em instantes diferentes nao pode fazer aparecer nem sumir
      // unidade: o animador muda posicao, opacidade e giro, e nao a
      // contagem. Um `continue` novo no meio do laco cairia aqui.
      for (final idioma in const ['العربية', '한국어', 'Emoji', 'हिन्दी']) {
        final texto = _amostras[idioma]!;
        final esperado = _desenhaveis(texto);
        for (final ms in [900, 1800, 2700, 3900]) {
          final caneta = desenha(texto, Duration(milliseconds: ms));
          expect(
            caneta.paragrafos.length,
            esperado,
            reason: '$idioma em ${ms}ms',
          );
        }
      }
    });
  });
}
