// O SELO "AUTO" — a condicao que `docs/keyframe-explicito.md` impoe ao
// keyframe automatico: enquanto estiver ligado, ele e ANUNCIADO na tela,
// e sai com UM toque. Antes o unico sinal era um item marcado dentro de
// um menu fechado, e as marcas "apareciam sozinhas".
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/widgets/rails_do_painel.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

Future<ProviderContainer> _montar(WidgetTester tester) async {
  final c = ProviderContainer();
  addTearDown(c.dispose);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(
          body: Align(
            alignment: Alignment.topLeft,
            child: SizedBox(width: 44, child: SeloAutoKeyframe()),
          ),
        ),
      ),
    ),
  );
  return c;
}

void main() {
  testWidgets('desligado, o selo nao ocupa lugar nenhum', (tester) async {
    final c = await _montar(tester);
    expect(c.read(autoKeyframeProvider), isFalse);
    expect(find.text('AUTO'), findsNothing);
  });

  testWidgets('ligado, aparece; e um toque desliga', (tester) async {
    final c = await _montar(tester);
    c.read(autoKeyframeProvider.notifier).state = true;
    await tester.pump();
    expect(find.text('AUTO'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('rail-auto-keyframe')));
    await tester.pump();
    expect(c.read(autoKeyframeProvider), isFalse);
    expect(find.text('AUTO'), findsNothing);
  });
}
