import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/abrir_editor.dart' show openEditor;

/// O RELATO: "os testadores nao estao conseguindo editar".
///
/// A causa: so a barrinha da timeline selecionava. Quem abre o app toca
/// no objeto NA TELA — e nada acontecia. Aqui o palco vira o lugar de
/// editar: tocar seleciona, arrastar move, a alca de baixo redimensiona
/// e a de cima gira.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  /// O centro do palco, onde as duas formas do projeto de teste estao.
  Offset centroDoPalco(WidgetTester tester) =>
      tester.getRect(find.byType(PreviewStage)).center;

  testWidgets('tocar no objeto no palco seleciona; tocar no vazio tira', (
    tester,
  ) async {
    final c = await openEditor(tester);
    expect(c.read(selectedLayerProvider), isNull);

    await tester.tapAt(centroDoPalco(tester));
    await tester.pumpAndSettle();
    final id = c.read(selectedLayerProvider);
    expect(id, isNotNull, reason: 'o toque no objeto selecionou');
    expect(
      id,
      c.read(editorControllerProvider).layers.first.id,
      reason: 'a de cima ganha',
    );
    // E a barra da camada (as ferramentas) apareceu sozinha.
    expect(find.byKey(const ValueKey('ferramenta-transformar')), findsOneWidget);

    // Canto do palco: nao ha camada ali.
    final palco = tester.getRect(find.byType(PreviewStage));
    await tester.tapAt(Offset(palco.left + 6, palco.top + 6));
    await tester.pumpAndSettle();
    expect(c.read(selectedLayerProvider), isNull, reason: 'o vazio tira');
  });

  testWidgets('arrastar no palco move a camada selecionada', (tester) async {
    final c = await openEditor(tester);
    await tester.tapAt(centroDoPalco(tester));
    await tester.pumpAndSettle();
    final id = c.read(selectedLayerProvider)!;
    Offset onde() => c
        .read(editorControllerProvider)
        .layerById(id)!
        .position
        .valueAt(Duration.zero);
    final antes = onde();

    await tester.dragFrom(centroDoPalco(tester), const Offset(0, 60));
    await tester.pumpAndSettle();
    expect(onde().dy, greaterThan(antes.dy + 10), reason: 'desceu');
    // Arrasto manual no Pro nao cria keyframes sem auto-key armado.
    expect(
      c.read(editorControllerProvider).layerById(id)!.position.isAnimated,
      isFalse,
    );
  });

  testWidgets('a alca de baixo redimensiona e a de cima gira', (tester) async {
    final c = await openEditor(tester);
    await tester.tapAt(centroDoPalco(tester));
    await tester.pumpAndSettle();
    final id = c.read(selectedLayerProvider)!;
    expect(find.byKey(const ValueKey('alca-escala')), findsOneWidget);
    expect(find.byKey(const ValueKey('alca-giro')), findsOneWidget);

    double escala() => c
        .read(editorControllerProvider)
        .layerById(id)!
        .scaleX
        .valueAt(Duration.zero);
    double giroAtual() => c
        .read(editorControllerProvider)
        .layerById(id)!
        .rotation
        .valueAt(Duration.zero);
    final escalaAntes = escala();
    final alca = tester
        .getRect(find.byKey(const ValueKey('alca-escala')))
        .center;
    await tester.dragFrom(alca, const Offset(40, 40));
    await tester.pumpAndSettle();
    expect(escala(), greaterThan(escalaAntes), reason: 'a alca aumentou');

    final giro = tester.getRect(find.byKey(const ValueKey('alca-giro'))).center;
    await tester.dragFrom(giro, const Offset(-60, 40));
    await tester.pumpAndSettle();
    expect(giroAtual().abs(), greaterThan(5), reason: 'a alca girou');
  });

  testWidgets('camada bloqueada nao e pega pelo toque no palco', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    final topo = c.read(editorControllerProvider).layers.first.id;
    e.toggleLocked(topo);
    await tester.pumpAndSettle();
    await tester.tapAt(centroDoPalco(tester));
    await tester.pumpAndSettle();
    expect(c.read(selectedLayerProvider), isNot(topo));
  });
}
