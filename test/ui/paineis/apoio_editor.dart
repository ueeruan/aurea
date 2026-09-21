import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'apoio_paineis.dart';

/// Abre o EDITOR INTEIRO (casca, timeline, palco) num aparelho de
/// [tamanho], com as camadas que [preparar] criar antes da tela nascer.
Future<ProviderContainer> abrirEditorInteiro(
  WidgetTester tester, {
  Size tamanho = const Size(390, 844),
  void Function(EditorController c)? preparar,
}) async {
  tester.view.physicalSize = tamanho;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  SharedPreferences.setMockInitialValues(const {});
  final prefs = await SharedPreferences.getInstance();
  final container = ProviderContainer(
    overrides: [
      sharedPreferencesProvider.overrideWithValue(prefs),
      projectsControllerProvider.overrideWith(ProjetosEmMemoria.new),
    ],
  );
  addTearDown(container.dispose);
  preparar?.call(container.read(editorControllerProvider.notifier));
  container.read(selectedLayerProvider.notifier).state = null;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: const MaterialApp(home: EditorScreen()),
    ),
  );
  await tester.pumpAndSettle();
  return container;
}

