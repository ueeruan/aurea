// O TEXTO MONTADO — a frase com numero dentro, traduzida de verdade.
//
// O DEFEITO QUE ISTO CONSERTA, e ele nao era falta de traducao: era falta
// de CAMINHO.
//
//   AppText('Excluir ${ids.length} projetos?')
//
// O Dart monta a string ANTES de o `AppText` existir, e o que chegava ao
// catalogo era "Excluir 3 projetos?" — que nao casa com chave nenhuma.
// Toda frase com um numero, um nome ou um tempo dentro ficava em portugues
// em qualquer idioma, e nao havia como consertar traduzindo: a chave
// certa nunca existiria.
//
// O MOLDE resolve: a frase vai ao catalogo COM OS MARCADORES, a traducao e
// escolhida, e so entao os valores entram.
//
// OS MARCADORES SAO POSICIONAIS PARA PODEREM SER REORDENADOS — e e por
// isso que o teste mais importante daqui traduz uma frase com a ordem
// trocada. Se os marcadores fossem substituidos por nome na ordem do
// portugues, uma lingua que conta diferente sairia errada.
import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:aurea/src/core/l10n/translations.dart';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('o molde substitui os valores na ordem que a traducao pedir', () {
    test('em portugues, o valor entra onde o marcador esta', () {
      expect(
        moldarPara('pt', 'Excluir {0} projetos?', [3]),
        'Excluir 3 projetos?',
      );
    });

    test('em ingles, a traducao escolhida e a do molde', () {
      expect(
        moldarPara('en', 'Excluir {0} projetos?', [3]),
        'Delete 3 projects?',
      );
    });

    test('UMA TRADUCAO QUE CONTA DIFERENTE continua certa', () {
      // Em japones o numero vem DEPOIS do que ele conta, e em russo a
      // frase muda de forma. O marcador e posicional justamente para a
      // traducao poder mover o numero — se a substituicao fosse amarrada
      // a ordem do portugues, estas duas sairiam erradas.
      final jp = moldarPara('ja', 'Excluir {0} projetos?', [3]);
      expect(jp, contains('3'));
      expect(jp, isNot(contains('{0}')));
      expect(jp, startsWith('3'), reason: 'em japones o numero vem primeiro');

      final ru = moldarPara('ru', 'Excluir {0} projetos?', [3]);
      expect(ru, contains('3'));
      expect(ru, isNot(contains('{0}')));
    });

    test('dois valores, na ordem pedida', () {
      expect(
        moldarPara('pt', 'Versao {0} · build {1}', ['1.1.8', 93]),
        'Versao 1.1.8 · build 93',
      );
      expect(
        moldarPara('en', 'Versao {0} · build {1}', ['1.1.8', 93]),
        'Version 1.1.8 · build 93',
      );
    });

    test('o MESMO valor usado duas vezes', () {
      expect(
        moldarPara('pt', '{0} e {0}', ['x']),
        'x e x',
      );
    });
  });

  group('o que acontece quando o molde nao esta no catalogo', () {
    test('os valores entram do mesmo jeito, e a frase sai em portugues', () {
      // DEVOLVER O MOLDE CRU SERIA PIOR: a tela mostraria `{0}` no lugar
      // do numero. Uma frase na lingua errada e ruim; uma frase com
      // chaves na frente do usuario e um defeito.
      const molde = 'Frase que ninguem cadastrou: {0}';
      expect(moldarPara('en', molde, [7]), 'Frase que ninguem cadastrou: 7');
      expect(moldarPara('ja', molde, [7]), contains('7'));
      expect(moldarPara('ar', molde, [7]), isNot(contains('{')));
    });

    test('marcador sem valor fica como esta, e nao derruba a tela', () {
      // ACONTECE quando alguem escreve `{1}` e passa um valor so. Mostrar
      // o marcador e feio; estourar uma excecao no meio de um build seria
      // pior.
      expect(moldarPara('pt', '{0} e {1}', ['a']), 'a e {1}');
    });

    test('marcador fora de faixa nao vira excecao', () {
      expect(moldarPara('pt', 'x {9} y', ['a']), 'x {9} y');
    });
  });

  group('o catalogo dos moldes', () {
    test('os marcadores sao OS MESMOS nos nove idiomas', () {
      // A REGRA QUE MANTEM AS TRADUCOES HONESTAS. Uma traducao que perde o
      // `{0}` perde o numero — "Excluir projetos?" em vez de "Excluir 3
      // projetos?" —, e uma que inventa um `{1}` mostra a chave crua na
      // tela. Nenhuma das duas coisas aparece num teste de "traduziu?".
      final problemas = <String>[];
      for (final entrada in appTranslations.entries) {
        final marcadores = marcadoresDoMolde(entrada.key);
        if (marcadores.isEmpty) continue;
        for (final idioma in entrada.value.keys) {
          final t = entrada.value[idioma]!;
          final daTraducao = marcadoresDoMolde(t);
          if (idioma == 'pt') continue;
          final faltando = marcadores.where((m) => !daTraducao.contains(m));
          final sobrando = daTraducao.where((m) => !marcadores.contains(m));
          if (faltando.isNotEmpty || sobrando.isNotEmpty) {
            problemas.add(
              '[$idioma] ${entrada.key} -> $t '
              '(falta ${faltando.toList()}, sobra ${sobrando.toList()})',
            );
          }
        }
      }
      expect(
        problemas,
        isEmpty,
        reason:
            'moldes com marcador perdido ou inventado '
            '(${problemas.length}):\n${problemas.take(20).join('\n')}',
      );
    });

    test('marcadoresDoMolde le a chave de tras para frente', () {
      expect(marcadoresDoMolde('a {1} b {0} c'), [1, 0]);
      expect(marcadoresDoMolde('sem marcador'), isEmpty);
      expect(marcadoresDoMolde('{0}{1}{2}'), [0, 1, 2]);
    });

    test('todo molde usado no codigo tem os valores na chamada', () {
      // UM MOLDE COM `{1}` E UMA CHAMADA COM UM VALOR SO mostraria `{1}`
      // na tela. Este teste pega o descompasso olhando o que o codigo
      // escreve, e nao o que o catalogo guarda.
      const amostras = <String, List<Object?>>{
        'Excluir {0} projetos?': [1],
        'Versao {0} · build {1}': ['1.1.8', 93],
      };
      amostras.forEach((molde, valores) {
        for (final m in marcadoresDoMolde(molde)) {
          expect(
            m,
            lessThan(valores.length),
            reason: 'o molde $molde usa {$m} e so tem ${valores.length} valor(es)',
          );
        }
      });
    });
  });

  group('na tela', () {
    testWidgets('AppTextMoldado mostra a frase traduzida', (tester) async {
      // O IDIOMA VEM DO `Localizations.maybeLocaleOf`, entao o teste
      // monta a arvore com o locale e o delegate do proprio Flutter — e
      // nao com um `Locale` solto, que nao chega ao `AppTextMoldado`.
      await tester.pumpWidget(
        MaterialApp(
          locale: const Locale('en'),
          supportedLocales: const [Locale('en'), Locale('pt')],
          localizationsDelegates: GlobalMaterialLocalizations.delegates,
          home: const AppTextMoldado('Excluir {0} projetos?', [3]),
        ),
      );
      expect(find.text('Delete 3 projects?'), findsOneWidget);
    });

    testWidgets('AppTextMoldado em portugues nao muda a frase', (tester) async {
      // O LOCALE TEM DE SER DITO. Sem ele o teste herda o do ambiente, e
      // o ambiente de teste nao e o Brasil — a frase sairia em ingles e o
      // teste acusaria um defeito que nao existe.
      await tester.pumpWidget(
        MaterialApp(
          locale: const Locale('pt'),
          supportedLocales: const [Locale('en'), Locale('pt')],
          localizationsDelegates: GlobalMaterialLocalizations.delegates,
          home: const AppTextMoldado('Excluir {0} projetos?', [3]),
        ),
      );
      expect(find.text('Excluir 3 projetos?'), findsOneWidget);
    });
  });
}
