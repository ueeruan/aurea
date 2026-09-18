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

Future<void> openLayerActions(WidgetTester tester) async {
  await tester.tap(find.text('Mais').last);
  await tester.pumpAndSettle();
}

Future<void> closeEditorPanel(WidgetTester tester) async {
  await tester.tap(find.byKey(const ValueKey('editor-back')));
  await tester.pumpAndSettle();
}
