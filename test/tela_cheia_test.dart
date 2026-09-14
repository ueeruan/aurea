// "EFEITOS NECESSARIOS, E TELA CHEIA TAMBEM" (testador, 14/09/2026).
//
// O botao de tela cheia existia com cara de visor e, dentro dela, nao
// havia como andar pelo video: a timeline some. Agora o botao diz o que
// faz e a tela cheia tem a propria barra de tempo.
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

void main() {
  testWidgets('tela cheia: previa ocupa a tela, barra de tempo anda pelo video e volta', (
    tester,
  ) async {
    final c = await openEditor(tester, size: const Size(390, 844));
    expect(find.byType(AmTimeline), findsOneWidget);
    final antes = tester.getSize(find.byType(PreviewStage)).height;

    await tester.tap(find.byKey(const ValueKey('transport-expand')));
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).previewExpanded, isTrue);
    expect(find.byType(AmTimeline), findsNothing);
    expect(find.byKey(const ValueKey('tela-cheia-tempo')), findsOneWidget);
    final depois = tester.getSize(find.byType(PreviewStage)).height;
    expect(depois, greaterThan(antes + 150), reason: 'a previa cresce de verdade');

    // Tocar no fim do trilho leva o cabecote para perto do fim.
    final playback = tester.widget<PreviewStage>(find.byType(PreviewStage)).playback;
    final trilho = tester.getRect(find.byKey(const ValueKey('tela-cheia-trilho')));
    await tester.tapAt(Offset(trilho.right - 2, trilho.center.dy));
    await tester.pump();
    final total = playback.durationOf();
    expect(
      playback.time.value.inMilliseconds,
      greaterThan((total.inMilliseconds * 0.9).round()),
    );
    // Arrastar para o comeco volta.
    await tester.dragFrom(
      Offset(trilho.right - 4, trilho.center.dy),
      Offset(-(trilho.width - 8), 0),
    );
    await tester.pump();
    expect(playback.time.value.inMilliseconds, lessThan((total.inMilliseconds * 0.1).round()));

    // O mesmo botao sai da tela cheia.
    await tester.tap(find.byKey(const ValueKey('transport-expand')));
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).previewExpanded, isFalse);
    expect(find.byType(AmTimeline), findsOneWidget);
    expect(find.byKey(const ValueKey('tela-cheia-tempo')), findsNothing);
    await tester.pump(const Duration(seconds: 1));
  });
}
