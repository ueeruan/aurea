import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// A ABA DO TRILHO DIREITO DO PAINEL.
///
/// O trilho e uma coluna de ICONES: nao ha texto visivel nem Tooltip, e o
/// que nomeia cada aba e o rotulo de acessibilidade (`Semantics`). Procurar
/// por `find.byTooltip('Mover')` nao acha nada — por isso este finder
/// existe, e por isso os testes que o usavam acusavam o painel de nao
/// caber quando o problema era o proprio finder.
Finder abaDoTrilho(String label) => find.byWidgetPredicate(
  (w) => w is Semantics && w.properties.label == label,
);

/// Follow visible controls, including secondary transform tools in Options.
Future<void> selectTransformTool(WidgetTester tester, String label) async {
  if (label == 'Pivo' || label == 'Opacid.') {
    // O TOOLTIP CARREGA O ESTADO DO AUTO-KEY: "Opções de transformação" e
    // "Opções de transformação · auto-key ligado" sao o MESMO botao. Casar
    // pelo texto exato fazia o teste depender de um ajuste que a pessoa
    // liga e desliga — e o ajuste nasce LIGADO, entao o caminho comum era
    // justamente o que nao casava.
    await tester.tap(
      find.byWidgetPredicate(
        (w) =>
            w is Tooltip &&
            (w.message ?? '').startsWith('Opções de transformação'),
      ),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.text(label == 'Pivo' ? 'Editar pivô' : 'Opacidade'));
  } else {
    await tester.tap(abaDoTrilho(label));
  }
  await tester.pumpAndSettle();
}

/// DIGITA NO CAMPO DE VALOR DO PAINEL DE TRANSFORMACAO.
///
/// SAO DOIS DIALOGOS DE VALOR no aplicativo, com chaves diferentes: o da
/// LINHA DE PARAMETRO (`valor-campo`) e o do CAMPO do painel novo
/// (`campo-de-valor-entrada`). Usar a chave do outro faz o teste procurar
/// um campo que nao esta na tela, e o sintoma que aparece e "o toque nao
/// abriu nada" — que e exatamente o que este finder veio separar.
Future<void> digitarNoCampoDeValor(WidgetTester tester, String texto) async {
  final campo = find.byKey(const ValueKey('campo-de-valor-entrada'));
  expect(campo, findsOneWidget, reason: 'o teclado do campo abriu');
  await tester.enterText(campo, texto);
  await tester.pumpAndSettle();
  await tester.tap(find.byKey(const ValueKey('campo-de-valor-ok')));
  await tester.pumpAndSettle();
}

/// O MENU COMPLETO DA CAMADA — o ⋮ do cabecalho dela.
///
/// SAO DOIS PASSOS, e nao um. O ⋮ abre o menu do DIA A DIA (congelar,
/// parentesco, excluir) e a grade inteira de acoes mora atras de
/// "Todas as acoes…". Quando as acoes rapidas estavam soltas no menu,
/// um toque bastava; agora quem procura `mais-dividir` precisa passar
/// pela porta que existe.
Future<void> openLayerActions(WidgetTester tester) async {
  await tester.tap(find.byKey(const ValueKey('camada-menu')));
  await tester.pumpAndSettle();
  // A PORTA E ROLAVEL E "Todas as acoes..." E A ULTIMA: tocar sem rolar
  // manda o dedo para o que estiver naquele ponto — que no menu da
  // camada e o fundo, e o toque no fundo fecha a folha. O sintoma era
  // "o menu nao tem as acoes", e o menu tinha.
  final todas = find.byKey(const ValueKey('camada-menu-todas'));
  await tester.ensureVisible(todas);
  await tester.pumpAndSettle();
  await tester.tap(todas);
  await tester.pumpAndSettle();
}

Future<void> closeEditorPanel(WidgetTester tester) async {
  await tester.tap(find.byKey(const ValueKey('editor-back')));
  await tester.pumpAndSettle();
}
