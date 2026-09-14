// PARTICULAS DE VOLTA NO "+" (pedido do beta 1.0.5).
//
// A camada de particulas continuou inteira no codigo, mas o botao que a
// criava sumiu quando a folha de adicionar foi refeita (c7216c9). E a aba
// Objeto nao rolava: um quinto cartao em duas colunas ficaria cortado numa
// tela baixa, sem jeito de alcancar.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/add_layer_sheet.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  // A folha modal tem min(270, 31% da tela); o painel do editor, 48%.
  for (final (nome, largura, altura) in [
    ('folha num iPhone SE', 320.0, 176.0),
    ('folha num celular pequeno', 375.0, 207.0),
    ('painel do editor', 390.0, 330.0),
  ]) {
    testWidgets('Objeto > Particulas cria a camada ($nome)', (tester) async {
      tester.view.physicalSize = Size(largura, 800);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final c = ProviderContainer();
      addTearDown(c.dispose);
      c
          .read(editorControllerProvider.notifier)
          .openProject(
            VideoProject(
              name: 'p',
              createdAt: DateTime(2026, 9, 14),
              layers: const [],
            ),
          );
      var fechou = false;
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: MaterialApp(
            home: Scaffold(
              body: Align(
                alignment: Alignment.bottomCenter,
                child: SizedBox(
                  width: largura,
                  height: altura,
                  child: AddLayerPanel(
                    onClose: () => fechou = true,
                    playhead: Duration.zero,
                    initialTab: AddTab.objeto,
                  ),
                ),
              ),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      for (final rotulo in [
        'Scene 3D',
        'Grupo Vazio',
        'Nulo',
        'Elemento / Projeto',
        'Partículas',
      ]) {
        expect(
          find.text(rotulo).hitTestable(),
          findsOneWidget,
          reason: '"$rotulo" ficou fora do alcance na $nome',
        );
      }
      expect(tester.takeException(), isNull);

      await tester.tap(find.byKey(const ValueKey('add-particulas')));
      await tester.pumpAndSettle();
      expect(fechou, isTrue);
      expect(
        c.read(editorControllerProvider).layers.single,
        isA<ParticlesLayer>(),
      );
    });
  }
}
