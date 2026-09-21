// O PAINEL TEXTO 3D: trocar o metal e trocar a palavra nao podem jogar
// fora o resto da letra.
//
//   * o painel abre com a palavra da camada e as quatro predefinicoes de
//     metal;
//   * trocar o METAL refaz a malha e guarda a palavra e o giro da camada;
//   * trocar o TEXTO refaz a geometria e mantem o metal escolhido.
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/texto3d.dart';
import 'package:flutter/cupertino.dart' show CupertinoTextField;
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'ui/paineis_3d/banco.dart';

/// A aba Material do painel e a ficha de [e] (a fileira rola de lado).
Future<void> _tocarNoMetal(WidgetTester tester, EstiloDoTexto3D e) async {
  final ficha = find.byKey(ValueKey('texto3d-estilo-${e.name}'));
  await tester.ensureVisible(ficha);
  await tester.pumpAndSettle();
  await tester.tap(ficha);
  await tester.pump();
}

void main() {
  testWidgets('abre com o texto da camada e os quatro metais', (tester) async {
    final c = containerNovo();
    final (cena, _) = await criarTexto3D(tester, c, 'AUREA');
    await montar(tester, c, (_) => PainelTexto3D(layerId: cena));

    await tocarNaAba(tester, 'texto3d', 0);
    final campo = tester.widget<CupertinoTextField>(
      find.byKey(const ValueKey('texto3d-campo')),
    );
    expect(campo.controller!.text, 'AUREA');

    await tocarNaAba(tester, 'texto3d', 2);
    for (final e in EstiloDoTexto3D.values) {
      expect(
        find.byKey(ValueKey('texto3d-estilo-${e.name}')),
        findsOneWidget,
        reason: 'o metal ${e.name} tem de estar no painel',
      );
    }
    expect(tester.takeException(), isNull);
  });

  testWidgets('trocar o metal refaz a malha e guarda a palavra e o giro', (
    tester,
  ) async {
    final c = containerNovo();
    final (cena, no) = await criarTexto3D(tester, c, 'AUREA');
    await montar(tester, c, (_) => PainelTexto3D(layerId: cena));
    final antes = cenaDe(c, cena).scene.nodeById(no)!;
    expect(antes.estiloTexto3d, EstiloDoTexto3D.ouro);
    final giroAntes = antes.rotY.valueAt(Duration.zero);

    await tocarNaAba(tester, 'texto3d', 2);
    await _tocarNoMetal(tester, EstiloDoTexto3D.cromo);
    await esperarAMalha(tester);

    final depois = cenaDe(c, cena).scene.nodeById(no)!;
    expect(depois.estiloTexto3d, EstiloDoTexto3D.cromo);
    expect(depois.texto3d!.texto, 'AUREA', reason: 'o texto nao pode se perder');
    expect(
      depois.rotY.valueAt(Duration.zero),
      giroAntes,
      reason: 'o giro da camada continua o que o dono ajustou',
    );
    expect(
      depois.modelAsset,
      isNot(equals(antes.modelAsset)),
      reason: 'o metal novo entra numa malha refeita',
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('editar o texto refaz a geometria e guarda o metal', (
    tester,
  ) async {
    final c = containerNovo();
    final (cena, no) = await criarTexto3D(tester, c, 'AUREA');
    await montar(tester, c, (_) => PainelTexto3D(layerId: cena));
    await tocarNaAba(tester, 'texto3d', 2);
    await _tocarNoMetal(tester, EstiloDoTexto3D.cromo);
    await esperarAMalha(tester);

    await tocarNaAba(tester, 'texto3d', 0);
    await tester.enterText(find.byKey(const ValueKey('texto3d-campo')), 'METAL');
    await tester.testTextInput.receiveAction(TextInputAction.done);
    await esperarAMalha(tester);

    final n = cenaDe(c, cena).scene.nodeById(no)!;
    expect(n.texto3d!.texto, 'METAL');
    expect(
      n.estiloTexto3d,
      EstiloDoTexto3D.cromo,
      reason: 'trocar a palavra nao pode voltar o metal para o padrao',
    );
    expect(tester.takeException(), isNull);
  });
}
