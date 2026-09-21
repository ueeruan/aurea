import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/degrade.dart';
import 'package:aurea/src/features/projects/domain/reference_rebuild_template.dart';

void main() {
  testWidgets('gradiente abre, ajusta e fecha sem remover a pagina', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container
        .read(editorControllerProvider.notifier)
        .openProject(buildReferenceRebuildTemplate());
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          theme: ThemeData(platform: TargetPlatform.iOS),
          home: Scaffold(
            body: Builder(
              builder: (context) => TextButton(
                onPressed: () =>
                    showGradientFillSheet(context, 'rebuild_floor'),
                child: const Text('editor'),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('editor'));
    await tester.pumpAndSettle();
    // O titulo passa pelo catalogo (sai em ingles no locale do teste): a
    // folha e reconhecida pela chave da primeira parada.

    // A primeira parada comeca em 0 e nunca passa da vizinha (.27).
    final caixa = find.byKey(const ValueKey('valor-degrade-posicao-0'));
    expect(caixa, findsOneWidget);
    final g = await tester.startGesture(tester.getCenter(caixa));
    for (var i = 0; i < 12; i++) {
      await g.moveBy(const Offset(8, 0));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await g.up();
    await tester.pumpAndSettle();
    final layer =
        container.read(editorControllerProvider).layerById('rebuild_floor')!
            as ShapeLayer;
    final degrade = layer.contents.whereType<ShapeGradientFill>().single;
    expect(degrade.resolvedStops.first, inInclusiveRange(0, .27));
    expect(degrade.resolvedStops.first, greaterThan(0));

    await tester.tap(find.byKey(const ValueKey('folha-fechar')));
    await tester.pumpAndSettle();
    expect(find.text('editor'), findsOneWidget);
    expect(find.byKey(const ValueKey('valor-degrade-posicao-0')), findsNothing);
    expect(tester.takeException(), isNull);
  });
}
