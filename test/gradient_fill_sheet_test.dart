import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';
import 'package:aurea/src/features/editor/presentation/am/gradient_fill_sheet.dart';
import 'package:aurea/src/features/projects/domain/reference_rebuild_template.dart';

void main() {
  testWidgets('gradiente abre, ajusta e fecha sem remover a pagina', (
    tester,
  ) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    RecentSheets.instance.clear();
    addTearDown(RecentSheets.instance.clear);
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
    expect(find.text('Cores e distribuicao'), findsOneWidget);
    final ruler = find.byType(AmTickRuler).first;
    await tester.drag(ruler, const Offset(-40, 0));
    await tester.pumpAndSettle();
    final layer =
        container.read(editorControllerProvider).layerById('rebuild_floor')!
            as ShapeLayer;
    final g = layer.contents.whereType<ShapeGradientFill>().single;
    expect(g.resolvedStops.first, inInclusiveRange(0, .27));
    expect(g.resolvedStops.first, greaterThan(0));
    await tester.tap(find.byIcon(CupertinoIcons.chevron_back));
    await tester.pumpAndSettle();
    expect(find.text('editor'), findsOneWidget);
    expect(find.text('Cores e distribuicao'), findsNothing);
    expect(tester.takeException(), isNull);
  });
}
