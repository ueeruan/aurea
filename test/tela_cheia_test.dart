// "EFEITOS NECESSARIOS, E TELA CHEIA TAMBEM" (testador, 14/09/2026).
//
// Dentro da tela cheia nao havia como andar pelo video: a timeline some.
// Na casca nova a tela cheia mora no menu da previa (o olho do
// transporte) e o TRANSPORTE fica — play, quadro a quadro e o tempo
// tocavel continuam la.
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/timeline.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/abrir_editor.dart' show openEditor;

void main() {
  testWidgets('tela cheia: previa ocupa a tela, o transporte anda pelo video e volta', (
    tester,
  ) async {
    final c = await openEditor(tester, size: const Size(390, 844));
    expect(find.byType(TimelineDoEditor), findsOneWidget);
    final antes = tester.getSize(find.byType(PreviewStage)).height;

    Future<void> alternarTelaCheia() async {
      await tester.tap(find.byKey(const ValueKey('transporte-modo')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('menu-tela-cheia')));
      await tester.pumpAndSettle();
    }

    await alternarTelaCheia();
    expect(c.read(editorSessionProvider).previewExpanded, isTrue);
    expect(find.byType(TimelineDoEditor), findsNothing);
    expect(find.byKey(const ValueKey('barra-do-topo')), findsNothing);
    expect(find.byKey(const ValueKey('barra-de-transporte')), findsOneWidget);
    final depois = tester.getSize(find.byType(PreviewStage)).height;
    expect(depois, greaterThan(antes + 150), reason: 'a previa cresce de verdade');

    // O transporte anda pelo video dentro da tela cheia.
    final playback = tester.widget<PreviewStage>(find.byType(PreviewStage)).playback;
    await tester.tap(find.byKey(const ValueKey('transporte-proximo')));
    await tester.pump();
    expect(playback.time.value, greaterThan(Duration.zero));

    // O mesmo menu sai da tela cheia.
    await alternarTelaCheia();
    expect(c.read(editorSessionProvider).previewExpanded, isFalse);
    expect(find.byType(TimelineDoEditor), findsOneWidget);
    await tester.pump(const Duration(seconds: 1));
  });
}
