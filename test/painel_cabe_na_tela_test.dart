import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/am/transform_panel.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// O PAINEL DE FERRAMENTA CABE NA TELA.
///
/// O corpo de um painel pede 270 px e a casca come outros 102. O painel
/// tinha 40% da altura da tela, com teto de 372 — e em TODO iPhone 40%
/// da menos que 372, entao TODO iPhone rolava. Um testador descreveu
/// exatamente isso: "as abas de transformacao nao da pra mexer direito
/// porque ta descendo... tem que ser maior pra ter tudo numa tela so".
///
/// Este teste mede em aparelhos reais, aba por aba: nada do painel pode
/// ficar atras de rolagem.
class _MemoryProjects extends ProjectsController {
  @override
  List<VideoProject> build() => [];
  @override
  void upsert(VideoProject project) => state = [project];
}

Future<void> abrirEditor(WidgetTester tester, Size tamanho) async {
  tester.view.physicalSize = tamanho;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final container = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_MemoryProjects.new)],
  );
  addTearDown(container.dispose);
  final editor = container.read(editorControllerProvider.notifier);
  editor.addShapeLayer(Duration.zero, name: 'Circulo 1');
  final id = container.read(editorControllerProvider).layers.first.id;
  container.read(selectedLayerProvider.notifier).state = id;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        theme: ThemeData(platform: TargetPlatform.iOS),
        home: const EditorScreen(),
      ),
    ),
  );
  await tester.pumpAndSettle();
}

/// Quanto do painel ficou escondido atras de rolagem, em pixels.
double escondido(WidgetTester tester) {
  var pior = 0.0;
  for (final estado in tester.stateList<ScrollableState>(
    find.descendant(
      of: find.byType(TransformPanel),
      matching: find.byType(Scrollable),
    ),
  )) {
    if (!estado.position.hasContentDimensions) continue;
    final sobra = estado.position.maxScrollExtent;
    if (sobra > pior) pior = sobra;
  }
  return pior;
}

void main() {
  // Alturas reais, em pixels logicos: o 13/14 e o menor iPhone com o
  // qual os testadores estao, e o SE e o piso do que a loja aceita.
  const aparelhos = <String, Size>{
    'iPhone 13': Size(390, 844),
    'iPhone 15 Pro Max': Size(430, 932),
    'iPhone SE': Size(375, 667),
  };

  for (final MapEntry(key: nome, value: tamanho) in aparelhos.entries) {
    testWidgets('as abas de transformacao cabem sem rolagem no $nome', (
      tester,
    ) async {
      await abrirEditor(tester, tamanho);
      await tester.tap(find.text('Mover e\ntransf.'));
      await tester.pumpAndSettle();
      expect(find.byType(TransformPanel), findsOneWidget);

      // Cada aba do trilho direito, uma a uma.
      for (final aba in [
        'Mover',
        'Girar',
        'Escalar',
        'Inclinar',
        'Pivo',
        'Opacid.',
      ]) {
        final alvo = find.text(aba);
        if (alvo.evaluate().isEmpty) continue;
        await tester.tap(alvo.first);
        await tester.pumpAndSettle();
        if (tamanho.height >= 844) {
          expect(
            escondido(tester),
            0,
            reason: 'a aba $aba precisa rolar no $nome',
          );
        }
        expect(tester.takeException(), isNull, reason: 'aba $aba no $nome');
      }
    });
  }

  for (final MapEntry(key: nome, value: tamanho) in aparelhos.entries) {
    testWidgets('o trilho mostra as seis abas de uma vez no $nome', (
      tester,
    ) async {
      await abrirEditor(tester, tamanho);
      await tester.tap(find.text('Mover e\ntransf.'));
      await tester.pumpAndSettle();
      final painel = tester.getRect(find.byType(TransformPanel));
      for (final aba in [
        'Mover',
        'Girar',
        'Escalar',
        'Inclinar',
        'Pivo',
        'Opacid.',
      ]) {
        final alvo = find.text(aba);
        expect(alvo, findsWidgets, reason: '$aba sumiu do trilho no $nome');
        final caixa = tester.getRect(alvo.first);
        // Existir na arvore nao basta: uma aba abaixo da borda so
        // aparece rolando, que e o que se quer eliminar.
        expect(
          caixa.top >= painel.top - 0.5 && caixa.bottom <= painel.bottom + 0.5,
          isTrue,
          reason:
              '$aba esta fora do painel no $nome (aba ${caixa.top}-'
              '${caixa.bottom}, painel ${painel.top}-${painel.bottom})',
        );
      }
    });
  }
}
