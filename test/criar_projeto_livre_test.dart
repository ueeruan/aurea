import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/domain/project_presets.dart';
import 'package:aurea/src/features/projects/presentation/new_project_sheet.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

Future<VideoProject?> _abrir(
  WidgetTester tester,
  Future<void> Function() mexer,
) async {
  VideoProject? criado;
  SharedPreferences.setMockInitialValues(const {});
  final prefs = await SharedPreferences.getInstance();
  await tester.pumpWidget(
    ProviderScope(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
      child: MaterialApp(
        home: Scaffold(
          body: Builder(
            builder: (context) => Center(
              child: TextButton(
                onPressed: () async =>
                    criado = await showNewProjectSheet(context),
                child: const Text('abrir'),
              ),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.tap(find.text('abrir'));
  await tester.pumpAndSettle();
  await mexer();
  return criado;
}

void main() {
  testWidgets('o catálogo de formatos ganhou 4:3 e a medida livre', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(420, 950);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    expect(ProjectPresets.aspects.map((a) => a.key), contains('4:3'));
    expect(ProjectPresets.resolutions, contains(1440));
    await _abrir(tester, () async {
      expect(find.byKey(const ValueKey('formato-4:3')), findsOneWidget);
      expect(find.byKey(const ValueKey('formato-livre')), findsOneWidget);
      // Sem escolher livre, os campos de medida nao existem.
      expect(find.byKey(const ValueKey('livre-largura')), findsNothing);
      await tester.tap(find.byKey(const ValueKey('formato-livre')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('livre-largura')), findsOneWidget);
      expect(find.byKey(const ValueKey('livre-altura')), findsOneWidget);
      // Medida livre dispensa a regua de resolucao: os numeros JA SAO a
      // resolucao.
      expect(find.text('Resolução'), findsNothing);
    });
  });

  testWidgets('a medida livre cria o projeto com o quadro digitado', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(420, 950);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final criado = await _abrir(tester, () async {
      await tester.tap(find.byKey(const ValueKey('formato-livre')));
      await tester.pumpAndSettle();
      await tester.enterText(
        find.byKey(const ValueKey('livre-largura')),
        '1920',
      );
      await tester.enterText(find.byKey(const ValueKey('livre-altura')), '480');
      await tester.pumpAndSettle();
      await tester.ensureVisible(find.byKey(const ValueKey('criar-projeto')));
      await tester.tap(find.byKey(const ValueKey('criar-projeto')));
      await tester.pumpAndSettle();
    });
    expect(criado, isNotNull);
    expect(criado!.outputWidth, 1920);
    expect(criado.outputHeight, 480);
  });

  testWidgets('número torto não derruba: a medida é presa na faixa', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(420, 950);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final criado = await _abrir(tester, () async {
      await tester.tap(find.byKey(const ValueKey('formato-livre')));
      await tester.pumpAndSettle();
      await tester.enterText(find.byKey(const ValueKey('livre-largura')), '7');
      await tester.enterText(find.byKey(const ValueKey('livre-altura')), 'abc');
      await tester.pumpAndSettle();
      await tester.ensureVisible(find.byKey(const ValueKey('criar-projeto')));
      await tester.tap(find.byKey(const ValueKey('criar-projeto')));
      await tester.pumpAndSettle();
    });
    expect(criado, isNotNull);
    expect(criado!.outputWidth, greaterThanOrEqualTo(64));
    expect(criado.outputHeight, greaterThanOrEqualTo(64));
    expect(tester.takeException(), isNull);
  });

  testWidgets('voltar para um formato pronto esconde a medida livre', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(420, 950);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final criado = await _abrir(tester, () async {
      await tester.tap(find.byKey(const ValueKey('formato-livre')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('formato-9:16')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('livre-largura')), findsNothing);
      await tester.ensureVisible(find.byKey(const ValueKey('criar-projeto')));
      await tester.tap(find.byKey(const ValueKey('criar-projeto')));
      await tester.pumpAndSettle();
    });
    expect(criado!.aspectRatio, closeTo(9 / 16, .001));
  });
}
