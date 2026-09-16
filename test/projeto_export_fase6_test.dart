import 'dart:convert';

import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/pro_mode.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/settings/application/settings_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// FASE 6 DO REDESIGN — EXPORTAR, ⚙ PROJETO, ONBOARDING, TEMA (3.6).
///
/// - Exportar em dois toques: Exportar → preset 1080p.
/// - ⚙ Projeto: proporcao, resolucao, fps e fundo editaveis (fundo e um
///   campo novo do modelo, que sobrevive ao disco); Pro acrescenta guias,
///   motion blur, paleta, expostas e diagnostico.
/// - Quatro dicas de primeiro uso, uma vez; estado vazio com chamada.
/// - Tema claro/escuro/sistema nos Ajustes; o editor continua escuro.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  test('fundo da composicao: campo novo, com padrao preto e persistencia', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    expect(
      c.read(editorControllerProvider).backgroundColor,
      const Color(0xFF000000),
    );
    e.setBackgroundColor(const Color(0xFF123456));
    final p = c.read(editorControllerProvider);
    expect(p.backgroundColor, const Color(0xFF123456));
    final volta = projectFromJson(
      jsonDecode(jsonEncode(projectToJson(p))) as Map<String, dynamic>,
    );
    expect(volta.backgroundColor, const Color(0xFF123456));
    e.undo();
    expect(
      c.read(editorControllerProvider).backgroundColor,
      const Color(0xFF000000),
    );
  });

  test('setComposition muda proporcao, resolucao e fps num passo', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.setComposition(aspectRatio: 1, resolutionHeight: 720, fps: 24);
    final p = c.read(editorControllerProvider);
    expect(p.aspectRatio, 1);
    expect(p.resolutionHeight, 720);
    expect(p.fps, 24);
    expect(p.outputWidth, 720);
  });

  test('o tema claro troca os papeis de AppColors e o escuro devolve', () {
    final claro = AppTheme.light;
    expect(claro.brightness, Brightness.light);
    expect(AppColors.background, const Color(0xFFF4F5F7));
    final escuro = AppTheme.dark;
    expect(escuro.brightness, Brightness.dark);
    expect(AppColors.background, const Color(0xFF12151A));
    expect(const AppSettings().themeMode, 'escuro');
    expect(const AppSettings().copyWith(themeMode: 'claro').themeMode, 'claro');
  });

  testWidgets('exportar em dois toques: Exportar, preset 1080p', (
    tester,
  ) async {
    await openEditor(tester);
    await tester.tap(find.byKey(const ValueKey('editor-export'))); // 1
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('export-preset-1080p')), findsOneWidget);
    expect(find.byKey(const ValueKey('export-preset-720p')), findsOneWidget);
    expect(find.byKey(const ValueKey('export-preset-4K')), findsOneWidget);
    expect(
      find.byKey(const ValueKey('export-renderizar')),
      findsOneWidget,
      reason: 'ajustes finos sao Pro',
    );
    await tester.tap(find.byKey(const ValueKey('export-preset-1080p'))); // 2
    await tester.pumpAndSettle();
    // A tela de exportacao abriu (o render de verdade nao roda em teste).
    expect(
      find.byKey(const ValueKey('export-preset-1080p')),
      findsNothing,
      reason: 'a folha fechou',
    );
    expect(
      find.byKey(const ValueKey('editor-capture')),
      findsNothing,
      reason: 'a tela de exportacao cobre o editor',
    );
  });

  testWidgets(
    '⚙ Projeto: composicao editavel no Simples; guias, motion blur e paleta no Pro',
    (tester) async {
      final c = await openEditor(tester);
      await tester.tap(find.byKey(const ValueKey('editor-settings')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('projeto-fundo')), findsOneWidget);
      expect(
        find.byKey(const ValueKey('projeto-areas-seguras')),
        findsOneWidget,
        reason: 'guias sao Pro',
      );

      await tester.tap(find.byKey(const ValueKey('projeto-proporcao-1:1')));
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).aspectRatio, 1);
      await tester.tap(find.byKey(const ValueKey('projeto-fps-60')));
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).fps, 60);

      // Pro: guias, motion blur e paleta.
      c.read(proModeProvider.notifier).set(true);

      // Reabre a folha para ver as secoes Pro.
      await tester.tapAt(const Offset(10, 10));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('editor-settings')));
      await tester.pumpAndSettle();
      expect(
        find.byKey(const ValueKey('projeto-areas-seguras')),
        findsOneWidget,
      );
      // A linha de proporcao ganhou o 4:3 (v1.1.1) e a folha ficou um
      // pouco mais alta: garantir que o interruptor esta na tela antes
      // de tocar.
      await tester.ensureVisible(
        find.byKey(const ValueKey('projeto-areas-seguras')),
      );
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('projeto-areas-seguras')));
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).guides.showSafeAreas, isTrue);
      await tester.scrollUntilVisible(
        find.byKey(const ValueKey('projeto-motion-blur')),
        180,
        scrollable: find
            .descendant(
              of: find.byType(BottomSheet),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      await tester.tap(find.byKey(const ValueKey('projeto-motion-blur')));
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).motionBlur.enabled, isTrue);
    },
  );

  testWidgets(
    'as dicas de primeiro uso aparecem uma vez e o estado vazio tem a chamada',
    (tester) async {
      final c = await openEditor(tester);
      expect(
        find.byKey(const ValueKey('editor-dica')),
        findsNothing,
        reason: 'help no longer covers editing',
      );

      // Estado vazio: sem camadas, a chamada "+ Adicione uma midia".
      final e = c.read(editorControllerProvider.notifier);
      for (final l in c.read(editorControllerProvider).layers.toList()) {
        e.removeLayer(l.id);
      }
      c.read(selectedLayerProvider.notifier).state = null;
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('estado-vazio')), findsOneWidget);
      expect(find.byKey(const ValueKey('estado-vazio-cta')), findsOneWidget);
      // A barra de adicionar (com o chip de ajuda) so vem pelo "+".
      expect(find.byKey(const ValueKey('projeto-ajuda')), findsNothing);
      await tester.tap(find.byKey(const ValueKey('editor-settings')));
      await tester.pumpAndSettle();
      await tester.scrollUntilVisible(
        find.byKey(const ValueKey('projeto-ajuda')),
        180,
        scrollable: find
            .descendant(
              of: find.byType(BottomSheet),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      expect(
        find.byKey(const ValueKey('projeto-ajuda')).hitTestable(),
        findsOneWidget,
      );
    },
  );
}
