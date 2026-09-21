// POLIMENTO DA EXPORTACAO E DA PREVIA (item 7 do teste de fluxo):
//  * com AJUSTES aberto o botao Exportar ia para baixo da dobra;
//  * "480p" num projeto 9:16 dava 270 x 480 — "Np" e o LADO MENOR = N;
//  * o selo "Full" em ingles sobre a previa.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/preview_resolution.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/export/application/export_engine.dart';
import 'package:aurea/src/features/export/domain/export_settings.dart';
import 'package:aurea/src/features/export/presentation/export_video_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// 1080 x 1920 (9:16) ou 1920 x 1080 (16:9), 30 fps, 2 s de conteudo.
VideoProject _projeto({bool vertical = true}) => VideoProject(
  name: 'p',
  createdAt: DateTime(2026, 9, 21),
  aspectRatio: vertical ? 9 / 16 : 16 / 9,
  resolutionHeight: 1080,
  fps: 30,
  layers: [
    ShapeLayer(
      name: 'forma',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
    ),
  ],
);

Future<void> _abrir(
  WidgetTester tester,
  Size tela, {
  ExportSettings ajustes = const ExportSettings(),
  bool vertical = true,
}) async {
  tester.view.physicalSize = tela;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final container = ProviderContainer();
  addTearDown(container.dispose);
  container
      .read(editorControllerProvider.notifier)
      .openProject(_projeto(vertical: vertical));
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(home: ExportVideoScreen(settings: ajustes)),
    ),
  );
  await tester.pump();
}

String _resumo(WidgetTester tester) =>
    tester.widget<Text>(find.byKey(const ValueKey('export-resumo'))).data!;

void main() {
  group('o botao Exportar fica no rodape', () {
    for (final tela in const [Size(390, 844), Size(360, 640), Size(320, 568)]) {
      testWidgets('${tela.width.toInt()}x${tela.height.toInt()}, AJUSTES '
          'aberto: Exportar inteiro na tela, fora da rolagem', (tester) async {
        await _abrir(tester, tela);
        // Num aparelho baixo a cabeca de AJUSTES pode pedir uma rolagem da
        // lista; o botao, nunca.
        final cabeca = find.byKey(const ValueKey('export-cabeca-ajustes'));
        await tester.ensureVisible(cabeca);
        await tester.pumpAndSettle();
        await tester.tap(cabeca);
        await tester.pumpAndSettle();
        expect(
          find.byKey(const ValueKey('export-ajustes-corpo')),
          findsOneWidget,
        );
        final exportar = find.byKey(const ValueKey('export-exportar'));
        final r = tester.getRect(exportar);
        expect(r.top, greaterThanOrEqualTo(0));
        expect(r.bottom, lessThanOrEqualTo(tela.height));
        // Preso: nao mora dentro do que rola.
        expect(
          find.descendant(
            of: find.byKey(const ValueKey('export-rolagem-dos-ajustes')),
            matching: exportar,
          ),
          findsNothing,
        );
        // Rolar os ajustes ate o fim nao mexe nele.
        await tester.drag(
          find.byKey(const ValueKey('export-rolagem-dos-ajustes')),
          const Offset(0, -600),
        );
        await tester.pumpAndSettle();
        expect(tester.getRect(exportar), r);
        expect(tester.takeException(), isNull);
      });
    }
  });

  group('"Np" e o lado menor', () {
    testWidgets('480p num 9:16: a ficha diz 480 x 854 (e nao 270 x 480)', (
      tester,
    ) async {
      await _abrir(tester, const Size(390, 844));
      await tester.tap(find.byKey(const ValueKey('export-cabeca-ajustes')));
      await tester.pumpAndSettle();
      final p480 = find.descendant(
        of: find.byKey(const ValueKey('export-ajuste-tamanho')),
        matching: find.text('480p'),
      );
      await tester.ensureVisible(p480);
      await tester.pumpAndSettle();
      await tester.tap(p480);
      await tester.pumpAndSettle();
      expect(_resumo(tester), startsWith('480 x 854 · 30 fps'));
    });

    testWidgets('16:9 continua igual: 480p = 854 x 480', (tester) async {
      await _abrir(
        tester,
        const Size(390, 844),
        ajustes: const ExportSettings(size: ExportSize.p480),
        vertical: false,
      );
      expect(_resumo(tester), startsWith('854 x 480 · 30 fps'));
    });

    test('o motor recebe a mesma leitura (sem mudar o motor)', () {
      final vertical = _projeto();
      expect((vertical.outputWidth, vertical.outputHeight), (1080, 1920));
      for (final (tamanho, esperado) in const [
        (ExportSize.p480, (480, 854)),
        (ExportSize.p720, (720, 1280)),
        (ExportSize.p1080, (1080, 1920)),
        (ExportSize.p2160, (2160, 3840)),
        (ExportSize.original, (1080, 1920)),
      ]) {
        final motor = ExportEngine(
          vertical,
          AjustesPeloLadoMenor(ExportSettings(size: tamanho, fps: 24)),
        );
        expect((motor.width, motor.height), esperado, reason: '$tamanho');
        // O resto dos ajustes passa intacto.
        expect(motor.fps, 24);
      }
      final deitado = _projeto(vertical: false);
      final motor = ExportEngine(
        deitado,
        AjustesPeloLadoMenor(const ExportSettings(size: ExportSize.p480)),
      );
      expect((motor.width, motor.height), (854, 480));
      // O DOMINIO nao mudou: a leitura antiga (altura) continua la.
      expect(
        const ExportSettings(size: ExportSize.p480).resolve(1080, 1920),
        (270, 480),
      );
    });
  });

  test('o selo da previa e um numero em toda faixa (nada de "Full")', () {
    expect(PreviewResolution.full.label, '100%');
    for (final r in PreviewResolution.values) {
      expect(r.label, matches(RegExp(r'^[\d,]+%$')), reason: r.name);
    }
  });
}
