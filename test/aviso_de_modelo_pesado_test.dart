// O AVISO "MODELO PESADO" — o que ele mostra e o que ele devolve.
//
// Ele substitui um aviso fixo que aparecia em TODA importacao 3D, ate de um
// cubo, dizendo que o app "nao vai reduzir o arquivo". Aviso que aparece
// sempre ninguem le, e esse ainda mentia sobre a unica coisa que o dono
// queria: reduzir. Agora ele so aparece quando a ficha do modelo passa dos
// limites, diz o que pesa, e o antes -> depois da otimizacao.
//
// O que fica preso aqui:
//   * os tres botoes devolvem o que dizem (e cancelar devolve `null`);
//   * o antes -> depois aparece quando ha o que reduzir, e o numero sozinho
//     quando nao ha (nada de "2048 px → 2048 px");
//   * a licenca sem derivados vira um lembrete, e nao um bloqueio.
import 'package:aurea/src/features/editor/domain/analise_do_modelo.dart';
import 'package:aurea/src/features/editor/presentation/widgets/importacao_3d.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_test/flutter_test.dart';

/// Todo o texto visivel do aviso, numa linha so.
///
/// `bySubtype` e nao `byType`: metade do aviso e `AppText` (que ESTENDE
/// `Text` para passar pelo catalogo de traducao), e `byType` casa tipo
/// exato — com ele o teste leria so metade da ficha e passaria a acreditar
/// que a outra metade nao existe.
String _texto(WidgetTester tester) => tester
    .widgetList<Text>(find.bySubtype<Text>())
    .map((t) => t.data ?? '')
    .join(' | ');

void main() {
  const pesado = AnaliseDoModelo(
    triangulos: 1200000,
    vertices: 700000,
    texturas: 4,
    maiorTextura: 4096,
    pixelsDasTexturas: 4096 * 4096 * 4,
    materiais: 6,
    animacoes: 2,
    ossos: 48,
    memoriaDaMalha: 60 * 1024 * 1024,
    memoriaDasTexturas: 180 * 1024 * 1024,
  );

  Future<EscolhaDoPeso?> abrir(
    WidgetTester tester, {
    AnaliseDoModelo analise = pesado,
    String? licenca,
    required String toque,
  }) async {
    // Tela de celular inteira: a ficha tem cinco linhas e o aviso tem tres
    // botoes, e nos 600 px do teste o ultimo deles fica fora da vista (no
    // aparelho a lista de acoes do CupertinoAlertDialog rola).
    tester.view.physicalSize = const Size(400, 1400);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    EscolhaDoPeso? resposta;
    var respondeu = false;
    await tester.pumpWidget(
      CupertinoApp(
        home: Builder(
          builder: (context) => CupertinoButton(
            child: const Text('abrir'),
            onPressed: () async {
              resposta = await perguntarModeloPesado(
                context,
                analise,
                licenca: licenca,
              );
              respondeu = true;
            },
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    expect(find.byType(AvisoDeModeloPesado), findsOneWidget);
    if (toque.isNotEmpty) {
      await tester.tap(find.byKey(ValueKey('modelo-pesado-$toque')));
      await tester.pumpAndSettle();
      expect(respondeu, isTrue, reason: 'o aviso nao devolveu resposta');
    }
    return resposta;
  }

  testWidgets('otimizar devolve a escolha de otimizar', (tester) async {
    expect(await abrir(tester, toque: 'otimizar'), EscolhaDoPeso.otimizar);
  });

  testWidgets('importar original devolve a escolha de original', (
    tester,
  ) async {
    expect(await abrir(tester, toque: 'original'), EscolhaDoPeso.original);
  });

  testWidgets('cancelar devolve nada (a importacao para)', (tester) async {
    expect(await abrir(tester, toque: 'cancelar'), isNull);
  });

  testWidgets('a ficha mostra o antes -> depois do que vai mudar', (
    tester,
  ) async {
    await abrir(tester, toque: '');
    final texto = _texto(tester);
    // 1,2 milhao -> 150 mil triangulos, 4096 -> 1024 px.
    expect(texto, contains('1,2 milhão'));
    expect(texto, contains('150 mil'));
    expect(texto, contains('4096'));
    expect(texto, contains('1024'));
    expect(texto, contains('→'));
    // Materiais, animacoes e ossos so informam: a otimizacao nao os toca.
    expect(texto, contains('6'));
    expect(texto, contains('48'));
  });

  testWidgets('sem o que reduzir, nao aparece seta nenhuma', (tester) async {
    // Pesado so pelo arquivo no disco: a malha e a textura ja cabem, e
    // "150 mil → 150 mil" seria ruido.
    await abrir(
      tester,
      analise: const AnaliseDoModelo(
        triangulos: 80000,
        vertices: 40000,
        texturas: 2,
        maiorTextura: 1024,
        materiais: 2,
        arquivoBytes: 300 * 1024 * 1024,
      ),
      toque: '',
    );
    expect(_texto(tester), isNot(contains('→')));
  });

  testWidgets('licenca sem derivados vira lembrete, nao bloqueio', (
    tester,
  ) async {
    await abrir(
      tester,
      licenca: 'CC Attribution-NonCommercial-NoDerivs',
      toque: '',
    );
    expect(_texto(tester), contains('derivadas'));
    // O botao de otimizar continua la: o app informa, nao policia.
    expect(find.byKey(const ValueKey('modelo-pesado-otimizar')), findsOneWidget);
  });

  testWidgets('licenca comum nao mostra lembrete de derivados', (tester) async {
    await abrir(tester, licenca: 'CC Attribution', toque: '');
    expect(_texto(tester), isNot(contains('derivadas')));
  });
}
