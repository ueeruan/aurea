// A FOLHA DE NOVO PROJETO nascia marcada em 16:9 (o padrao dos Ajustes).
// App de celular: quem NUNCA mexeu nos Ajustes comeca em 9:16; quem
// escolheu outra proporcao la continua com a sua.
import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/settings/application/settings_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../home/apoio_home.dart';

Future<VideoProject> _criarPeloPadrao(
  WidgetTester tester,
  ProviderContainer c,
) async {
  await tester.tap(find.byKey(const ValueKey('novo-projeto')));
  await assentar(tester);
  await tester.ensureVisible(find.byKey(const ValueKey('criar-projeto')));
  await tester.tap(find.byKey(const ValueKey('criar-projeto')));
  await assentar(tester);
  return c.read(projectsControllerProvider).first;
}

void main() {
  testWidgets('Ajustes nunca mexidos: a folha nasce em 9:16 e cria 9:16', (
    tester,
  ) async {
    final (c, _) = await montarInicio(tester);
    await tester.tap(find.byKey(const ValueKey('novo-projeto')));
    await assentar(tester);
    expect(find.text('1080 × 1920 · 30 fps'), findsOneWidget);
    Navigator.of(tester.element(find.byKey(const ValueKey('criar-projeto'))))
        .pop();
    await assentar(tester);
    final novo = await _criarPeloPadrao(tester, c);
    expect((novo.outputWidth, novo.outputHeight), (1080, 1920));
    await c.read(projectsControllerProvider.notifier).flush();
  });

  testWidgets('quem escolheu 16:9 nos Ajustes continua com 16:9', (
    tester,
  ) async {
    final (c, _) = await montarInicio(
      tester,
      prefsIniciais: {'settings.defaultAspect': '16:9'},
    );
    expect(c.read(settingsControllerProvider).defaultAspectKey, '16:9');
    final novo = await _criarPeloPadrao(tester, c);
    expect((novo.outputWidth, novo.outputHeight), (1920, 1080));
    await c.read(projectsControllerProvider.notifier).flush();
  });

  test('a escolha feita nos Ajustes e gravada e vale na volta', () async {
    SharedPreferences.setMockInitialValues({});
    final prefs = await SharedPreferences.getInstance();
    final c = ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
    );
    addTearDown(c.dispose);
    expect(c.read(settingsControllerProvider).defaultAspectKey, '9:16');
    c.read(settingsControllerProvider.notifier).setDefaultAspect('16:9');
    final outro = ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
    );
    addTearDown(outro.dispose);
    expect(outro.read(settingsControllerProvider).defaultAspectKey, '16:9');
  });
}
